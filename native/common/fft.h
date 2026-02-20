// fft.h — Audio processor for the Spotify visualizer.
// Sliding-window FFT, log-frequency binning, per-bar EQ,
// auto-sensitivity, asymmetric EMA smoothing, and gravity falloff.
// Closely follows cava's signal processing pipeline.
// Header-only, no external dependencies.
#ifndef VIS_FFT_H
#define VIS_FFT_H

#include <cmath>
#include <cstring>
#include <algorithm>
#include "protocol.h"

// ---- Tuning constants (matched to cava defaults) ----

// Asymmetric smoothing: instant attack (mem snaps to raw on rise),
// pure exponential decay on fall (mem *= NOISE_REDUCTION).
// This matches cava's integral filter exactly.
constexpr float NOISE_REDUCTION = 0.77f;

// Gravity: subtraction-based accelerating fall from peak.
// peak -= GRAVITY * velocity; velocity += GRAVITY;
// With GRAVITY=0.08 a bar falls from 1.0 to 0 in ~18 frames (300ms).
// This replaces the old multiplicative formula which decayed too slowly.
constexpr float GRAVITY         = 0.08f;

// Auto-sensitivity (global gain).
// attack 0.98 per frame on overshoot, release 1.001 per frame.
// sens_init mode ramps fast (1.1x/frame) until first overshoot.
// Silence is detected on raw PCM (any non-zero = not silent), matching
// cava's approach — prevents sens drift on FFT floating-point artifacts.
// sensInit boost is gated on audioMax > SENS_INIT_AUDIO_GATE to prevent
// explosive ramp on microscopic PA warmup noise, and capped at
// SENS_INIT_CAP so the worst-case lockup when real music arrives is brief.
constexpr float SENS_INIT       = 1.0f;
constexpr float SENS_ATTACK     = 0.98f;
constexpr float SENS_RELEASE    = 1.001f;
constexpr float SENS_INIT_BOOST = 1.1f;
constexpr float SENS_INIT_CAP   = 2.0f;
constexpr float SENS_INIT_AUDIO_GATE = 0.005f;
constexpr float SENS_MIN        = 0.02f;
constexpr float SENS_MAX        = 20.0f;

// Per-bar EQ: pow(freq/FREQ_MIN, EQ_POWER).
// Boosts high-frequency bars to compensate for music having more
// energy in bass.  Combined with sqrt() normalization, 0.5 produces
// nearly flat response across all bars for broadband audio.
constexpr float EQ_POWER        = 0.5f;

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
static float g_inputBuf[FFT_SIZE];      // sliding window of real audio
static float g_window[FFT_SIZE];        // Hann window (full FFT buffer)
static int   g_binLo[BAR_COUNT];        // FFT bin lower bound per bar
static int   g_binHi[BAR_COUNT];        // FFT bin upper bound per bar
static float g_eq[BAR_COUNT];           // per-bar EQ weight
static float g_mem[BAR_COUNT];          // smoothing memory
static float g_peak[BAR_COUNT];         // gravity peak tracker
static float g_fall[BAR_COUNT];         // gravity fall velocity
static float g_sens;                    // auto-sensitivity (global gain)
static bool  g_sensInit;                // fast initial ramp-up active
static bool  g_inited = false;

static void initProcessor() {
    // Hann window sized to full FFT buffer
    for (int i = 0; i < FFT_SIZE; i++)
        g_window[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (FFT_SIZE - 1)));

    // Log-spaced frequency bin cutoffs
    float logMin = log10f(FREQ_MIN);
    float logMax = log10f(FREQ_MAX);
    int loCut[BAR_COUNT + 1];
    for (int i = 0; i <= BAR_COUNT; i++) {
        float f = powf(10.0f, logMin + (float)i / BAR_COUNT * (logMax - logMin));
        loCut[i] = std::max(1, (int)roundf(f * FFT_SIZE / SAMPLE_RATE));
    }
    // Push up to guarantee each bar has at least 1 unique FFT bin (cava approach)
    for (int i = 1; i <= BAR_COUNT; i++) {
        if (loCut[i] <= loCut[i - 1])
            loCut[i] = loCut[i - 1] + 1;
    }
    for (int i = 0; i < BAR_COUNT; i++) {
        g_binLo[i] = loCut[i];
        g_binHi[i] = std::max(loCut[i], loCut[i + 1] - 1);
        g_binHi[i] = std::min(g_binHi[i], FFT_SIZE / 2 - 1);
    }

    // Per-bar EQ: boost higher frequencies to balance typical music spectrum
    for (int i = 0; i < BAR_COUNT; i++) {
        float fCenter = (float)(g_binLo[i] + g_binHi[i]) * 0.5f
                        * (float)SAMPLE_RATE / (float)FFT_SIZE;
        g_eq[i] = powf(std::max(fCenter, (float)FREQ_MIN) / (float)FREQ_MIN, EQ_POWER);
    }

    memset(g_inputBuf, 0, sizeof(g_inputBuf));
    memset(g_mem, 0, sizeof(g_mem));
    memset(g_peak, 0, sizeof(g_peak));
    memset(g_fall, 0, sizeof(g_fall));
    g_sens = SENS_INIT;
    g_sensInit = true;
    g_inited = true;
}

