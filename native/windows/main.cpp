// main.cpp — Windows audio capture for the Spotify visualizer.
// Captures from WASAPI loopback, processes audio with cava-style
// FFT + gravity smoothing, sends 70 bars over WebSocket.
//
// Build:  build.bat
// Run:    vis-capture.exe

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <atomic>
#include <chrono>
#include <thread>
#include <string>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "ws2_32.lib")

#include "../common/protocol.h"
#include "../common/fft.h"
#include "../common/ws_server.h"

static std::atomic<bool> g_running{true};

static BOOL WINAPI consoleHandler(DWORD sig) {
    if (sig == CTRL_C_EVENT || sig == CTRL_CLOSE_EVENT) {
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

// Helper: convert interleaved multi-channel audio to mono float
static void toMono(const BYTE* src, float* dst, UINT32 frames,
                   WORD channels, WORD bitsPerSample, bool isFloat) {
    for (UINT32 f = 0; f < frames; f++) {
        float sum = 0.0f;
        for (WORD c = 0; c < channels; c++) {
            if (isFloat && bitsPerSample == 32) {
                sum += ((const float*)src)[f * channels + c];
            } else if (bitsPerSample == 16) {
                int16_t s16 = ((const int16_t*)src)[f * channels + c];
                sum += s16 / 32768.0f;
            } else if (bitsPerSample == 24) {
                const BYTE* p = src + (f * channels + c) * 3;
                int32_t s24 = (p[0]) | (p[1] << 8) | ((int8_t)p[2] << 16);
                sum += s24 / 8388608.0f;
            }
        }
        dst[f] = sum / channels;
    }
}

int main() {
    SetConsoleCtrlHandler(consoleHandler, TRUE);
    fprintf(stderr, "[vis] Spotify visualizer audio bridge (Windows)\n");
    fprintf(stderr, "[vis] FFT %d, bars %d, %d fps (%d samples/frame)\n",
            FFT_SIZE, BAR_COUNT, SEND_FPS, FRAME_SAMPLES);

    // --- Start WebSocket server ---
    WsServer ws;
    if (!ws.start(WS_PORT)) {
        fprintf(stderr, "[vis] FATAL: could not start WebSocket server\n");
        return 1;
    }

    // Dynamic send rate (default 30fps = 33ms)
    std::atomic<int> sendIntervalMs{33};

    // Handle text commands from WebSocket client.
    // Windows WASAPI loopback always captures the default render device,
    // so there are no selectable sources.  We respond to GET_SOURCES
    // with a single "default" entry so the UI knows it's Windows.
    ws.onText = [&](const std::string& msg) {
        if (msg == "GET_SOURCES") {
            ws.sendText("{\"sources\":[{\"name\":\"default\",\"desc\":\"Default Audio Output (WASAPI Loopback)\"}]}");
        } else if (msg.rfind("SET_SOURCE:", 0) == 0) {
            // No-op on Windows — always uses default loopback
            ws.sendText("{\"sourceChanged\":\"default\"}");
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

    // --- Initialize COM and WASAPI ---
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* audioClient = nullptr;
    IAudioCaptureClient* captureClient = nullptr;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  (void**)&enumerator);
    if (FAILED(hr)) { fprintf(stderr, "[vis] FATAL: CoCreateInstance failed\n"); return 1; }

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) { fprintf(stderr, "[vis] FATAL: GetDefaultAudioEndpoint failed\n"); return 1; }

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
    if (FAILED(hr)) { fprintf(stderr, "[vis] FATAL: Activate IAudioClient failed\n"); return 1; }

    WAVEFORMATEX* mixFormat = nullptr;
    hr = audioClient->GetMixFormat(&mixFormat);
    if (FAILED(hr)) { fprintf(stderr, "[vis] FATAL: GetMixFormat failed\n"); return 1; }

    fprintf(stderr, "[vis] Mix format: %d Hz, %d ch, %d bits\n",
            mixFormat->nSamplesPerSec, mixFormat->nChannels, mixFormat->wBitsPerSample);

    // Initialize in loopback mode — captures the audio output
    hr = audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        200000,  // 20ms buffer in 100ns units
        0,
        mixFormat,
        nullptr
    );
    if (FAILED(hr)) { fprintf(stderr, "[vis] FATAL: Initialize loopback failed (0x%lx)\n", hr); return 1; }

    hr = audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient);
    if (FAILED(hr)) { fprintf(stderr, "[vis] FATAL: GetService failed\n"); return 1; }

    hr = audioClient->Start();
    if (FAILED(hr)) { fprintf(stderr, "[vis] FATAL: Start failed\n"); return 1; }

    fprintf(stderr, "[vis] WASAPI loopback started\n");

    // --- Main loop ---
    initProcessor();
    float chunk[FRAME_SAMPLES];
    int chunkPos = 0;
    float bars[MAX_BAR_COUNT];
    bool wasIdle = true;

    bool isFloat = (mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    if (mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE* ext = (WAVEFORMATEXTENSIBLE*)mixFormat;
        isFloat = (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }

    auto lastSend = std::chrono::steady_clock::now();

    fprintf(stderr, "[vis] Waiting for client on ws://127.0.0.1:%d\n", WS_PORT);

    while (g_running) {
        ws.poll();

        if (!ws.hasClient()) {
            wasIdle = true;
            Sleep(50);
            continue;
        }

        if (wasIdle) {
            initProcessor();
            chunkPos = 0;
            wasIdle = false;
            lastSend = std::chrono::steady_clock::now();
            fprintf(stderr, "[vis] Client connected, streaming\n");
        }

        UINT32 packetLength = 0;
        hr = captureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr)) break;

        while (packetLength > 0) {
            BYTE* data = nullptr;
            UINT32 numFrames = 0;
            DWORD flags = 0;
            hr = captureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
            if (FAILED(hr)) break;

            float mono[4096];
            UINT32 toConvert = (numFrames > 4096) ? 4096 : numFrames;
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                memset(mono, 0, toConvert * sizeof(float));
            } else {
                toMono(data, mono, toConvert, mixFormat->nChannels,
                       mixFormat->wBitsPerSample, isFloat);
            }

            for (UINT32 i = 0; i < toConvert; i++) {
                chunk[chunkPos++] = mono[i];
                if (chunkPos >= FRAME_SAMPLES) {
                    processFrame(chunk, bars);
                    auto now = std::chrono::steady_clock::now();
                    if (ws.hasClient() && std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSend).count() >= sendIntervalMs.load()) {
                        ws.sendBinary(bars, g_barCount * sizeof(float));
                        lastSend = now;
                    }
                    chunkPos = 0;
                }
            }

            captureClient->ReleaseBuffer(numFrames);
            hr = captureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) break;
        }

        Sleep(1);
    }

    fprintf(stderr, "\n[vis] Shutting down...\n");
    audioClient->Stop();
    captureClient->Release();
    audioClient->Release();
    device->Release();
    enumerator->Release();
    CoTaskMemFree(mixFormat);
    CoUninitialize();
    ws.stop();
    return 0;
}
