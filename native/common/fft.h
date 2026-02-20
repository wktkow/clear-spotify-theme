// fft.h — Audio processor for the Spotify visualizer.
// Fresh-frame FFT (zero-padded), log-frequency binning, auto-gain control,
// and gravity smoothing.  Header-only, no external dependencies.
#ifndef VIS_FFT_H
#define VIS_FFT_H

#include <cmath>
#include <cstring>
#include <algorithm>
#include "protocol.h"

// ---- Tuning constants ----
constexpr float GRAVITY      = 0.04f;   // per-frame fall acceleration
constexpr float SENSITIVITY  = 8.0f;    // base amplification
constexpr float AGC_ATTACK   = 0.85f;   // shrink gain when too loud (fast)
constexpr float AGC_RELEASE  = 1.002f;  // grow gain when too quiet (slow)
constexpr float AGC_MIN      = 0.5f;
constexpr float AGC_MAX      = 15.0f;

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
static float g_frameWin[FRAME_SAMPLES]; // Hanning window for one frame of audio
static int   g_binLo[BAR_COUNT];
static int   g_binHi[BAR_COUNT];
static float g_smoothBars[BAR_COUNT];   // gravity-smoothed output
static float g_velocity[BAR_COUNT];     // per-bar fall velocity
static float g_gain;                    // auto-gain multiplier
static bool  g_inited = false;

static void initProcessor() {
    // Hanning window sized to FRAME_SAMPLES (not FFT_SIZE!)
    for (int i = 0; i < FRAME_SAMPLES; i++)
        g_frameWin[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (FRAME_SAMPLES - 1)));

    // Log-spaced frequency bin edges
    float logMin = log10f(FREQ_MIN);
    float logMax = log10f(FREQ_MAX);
    for (int i = 0; i < BAR_COUNT; i++) {
        float fLo = powf(10.0f, logMin + (float)i / BAR_COUNT * (logMax - logMin));
        float fHi = powf(10.0f, logMin + (float)(i + 1) / BAR_COUNT * (logMax - logMin));
        g_binLo[i] = std::max(1, (int)(fLo * FFT_SIZE / SAMPLE_RATE));
        g_binHi[i] = std::min(FFT_SIZE / 2 - 1, (int)(fHi * FFT_SIZE / SAMPLE_RATE));
        if (g_binHi[i] < g_binLo[i]) g_binHi[i] = g_binLo[i];
    }

    memset(g_smoothBars, 0, sizeof(g_smoothBars));
    memset(g_velocity, 0, sizeof(g_velocity));
    g_gain = SENSITIVITY;
    g_inited = true;
}

// Process one frame of FRAME_SAMPLES fresh audio.
// Window + zero-pad to FFT_SIZE → each frame is 100% new data.
// Output: bars[BAR_COUNT] in [0, 1].
static void processFrame(const float* newSamples, float* bars) {
    if (!g_inited) initProcessor();

    // 1. Window fresh samples, zero-pad rest → FFT input is fully fresh
    static Complex fftBuf[FFT_SIZE];
    for (int i = 0; i < FRAME_SAMPLES; i++) {
        fftBuf[i].re = newSamples[i] * g_frameWin[i];
        fftBuf[i].im = 0.0f;
    }
    for (int i = FRAME_SAMPLES; i < FFT_SIZE; i++) {
        fftBuf[i].re = 0.0f;
        fftBuf[i].im = 0.0f;
    }
    fft(fftBuf, FFT_SIZE);

    // 2. Magnitude spectrum
    static float mag[FFT_SIZE / 2];
    for (int i = 0; i < FFT_SIZE / 2; i++)
        mag[i] = sqrtf(fftBuf[i].re * fftBuf[i].re + fftBuf[i].im * fftBuf[i].im);

    // 3. Bin into bars (average magnitude per frequency range)
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

    // 4. Normalize and apply gain + sqrt perceptual scaling
    float rawPeak = 0.0f;
    for (int b = 0; b < BAR_COUNT; b++) {
        // Normalize by FRAME_SAMPLES (the actual signal length, not FFT_SIZE)
        float norm = rawBars[b] / (FRAME_SAMPLES * 0.5f);
        rawBars[b] = sqrtf(norm) * g_gain;
        if (rawBars[b] > rawPeak) rawPeak = rawBars[b];
    }

    // 5. AGC on raw signal — adapts to any volume level
    if (rawPeak > 1.0f) {
        g_gain *= AGC_ATTACK;
    } else if (rawPeak > 0.001f && rawPeak < 0.4f) {
        g_gain *= AGC_RELEASE;
    } else if (rawPeak <= 0.001f) {
        g_gain += (SENSITIVITY - g_gain) * 0.01f;
    }
    g_gain = std::max(AGC_MIN, std::min(AGC_MAX, g_gain));

    // 6. Clamp to [0, 1]
    for (int b = 0; b < BAR_COUNT; b++)
        rawBars[b] = std::max(0.0f, std::min(1.0f, rawBars[b]));

    // 7. Inter-bar smoothing (reduces single-bin noise spikes)
    {
        float tmp[BAR_COUNT];
        tmp[0] = rawBars[0] * 0.7f + rawBars[1] * 0.3f;
        for (int b = 1; b < BAR_COUNT - 1; b++)
            tmp[b] = rawBars[b - 1] * 0.15f + rawBars[b] * 0.7f + rawBars[b + 1] * 0.15f;
        tmp[BAR_COUNT - 1] = rawBars[BAR_COUNT - 2] * 0.3f + rawBars[BAR_COUNT - 1] * 0.7f;
        memcpy(rawBars, tmp, sizeof(rawBars));
    }

    // 8. Gravity: instant attack, accelerating fall
    for (int b = 0; b < BAR_COUNT; b++) {
        if (rawBars[b] >= g_smoothBars[b]) {
            g_smoothBars[b] = rawBars[b];
            g_velocity[b] = 0.0f;
        } else {
            g_velocity[b] += GRAVITY;
            g_smoothBars[b] -= g_velocity[b];
            if (g_smoothBars[b] < 0.0f) g_smoothBars[b] = 0.0f;
        }
    }

    // 9. Output
    for (int b = 0; b < BAR_COUNT; b++)
        bars[b] = g_smoothBars[b];
}

#endif // VIS_FFT_H
