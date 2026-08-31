/**
 * Breath Autoencoder — Pure float32 Inference (NO external dependencies)
 *
 * Architecture: 150 → Dense(48, ReLU) → Dense(16, ReLU) → Dense(48, ReLU) → Dense(150)
 * Weights embedded at compile time from breath_model_weights.h
 * ESP32-S3 handles float32 natively with hardware FPU.
 *
 * Inference cost per run: ~14K params × 2 flops = ~28K flops → < 1ms @ 240MHz
 */

#include "breath_ae.h"
#include "breath_model_weights.h"
#include "breath_model_config.h"

#include <math.h>
#include <string.h>

// ======================== Dense Layer ========================

/**
 * y = activation(W·x + b)
 * W: [output_dim × input_dim] row-major (as exported by Keras)
 * x: [input_dim]
 * b: [output_dim]
 * y: [output_dim]
 */
static void denseLayer(
    const float *x, const float *W, const float *b,
    float *y, int input_dim, int output_dim, bool relu)
{
    for (int j = 0; j < output_dim; ++j) {
        float accum = b[j];
        const float *W_row = W + j * input_dim;
        for (int i = 0; i < input_dim; ++i) {
            accum += W_row[i] * x[i];
        }
        if (relu && accum < 0.0f) {
            accum = 0.0f;
        }
        y[j] = accum;
    }
}

// ======================== Public API ========================

bool breathAeInit()
{
    return true;  // All weights are compile-time constants
}

float breathAePredict(const float *window150)
{
    float h1[48];
    denseLayer(window150, k_enc_dense1, b_enc_dense1, h1, 150, 48, true);

    float h2[16];
    denseLayer(h1, k_enc_bottleneck, b_enc_bottleneck, h2, 48, 16, true);

    float h3[48];
    denseLayer(h2, k_dec_dense1, b_dec_dense1, h3, 16, 48, true);

    float y[150];
    denseLayer(h3, k_output, b_output, y, 48, 150, false);

    // Reconstruction MSE
    float mse = 0.0f;
    for (int i = 0; i < 150; ++i) {
        float diff = window150[i] - y[i];
        mse += diff * diff;
    }
    mse /= 150.0f;

    // Normalized anomaly score
    return mse / BREATH_MEDIAN_MSE;
}

void breathNormalizeWindow(float *window150)
{
    float sum = 0.0f;
    for (int i = 0; i < BREATH_WINDOW_SIZE; ++i) {
        sum += window150[i];
    }
    float mean = sum / (float)BREATH_WINDOW_SIZE;

    float var = 0.0f;
    for (int i = 0; i < BREATH_WINDOW_SIZE; ++i) {
        float diff = window150[i] - mean;
        var += diff * diff;
    }
    var /= (float)BREATH_WINDOW_SIZE;
    float std = sqrt(var) + 1e-8f;

    for (int i = 0; i < BREATH_WINDOW_SIZE; ++i) {
        window150[i] = (window150[i] - mean) / std;
    }
}

// ======================== Breathing Rate Detection ========================

float breathDetectRate(const float *rawWindow150)
{
    // Step 1: Detrend with moving average (window ~30 samples = 6s)
    const int maWin = 30;
    float detrended[BREATH_WINDOW_SIZE];
    for (int i = 0; i < BREATH_WINDOW_SIZE; ++i) {
        int wStart = i - maWin / 2;
        if (wStart < 0) wStart = 0;
        int wEnd = i + maWin / 2;
        if (wEnd > BREATH_WINDOW_SIZE) wEnd = BREATH_WINDOW_SIZE;
        float sum = 0.0f;
        for (int j = wStart; j < wEnd; ++j) sum += rawWindow150[j];
        float ma = sum / (float)(wEnd - wStart);
        detrended[i] = rawWindow150[i] - ma;
    }

    // Step 2: Find upward zero-crossings
    int crossIdx[40];  // max ~30 breaths in 30s window
    int crossCount = 0;
    for (int i = 1; i < BREATH_WINDOW_SIZE; ++i) {
        if (detrended[i - 1] <= 0.0f && detrended[i] > 0.0f) {
            if (crossCount < 40) {
                crossIdx[crossCount++] = i;
            }
        }
    }

    if (crossCount < 2) return 0.0f;  // not enough cycles

    // Step 3: Breathing rate from first to last crossing (more stable)
    float durationSec = (float)(crossIdx[crossCount - 1] - crossIdx[0]) / 5.0f;  // 5Hz
    int cycles = crossCount - 1;
    if (durationSec <= 0.0f) return 0.0f;
    float rate = (float)cycles / durationSec * 60.0f;

    return rate;
}