// Process one frame of FRAME_SAMPLES fresh audio.
// Maintains a sliding window of FFT_SIZE samples (all real audio, no zero-padding).
// Output: bars[BAR_COUNT] in [0, 1].
static void processFrame(const float* newSamples, float* bars) {
    if (!g_inited) initProcessor();

    // 1. Sliding window: shift left by FRAME_SAMPLES, append new audio.
    //    The entire buffer contains real audio — no zero-padding.
    memmove(g_inputBuf, g_inputBuf + FRAME_SAMPLES,
            (FFT_SIZE - FRAME_SAMPLES) * sizeof(float));
    memcpy(g_inputBuf + (FFT_SIZE - FRAME_SAMPLES), newSamples,
           FRAME_SAMPLES * sizeof(float));

    // 1b. Peak audio level of new chunk — gates sensInit boost so
    //     microscopic PA warmup noise doesn't trigger the fast ramp-up.
    float audioMax = 0.0f;
    for (int i = 0; i < FRAME_SAMPLES; i++) {
        float a = fabsf(newSamples[i]);
        if (a > audioMax) audioMax = a;
    }

    // 2. Hann window over full buffer -> FFT
    static Complex fftBuf[FFT_SIZE];
    for (int i = 0; i < FFT_SIZE; i++) {
        fftBuf[i].re = g_inputBuf[i] * g_window[i];
        fftBuf[i].im = 0.0f;
    }
    fft(fftBuf, FFT_SIZE);

    // 3. Magnitude spectrum
    static float mag[FFT_SIZE / 2];
    for (int i = 0; i < FFT_SIZE / 2; i++)
        mag[i] = sqrtf(fftBuf[i].re * fftBuf[i].re + fftBuf[i].im * fftBuf[i].im);

    // 4. Bin into bars: average magnitude per frequency range, normalize, EQ
    //    Silence is checked on raw PCM (audioMax == 0), matching cava's
    //    approach.  Checking post-FFT bars was wrong: floating-point FFT
    //    artifacts × sens could fool the silence gate during true silence.
    bool silence = (audioMax == 0.0f);
    float rawBars[BAR_COUNT];
    for (int b = 0; b < BAR_COUNT; b++) {
        float sum = 0.0f;
        int count = g_binHi[b] - g_binLo[b] + 1;
        for (int k = g_binLo[b]; k <= g_binHi[b]; k++)
            sum += mag[k];
        float avg = count > 0 ? sum / count : 0.0f;

        // Normalize by FFT size, sqrt compression, per-bar EQ, global sensitivity
        float norm = avg / (FFT_SIZE * 0.5f);
        rawBars[b] = sqrtf(norm) * g_eq[b] * g_sens;
    }

    // 5. Smoothing + gravity (matches cava's actual pipeline).
    //    Order: (a) instant-attack / exponential-decay smoothing on raw,
    //           (b) gravity on the smoothed output.
    //    The old code applied gravity BEFORE EMA, causing double-smoothing
    //    that kept bars visually stuck at peak for 400ms.
    bool overshoot = false;
    for (int b = 0; b < BAR_COUNT; b++) {
        float raw = rawBars[b];

        // (a) Cava-style smoothing: snap up instantly, decay exponentially.
        if (raw > g_mem[b]) {
            g_mem[b] = raw;
        } else {
            g_mem[b] *= NOISE_REDUCTION;
        }

        // Overshoot detection for auto-sensitivity.
        if (g_mem[b] > 1.0f) overshoot = true;

        // (b) Gravity: accelerating fall from peak (subtraction form).
        //     peak -= GRAVITY * velocity; velocity += GRAVITY;
        //     Gives a smooth parabolic fall over ~300ms.
        if (g_mem[b] >= g_peak[b]) {
            g_peak[b] = g_mem[b];
            g_fall[b] = 0.0f;
        } else {
            g_peak[b] -= GRAVITY * g_fall[b];
            g_fall[b] += GRAVITY;
            // Peak can't go below the smoothed signal
            if (g_peak[b] < g_mem[b]) g_peak[b] = g_mem[b];
            if (g_peak[b] < 0.0f) g_peak[b] = 0.0f;
        }

        bars[b] = std::min(g_peak[b], 1.0f);
    }

    // 6. Auto-sensitivity (cava-style):
    //    overshoot -> gently reduce.  Quiet -> slowly grow.
    //    sensInit ramps fast until first overshoot OR sens exceeds cap.
    //    sensInit boost only fires when audioMax > SENS_INIT_AUDIO_GATE
    //    so PA warmup noise (amplitude ~1e-6) doesn't trigger the ramp.
    if (overshoot) {
        g_sens *= SENS_ATTACK;
        g_sensInit = false;
    } else if (!silence) {
        g_sens *= SENS_RELEASE;
        if (g_sensInit) {
            if (audioMax > SENS_INIT_AUDIO_GATE)
                g_sens *= SENS_INIT_BOOST;
            if (g_sens > SENS_INIT_CAP)
                g_sensInit = false;
        }
    }
    g_sens = std::max(SENS_MIN, std::min(SENS_MAX, g_sens));
}

#endif // VIS_FFT_H
