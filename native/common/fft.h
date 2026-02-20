// fft.h — Audio processor for the Spotify visualizer.
// Sliding-window FFT, log-frequency binning, per-bar EQ,
// auto-sensitivity, integral smoothing, and gravity falloff.
// Closely follows cava's signal processing pipeline.
// Header-only, no external dependencies.
#ifndef VIS_FFT_H
#define VIS_FFT_H

#include <cmath>
#include <cstring>
#include <algorithm>
#include "protocol.h"

// ---- Tuning constants (matched to cava defaults) ----

// Temporal IIR smoothing factor.  Each bar's output is accumulated as:
//   out = mem * NR + raw;  mem = out;
// Steady-state gain ~ 1/(1-NR).  Higher = smoother but laggier.
// Cava default: 0.77.
constexpr float NOISE_REDUCTION = 0.77f;

// Gravity: accelerating fall when signal drops.
// Cava uses step=0.028, mod = 1.54/NR ~ 2.0 at 60fps.
constexpr float GRAVITY_STEP    = 0.028f;
constexpr float GRAVITY_MOD     = 1.54f / NOISE_REDUCTION;

// Auto-sensitivity (global gain).
// attack 0.98 per frame on overshoot, release 1.001 per frame.
// sens_init mode ramps fast (1.1x/frame) until first overshoot.
// Start at 1.0 (cava default) so bars respond immediately.
constexpr float SENS_INIT       = 1.0f;
constexpr float SENS_ATTACK     = 0.98f;
constexpr float SENS_RELEASE    = 1.001f;
constexpr float SENS_INIT_BOOST = 1.1f;
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
static float g_mem[BAR_COUNT];          // integral smoothing memory
static float g_peak[BAR_COUNT];         // gravity peak tracker
static float g_fall[BAR_COUNT];         // gravity fall velocity
static float g_prevOut[BAR_COUNT];      // previous frame output (for gravity)
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
    memset(g_prevOut, 0, sizeof(g_prevOut));
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
    float rawBars[BAR_COUNT];
    bool silence = true;
    for (int b = 0; b < BAR_COUNT; b++) {
        float sum = 0.0f;
        int count = g_binHi[b] - g_binLo[b] + 1;
        for (int k = g_binLo[b]; k <= g_binHi[b]; k++)
            sum += mag[k];
        float avg = count > 0 ? sum / count : 0.0f;

        // Normalize by FFT size, sqrt compression, per-bar EQ, global sensitivity
        float norm = avg / (FFT_SIZE * 0.5f);
        rawBars[b] = sqrtf(norm) * g_eq[b] * g_sens;

        if (rawBars[b] > 0.001f) silence = false;
    }

    // 5. Gravity + integral smoothing + clamping (cava order)
    bool overshoot = false;
    for (int b = 0; b < BAR_COUNT; b++) {
        // Gravity: accelerating fall when signal drops
        if (rawBars[b] < g_prevOut[b]) {
            rawBars[b] = g_peak[b] * (1.0f - g_fall[b] * g_fall[b] * GRAVITY_MOD);
            if (rawBars[b] < 0.0f) rawBars[b] = 0.0f;
            g_fall[b] += GRAVITY_STEP;
        } else {
            g_peak[b] = rawBars[b];
            g_fall[b] = 0.0f;
        }
        g_prevOut[b] = rawBars[b];

        // Integral smoothing (temporal IIR low-pass filter).
        // Accumulates: out = mem * NR + raw.  Steady-state gain ~ 1/(1-NR).
        // Store UNCLAMPED value in mem — this is critical for autosens stability.
        // Clamped mem causes bars to oscillate at the 1.0 boundary because the
        // system loses inertia: when signal drops just below 0.23, bars instantly
        // leave the overshoot zone, sens grows, bars overshoot again = jitter.
        // Unclamped mem provides the inertia that lets autosens converge smoothly.
        rawBars[b] = g_mem[b] * NOISE_REDUCTION + rawBars[b];
        g_mem[b] = rawBars[b];  // store UNCLAMPED (matches cava)

        // Clamp output for display only — overshoot tracking uses unclamped value
        if (rawBars[b] > 1.0f) { overshoot = true; rawBars[b] = 1.0f; }
        if (rawBars[b] < 0.0f) rawBars[b] = 0.0f;

        bars[b] = rawBars[b];
    }

    // 6. Auto-sensitivity (cava-style):
    //    overshoot -> gently reduce.  Quiet -> slowly grow.
    //    Initial mode ramps fast (1.1x/frame) until first overshoot.
    if (overshoot) {
        g_sens *= SENS_ATTACK;
        g_sensInit = false;
    } else if (!silence) {
        g_sens *= SENS_RELEASE;
        if (g_sensInit) g_sens *= SENS_INIT_BOOST;
    }
    g_sens = std::max(SENS_MIN, std::min(SENS_MAX, g_sens));
}

#endif // VIS_FFT_H
