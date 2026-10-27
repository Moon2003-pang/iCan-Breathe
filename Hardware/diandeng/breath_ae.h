/**
 * Breath Autoencoder — Pure float32 Inference (NO external dependencies)
 *
 * Runs a lightweight Dense autoencoder (150→48→16→48→150) to detect
 * abnormal breathing via reconstruction error. Only normal breathing
 * data was used during training, so anomalies produce high MSE.
 *
 * Usage:
 *   1. Call breathAeInit() once during setup().
 *   2. Call breathAePredict(normalizedWindow150) every 6s (30 samples).
 *   3. Returns anomaly score: < BREATH_ANOMALY_THRESHOLD = normal
 */

#ifndef BREATH_AE_H
#define BREATH_AE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Always returns true (weights are compile-time constants, no init needed). */
bool breathAeInit();

/**
 * Run inference on a 150-point Z-SCORE NORMALIZED resistance window.
 * @param window150  150 float values (z-score normalized).
 * @return           Anomaly score = reconstruction MSE / training median MSE.
 *                   < BREATH_ANOMALY_THRESHOLD (3.0) → normal
 *                   >= BREATH_ANOMALY_THRESHOLD → anomaly
 */
float breathAePredict(const float *window150);

/**
 * Z-score normalize a raw resistance window in-place.
 * @param window150  [in/out] 150 raw resistance values.
 */
void breathNormalizeWindow(float *window150);

/**
 * Detect breathing rate from raw resistance window using zero-crossing.
 * @param rawWindow150  150 raw resistance values.
 * @return              Breathing rate in breaths per minute, or 0 if uncertain.
 */
float breathDetectRate(const float *rawWindow150);

#ifdef __cplusplus
}
#endif

#endif // BREATH_AE_H
