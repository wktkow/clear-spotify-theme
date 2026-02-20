// protocol.h — Shared constants for the Spotify visualizer audio bridge.
// Both the C++ capture daemon and the JS client must agree on these values.
#ifndef VIS_PROTOCOL_H
#define VIS_PROTOCOL_H

constexpr int    WS_PORT     = 7700;
constexpr int    BAR_COUNT   = 24;
constexpr int    FFT_SIZE    = 2048;
constexpr int    SAMPLE_RATE = 44100;
constexpr int    SEND_FPS    = 60;
constexpr float  FREQ_MIN    = 30.0f;
constexpr float  FREQ_MAX    = 14000.0f;
constexpr float  FREQ_CURVE  = 1.5f;   // >1 = more bars for low freqs

#endif // VIS_PROTOCOL_H
