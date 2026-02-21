// fft.h — Audio processor for the Spotify visualizer.
// Sliding-window FFT, log-frequency binning, per-bar EQ,
// auto-sensitivity, asymmetric EMA smoothing, and gravity falloff.
// Uses simple gain=1.0 EMA instead of cava's integral accumulator
// to guarantee bars cannot lock up at max values.
// Header-only, no external dependencies.
#ifndef VIS_FFT_H
#define VIS_FFT_H

#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include "protocol.h"

// ---- Tuning constants ----

// Asymmetric EMA smoothing (gain = 1.0, no amplification).
// On rise: fast attack — 60% weight on new value gives ~4 frame convergence.
// On fall: slow decay — 15% weight gives smooth ~4.3 frame half-life.
// Unlike cava's integral accumulator (gain=4.35x), this CAN'T exceed the
// input value, so bars never get stuck at 1.0 waiting for mem to drain.
constexpr float SMOOTH_ATTACK = 0.4f;   // EMA alpha when rising
constexpr float SMOOTH_DECAY  = 0.85f;  // EMA alpha when falling

// Gravity: constant-acceleration fall from peak position.
// velocity += GRAVITY each frame; peak -= velocity each frame.
// From peak=1.0 to 0 takes ~22 frames (367ms at 60fps).
constexpr float GRAVITY       = 0.004f;

// Silence threshold: float32 PA captures microscopic warmup noise
// that would be zero in cava's S16LE format.  Anything below this
// is treated as silence to prevent auto-sensitivity drift.
constexpr float SILENCE_THRESHOLD = 1e-4f;

// Auto-sensitivity (global gain).
// Attack 0.85 per frame on overshoot (fast convergence — 7 frames
// from 3.0 to 1.0 instead of 148 frames with cava's 0.98).
// Release 1.002 per frame when quiet.
// sensInit ramps at 1.1x/frame until first overshoot.
constexpr float SENS_INIT       = 1.0f;
constexpr float SENS_ATTACK     = 0.85f;
constexpr float SENS_RELEASE    = 1.002f;
constexpr float SENS_INIT_BOOST = 1.1f;
constexpr float SENS_INIT_CAP   = 5.0f;
constexpr float SENS_MIN        = 0.02f;
constexpr float SENS_MAX        = 5.0f;

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
static float g_mem[BAR_COUNT];          // EMA smoothing memory
static float g_peak[BAR_COUNT];         // gravity peak tracker
static float g_fall[BAR_COUNT];         // gravity fall velocity
static float g_sens;                    // auto-sensitivity (global gain)
static bool  g_sensInit;                // fast initial ramp-up active
static bool  g_inited = false;
static int   g_dbgFrame = 0;            // debug frame counter

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
    g_dbgFrame = 0;
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
    //    Silence is checked on raw PCM level vs threshold (matching cava's
    //    S16LE behavior where sub-16bit noise truncates to zero).
    bool silence = (audioMax < SILENCE_THRESHOLD);
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

    // 5. Asymmetric EMA + gravity (replaces cava's integral accumulator).
    //    The integral accumulated input (gain ~4.35x), causing bars to stay
    //    clamped at 1.0 for hundreds of milliseconds.  This EMA has gain=1.0
    //    so output never exceeds the input value — provably no lockup.
    bool overshoot = false;
    for (int b = 0; b < BAR_COUNT; b++) {
        float raw = rawBars[b];

        // (a) Asymmetric EMA: fast attack, slow decay (gain = 1.0).
        //     Attack: 60% new value → ~4 frames to reach 95% of step input.
        //     Decay:  15% new value → smooth exponential fall, half-life ~4 frames.
        if (raw > g_mem[b]) {
            g_mem[b] = g_mem[b] * SMOOTH_ATTACK + raw * (1.0f - SMOOTH_ATTACK);
        } else {
            g_mem[b] = g_mem[b] * SMOOTH_DECAY + raw * (1.0f - SMOOTH_DECAY);
        }

        // (b) Gravity: constant-acceleration fall from peak.
        //     Gives smooth parabolic descent (~367ms from 1.0 to 0).
        //     Peak tracks the EMA output upward instantly.
        if (g_mem[b] >= g_peak[b]) {
            g_peak[b] = g_mem[b];
            g_fall[b] = 0.0f;
        } else {
            g_fall[b] += GRAVITY;
            g_peak[b] -= g_fall[b];
            if (g_peak[b] < g_mem[b]) g_peak[b] = g_mem[b];
            if (g_peak[b] < 0.0f) g_peak[b] = 0.0f;
        }

        // Overshoot detection for auto-sensitivity.
        if (g_peak[b] > 1.0f) overshoot = true;

        bars[b] = std::min(g_peak[b], 1.0f);
    }

    // 6. Auto-sensitivity:
    //    Overshoot → fast reduction (0.85x, converges in ~7 frames).
    //    Non-silent → slow growth (1.002x).
    //    sensInit → fast ramp (1.1x) until first overshoot or cap.
    if (overshoot) {
        g_sens *= SENS_ATTACK;
        g_sensInit = false;
    } else if (!silence) {
        g_sens *= SENS_RELEASE;
        if (g_sensInit) {
            g_sens *= SENS_INIT_BOOST;
            if (g_sens > SENS_INIT_CAP)
                g_sensInit = false;
        }
    }
    g_sens = std::max(SENS_MIN, std::min(SENS_MAX, g_sens));

    // Debug: log every 60 frames (1 second) so we can verify data flow
    if (++g_dbgFrame % 60 == 0) {
        float maxBar = 0.0f;
        for (int b = 0; b < BAR_COUNT; b++)
            if (bars[b] > maxBar) maxBar = bars[b];
        fprintf(stderr, "[vis-dbg] f=%d sens=%.3f maxBar=%.3f audioMax=%.6f bars[0]=%.3f [35]=%.3f [69]=%.3f\n",
                g_dbgFrame, g_sens, maxBar, audioMax, bars[0], bars[35], bars[69]);
    }
}

#endif // VIS_FFT_H
