// fft.h — Cava-style audio processor for the Spotify visualizer.
// Sliding-window FFT, log-frequency binning, auto-gain control,
// and gravity smoothing.  Header-only, no external dependencies.
#ifndef VIS_FFT_H
#define VIS_FFT_H

#include <cmath>
#include <cstring>
#include <algorithm>
#include "protocol.h"

// ---- Tuning constants ----
constexpr float GRAVITY_ACCEL = 0.002f;
constexpr float AGC_ATTACK    = 0.985f;
constexpr float AGC_RELEASE   = 1.015f;
constexpr float AGC_MIN       = 0.1f;
constexpr float AGC_MAX       = 80.0f;

// ---- Complex helpers ----
struct Complex { float re, im; };

static inline Complex cadd(Complex a, Complex b) { return {a.re+b.re, a.im+b.im}; }
static inline Complex csub(Complex a, Complex b) { return {a.re-b.re, a.im-b.im}; }
static inline Complex cmul(Complex a, Complex b) {
    return {a.re*b.re - a.im*b.im, a.re*b.im + a.im*b.re};
}

// ---- Bit-reversal permutation ----
static void bitReverse(Complex* buf, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { Complex t = buf[i]; buf[i] = buf[j]; buf[j] = t; }
    }
}

// ---- In-place radix-2 FFT (n must be power of 2) ----
static void fft(Complex* buf, int n) {
    bitReverse(buf, n);
    for (int len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * (float)M_PI / len;
        Complex wn = {cosf(angle), sinf(angle)};
        for (int i = 0; i < n; i += len) {
            Complex w = {1.0f, 0.0f};
            for (int j = 0; j < len / 2; j++) {
                Complex u = buf[i + j];
                Complex v = cmul(w, buf[i + j + len/2]);
                buf[i + j]         = cadd(u, v);
                buf[i + j + len/2] = csub(u, v);
                w = cmul(w, wn);
            }
        }
    }
}

// ---- Processor state ----
static float g_hanning[FFT_SIZE];
static int   g_binLo[BAR_COUNT];
static int   g_binHi[BAR_COUNT];
static float g_window[FFT_SIZE];       // sliding audio window
static float g_smoothBars[BAR_COUNT];  // gravity-smoothed output
static float g_velocity[BAR_COUNT];    // per-bar fall velocity
static float g_sensitivity;            // auto-gain multiplier
static bool  g_inited = false;

static void initProcessor() {
    for (int i = 0; i < FFT_SIZE; i++)
        g_hanning[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (FFT_SIZE - 1)));

    float logMin = log10f(FREQ_MIN);
    float logMax = log10f(FREQ_MAX);
    for (int i = 0; i < BAR_COUNT; i++) {
        float fLo = powf(10.0f, logMin + (float)i / BAR_COUNT * (logMax - logMin));
        float fHi = powf(10.0f, logMin + (float)(i + 1) / BAR_COUNT * (logMax - logMin));
        g_binLo[i] = std::max(1, (int)(fLo * FFT_SIZE / SAMPLE_RATE));
        g_binHi[i] = std::min(FFT_SIZE / 2 - 1, (int)(fHi * FFT_SIZE / SAMPLE_RATE));
        if (g_binHi[i] < g_binLo[i]) g_binHi[i] = g_binLo[i];
    }

    memset(g_window, 0, sizeof(g_window));
    memset(g_smoothBars, 0, sizeof(g_smoothBars));
    memset(g_velocity, 0, sizeof(g_velocity));
    g_sensitivity = 1.5f;
    g_inited = true;
}

// Process one frame of FRAME_SAMPLES new audio samples.
// Appends to the sliding window, runs FFT, bins, AGC, gravity.
// Output: bars[BAR_COUNT] in [0, 1].
static void processFrame(const float* newSamples, float* bars) {
    if (!g_inited) initProcessor();

    // 1. Slide window: shift out old, append new
    memmove(g_window, g_window + FRAME_SAMPLES,
            (FFT_SIZE - FRAME_SAMPLES) * sizeof(float));
    memcpy(g_window + (FFT_SIZE - FRAME_SAMPLES), newSamples,
           FRAME_SAMPLES * sizeof(float));

    // 2. Apply Hanning window -> FFT
    static Complex fftBuf[FFT_SIZE];
    for (int i = 0; i < FFT_SIZE; i++) {
        fftBuf[i].re = g_window[i] * g_hanning[i];
        fftBuf[i].im = 0.0f;
    }
    fft(fftBuf, FFT_SIZE);

    // 3. Magnitude spectrum
    static float mag[FFT_SIZE / 2];
    for (int i = 0; i < FFT_SIZE / 2; i++)
        mag[i] = sqrtf(fftBuf[i].re * fftBuf[i].re + fftBuf[i].im * fftBuf[i].im);

    // 4. Bin into bars (average magnitude per frequency range)
    float rawBars[BAR_COUNT];
    for (int b = 0; b < BAR_COUNT; b++) {
        float sum = 0.0f;
        int count = 0;
        for (int k = g_binLo[b]; k <= g_binHi[b]; k++) {
            sum += mag[k];
            count++;
        }
        rawBars[b] = count > 0 ? sum / count : 0.0f;
    }

    // 5. Normalize by FFT size and apply perceptual scaling (sqrt)
    for (int b = 0; b < BAR_COUNT; b++) {
        float norm = rawBars[b] / (FFT_SIZE * 0.5f);
        rawBars[b] = sqrtf(norm) * g_sensitivity;
    }

    // 6. Inter-bar smoothing (gentle neighbor blend, reduces noise)
    {
        float tmp[BAR_COUNT];
        tmp[0] = rawBars[0] * 0.8f + rawBars[1] * 0.2f;
        for (int b = 1; b < BAR_COUNT - 1; b++)
            tmp[b] = rawBars[b - 1] * 0.1f + rawBars[b] * 0.8f + rawBars[b + 1] * 0.1f;
        tmp[BAR_COUNT - 1] = rawBars[BAR_COUNT - 2] * 0.2f + rawBars[BAR_COUNT - 1] * 0.8f;
        memcpy(rawBars, tmp, sizeof(rawBars));
    }

    // 7. Gravity smoothing (cava-style: instant attack, accelerating fall)
    for (int b = 0; b < BAR_COUNT; b++) {
        if (rawBars[b] >= g_smoothBars[b]) {
            g_smoothBars[b] = rawBars[b];
            g_velocity[b] = 0.0f;
        } else if (g_smoothBars[b] > 0.0f) {
            g_velocity[b] += GRAVITY_ACCEL;
            g_smoothBars[b] -= g_velocity[b];
            if (g_smoothBars[b] < 0.0f) g_smoothBars[b] = 0.0f;
        }
    }

    // 8. Auto-sensitivity (AGC) — adapts to any volume level
    float peak = 0.0f;
    for (int b = 0; b < BAR_COUNT; b++)
        if (g_smoothBars[b] > peak) peak = g_smoothBars[b];

    if (peak > 1.05f) {
        g_sensitivity *= AGC_ATTACK;
    } else if (peak > 0.001f && peak < 0.3f) {
        g_sensitivity *= AGC_RELEASE;
    } else if (peak <= 0.001f) {
        g_sensitivity += (1.5f - g_sensitivity) * 0.005f;
    }
    g_sensitivity = std::max(AGC_MIN, std::min(AGC_MAX, g_sensitivity));

    // 9. Output clamped to [0, 1]
    for (int b = 0; b < BAR_COUNT; b++)
        bars[b] = std::max(0.0f, std::min(1.0f, g_smoothBars[b]));
}

#endif // VIS_FFT_H
