// main.cpp — Linux audio capture for the Spotify visualizer.
// Captures from PulseAudio/PipeWire monitor source, processes audio
// with cava-style FFT + gravity smoothing, sends 70 bars over WebSocket.
// Supports source enumeration and live source switching via WebSocket commands.
//
// Build:  make
// Run:    ./vis-capture

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include <mutex>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/pulseaudio.h>

#include "../common/protocol.h"
#include "../common/fft.h"
#include "../common/ws_server.h"

static std::atomic<bool> g_running{true};

static void onSignal(int) { g_running = false; }

// --- PulseAudio source enumeration ---
struct SourceInfo {
    std::string name;        // PA internal name (e.g. "alsa_output.pci-xxx.monitor")
    std::string description; // Human-readable (e.g. "Monitor of Built-in Audio")
};

static std::vector<SourceInfo> enumerateSources() {
    std::vector<SourceInfo> result;

    pa_mainloop* ml = pa_mainloop_new();
    if (!ml) return result;
    pa_mainloop_api* api = pa_mainloop_get_api(ml);
    pa_context* ctx = pa_context_new(api, "ClearVis-Enum");
    if (!ctx) { pa_mainloop_free(ml); return result; }

    struct EnumState {
        std::vector<SourceInfo>* list;
        bool done;
        bool ready;
    } state{&result, false, false};

    pa_context_set_state_callback(ctx, [](pa_context* c, void* ud) {
        auto* st = (EnumState*)ud;
        auto s = pa_context_get_state(c);
        if (s == PA_CONTEXT_READY) {
            st->ready = true;
        } else if (s == PA_CONTEXT_FAILED || s == PA_CONTEXT_TERMINATED) {
            st->done = true;
        }
    }, &state);

    pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr);

    // Wait for connection
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!state.ready && !state.done && std::chrono::steady_clock::now() < deadline) {
        pa_mainloop_iterate(ml, 0, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (state.ready) {
        pa_context_get_source_info_list(ctx, [](pa_context*, const pa_source_info* info, int eol, void* ud2) {
            auto* st2 = (EnumState*)ud2;
            if (eol > 0 || !info) { st2->done = true; return; }
            // Only include monitor sources (loopback of sinks)
            if (info->monitor_of_sink != PA_INVALID_INDEX) {
                st2->list->push_back({info->name, info->description ? info->description : info->name});
            }
        }, &state);

        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!state.done && std::chrono::steady_clock::now() < deadline) {
            pa_mainloop_iterate(ml, 0, nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    pa_mainloop_free(ml);
    return result;
}

// Build JSON source list: {"sources":[{"name":"...","desc":"..."},...]}
static std::string buildSourcesJson(const std::vector<SourceInfo>& sources) {
    std::string json = "{\"sources\":[";
    for (size_t i = 0; i < sources.size(); i++) {
        if (i > 0) json += ",";
        // Escape quotes in descriptions
        std::string desc = sources[i].description;
        std::string name = sources[i].name;
        for (auto* s : {&desc, &name}) {
            size_t pos = 0;
            while ((pos = s->find('"', pos)) != std::string::npos) {
                s->replace(pos, 1, "\\\"");
                pos += 2;
            }
        }
        json += "{\"name\":\"" + name + "\",\"desc\":\"" + desc + "\"}";
    }
    json += "]}";
    return json;
}

int main() {
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    signal(SIGPIPE, SIG_IGN);

    fprintf(stderr, "[vis] Spotify visualizer audio bridge (Linux)\n");
    fprintf(stderr, "[vis] FFT %d, bars %d, %d Hz, 1 snapshot/sec (%d samples/frame)\n",
            FFT_SIZE, BAR_COUNT, SAMPLE_RATE, FRAME_SAMPLES);

    // --- WebSocket server ---
    WsServer ws;
    if (!ws.start(WS_PORT)) {
        fprintf(stderr, "[vis] FATAL: could not start WebSocket server\n");
        return 1;
    }

    // --- Current source (default = system default monitor) ---
    std::string currentSource = "@DEFAULT_MONITOR@";
    std::atomic<bool> sourceChangeRequested{false};
    std::string pendingSource;
    std::mutex sourceMtx;

    // Dynamic send rate (default 30fps = 33ms)
    std::atomic<int> sendIntervalMs{33};

    // Handle text commands from WebSocket client
    ws.onText = [&](const std::string& msg) {
        if (msg == "GET_SOURCES") {
            auto sources = enumerateSources();
            std::string json = buildSourcesJson(sources);
            fprintf(stderr, "[vis] Sending %zu sources to client\n", sources.size());
            ws.sendText(json);
        } else if (msg.rfind("SET_SOURCE:", 0) == 0) {
            std::string src = msg.substr(11);
            fprintf(stderr, "[vis] Source change requested: %s\n", src.c_str());
            std::lock_guard<std::mutex> lock(sourceMtx);
            pendingSource = src;
            sourceChangeRequested = true;
        } else if (msg.rfind("SET_FPS:", 0) == 0) {
            int fps = std::atoi(msg.substr(8).c_str());
            if (fps == 24 || fps == 30 || fps == 60) {
                sendIntervalMs = 1000 / fps;
                fprintf(stderr, "[vis] Send rate changed to %d fps (%d ms)\n", fps, sendIntervalMs.load());
                ws.sendText("{\"fpsChanged\":" + std::to_string(fps) + "}");
            }
        } else if (msg.rfind("SET_FREQ_MAX:", 0) == 0) {
            int freq = std::atoi(msg.substr(13).c_str());
            if (freq == 10000 || freq == 12000 || freq == 14000 || freq == 16000 || freq == 18000) {
                g_freqMax = (float)freq;
                initProcessor();
                fprintf(stderr, "[vis] Freq max changed to %d Hz\n", freq);
                ws.sendText("{\"freqMaxChanged\":" + std::to_string(freq) + "}");
            }
        } else if (msg.rfind("SET_BAR_COUNT:", 0) == 0) {
            int count = std::atoi(msg.substr(14).c_str());
            if (count == 8 || count == 16 || count == 24 || count == 36 || count == 72 || count == 100 || count == 144) {
                g_barCount = count;
                initProcessor();
                fprintf(stderr, "[vis] Bar count changed to %d\n", count);
                ws.sendText("{\"barCountChanged\":" + std::to_string(count) + "}");
            }
        }
    };

    // --- PulseAudio capture (reconnectable) ---
    pa_simple* pa = nullptr;
    auto connectPA = [&](const std::string& sourceName) -> bool {
        if (pa) { pa_simple_free(pa); pa = nullptr; }

        pa_sample_spec spec{};
        spec.format   = PA_SAMPLE_FLOAT32LE;
        spec.rate     = SAMPLE_RATE;
        spec.channels = 1;

        pa_buffer_attr battr{};
        battr.maxlength = (uint32_t)-1;
        battr.tlength   = (uint32_t)-1;
        battr.prebuf    = (uint32_t)-1;
        battr.minreq    = (uint32_t)-1;
        battr.fragsize  = FRAME_SAMPLES * sizeof(float);

        int paErr;
        pa = pa_simple_new(
            nullptr, "ClearVis", PA_STREAM_RECORD,
            sourceName.c_str(), "Audio Visualizer",
            &spec, nullptr, &battr, &paErr
        );
        if (!pa) {
            fprintf(stderr, "[vis] pa_simple_new(%s): %s\n", sourceName.c_str(), pa_strerror(paErr));
            return false;
        }
        fprintf(stderr, "[vis] PulseAudio connected to: %s\n", sourceName.c_str());
        return true;
    };

    if (!connectPA(currentSource)) {
        fprintf(stderr, "[vis] FATAL: could not connect to default monitor\n");
        return 1;
    }

    // --- Main loop ---
    initProcessor();
    float chunk[FRAME_SAMPLES];
    float bars[MAX_BAR_COUNT];
    bool wasIdle = true;
    auto lastSend = std::chrono::steady_clock::now();

    fprintf(stderr, "[vis] Waiting for client on ws://127.0.0.1:%d\n", WS_PORT);

    while (g_running) {
        ws.poll();

        // Handle source change request
        if (sourceChangeRequested) {
            std::string newSrc;
            {
                std::lock_guard<std::mutex> lock(sourceMtx);
                newSrc = pendingSource;
                sourceChangeRequested = false;
            }
            if (newSrc != currentSource) {
                if (connectPA(newSrc)) {
                    currentSource = newSrc;
                    initProcessor();
                    ws.sendText("{\"sourceChanged\":\"" + currentSource + "\"}");
                } else {
                    fprintf(stderr, "[vis] Failed to switch, reverting to %s\n", currentSource.c_str());
                    connectPA(currentSource);
                    ws.sendText("{\"sourceError\":\"Failed to connect to source\"}");
                }
            }
        }

        if (!ws.hasClient()) {
            wasIdle = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // Client just connected — flush stale audio, reset processor
        if (wasIdle) {
            pa_simple_flush(pa, nullptr);
            initProcessor();
            wasIdle = false;
            lastSend = std::chrono::steady_clock::now();
            fprintf(stderr, "[vis] Client connected, streaming 1 snapshot/sec\n");
        }

        // Blocking read of exactly FRAME_SAMPLES (~16.67ms at 44100 Hz).
        int paErr;
        int ret = pa_simple_read(pa, chunk, sizeof(chunk), &paErr);
        if (ret < 0) {
            fprintf(stderr, "[vis] pa_simple_read: %s\n", pa_strerror(paErr));
            break;
        }

        // Process: sliding-window FFT, binning, AGC, gravity smoothing
        processFrame(chunk, bars);

        // Send bars at configured frame rate
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSend).count() >= sendIntervalMs.load()) {
            ws.sendBinary(bars, g_barCount * sizeof(float));
            lastSend = now;
        }
    }

    fprintf(stderr, "\n[vis] Shutting down...\n");
    if (pa) pa_simple_free(pa);
    ws.stop();
    return 0;
}
