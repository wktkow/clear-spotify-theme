// main.cpp — Linux audio capture for the Spotify visualizer.
// Captures from PulseAudio/PipeWire monitor source, processes audio
// with cava-style FFT + gravity smoothing, sends 70 bars over WebSocket.
// The audio read naturally clocks at exactly 60fps.
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
#include <pulse/simple.h>
#include <pulse/error.h>

#include "../common/protocol.h"
#include "../common/fft.h"
#include "../common/ws_server.h"

static std::atomic<bool> g_running{true};

static void onSignal(int) { g_running = false; }

int main() {
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    fprintf(stderr, "[vis] Spotify visualizer audio bridge (Linux)\n");
    fprintf(stderr, "[vis] FFT %d, bars %d, %d Hz, %d fps (%d samples/frame)\n",
            FFT_SIZE, BAR_COUNT, SAMPLE_RATE, SEND_FPS, FRAME_SAMPLES);

    // --- WebSocket server ---
    WsServer ws;
    if (!ws.start(WS_PORT)) {
        fprintf(stderr, "[vis] FATAL: could not start WebSocket server\n");
        return 1;
    }

    // --- PulseAudio monitor source ---
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
    pa_simple* pa = pa_simple_new(
        nullptr, "ClearVis", PA_STREAM_RECORD,
        "@DEFAULT_MONITOR@", "Audio Visualizer",
        &spec, nullptr, &battr, &paErr
    );
    if (!pa) {
        fprintf(stderr, "[vis] FATAL: pa_simple_new: %s\n", pa_strerror(paErr));
        return 1;
    }
    fprintf(stderr, "[vis] PulseAudio connected\n");

    // --- Main loop ---
    initProcessor();
    float chunk[FRAME_SAMPLES];
    float bars[BAR_COUNT];
    bool wasIdle = true;

    fprintf(stderr, "[vis] Waiting for client on ws://127.0.0.1:%d\n", WS_PORT);

    while (g_running) {
        ws.poll();

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
            fprintf(stderr, "[vis] Client connected, streaming at %d fps\n", SEND_FPS);
        }

        // Blocking read of exactly FRAME_SAMPLES (~16.67ms at 44100 Hz).
        // This is the 60fps clock — driven by the audio hardware.
        int ret = pa_simple_read(pa, chunk, sizeof(chunk), &paErr);
        if (ret < 0) {
            fprintf(stderr, "[vis] pa_simple_read: %s\n", pa_strerror(paErr));
            break;
        }

        // Process: sliding-window FFT, binning, AGC, gravity smoothing
        processFrame(chunk, bars);

        // Send 70 floats as binary WebSocket frame
        ws.sendBinary(bars, sizeof(bars));
    }

    fprintf(stderr, "\n[vis] Shutting down...\n");
    pa_simple_free(pa);
    ws.stop();
    return 0;
}
