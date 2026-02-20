// protocol.h — Shared constants for the Spotify visualizer audio bridge.
// Both the C++ capture daemon and the JS client must agree on these values.
#ifndef VIS_PROTOCOL_H
#define VIS_PROTOCOL_H

constexpr int    WS_PORT       = 7700;
constexpr int    BAR_COUNT     = 70;
constexpr int    FFT_SIZE      = 4096;
constexpr int    SAMPLE_RATE   = 44100;
constexpr int    SEND_FPS      = 60;
constexpr int    FRAME_SAMPLES = SAMPLE_RATE / SEND_FPS;  // 735
constexpr float  FREQ_MIN      = 50.0f;
constexpr float  FREQ_MAX      = 16000.0f;

#endif // VIS_PROTOCOL_H
