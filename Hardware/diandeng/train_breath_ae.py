#!/usr/bin/env python3
"""
Breathing Autoencoder Trainer
==============================
Trains a lightweight Dense autoencoder on normal breathing data for
edge-AI anomaly detection. Exports int8-quantized TFLite model + C header
for ESP32-S3 TFLite Micro deployment.

Model: 150 → Dense(48, ReLU) → Dense(16, ReLU) → Dense(48, ReLU) → Dense(150)
Input:  150-point z-score normalized resistance window (30s @ 5Hz)
Output: Reconstruction MSE → anomaly score

Usage: python train_breath_ae.py
"""

import csv
import os
import sys
import numpy as np

os.environ["TF_CPP_MIN_LOG_LEVEL"] = "2"
import tensorflow as tf

# ======================== Config ========================

WINDOW_SIZE = 150       # 30s @ 5Hz
STRIDE = 1              # sliding window stride
AUG_FACTOR = 8          # augmentation multiplier
BATCH_SIZE = 64
EPOCHS = 100
LEARNING_RATE = 0.001
ANOMALY_THRESHOLD_MULT = 3.0  # threshold = median_MSE * this

DATA_DIR = os.path.dirname(os.path.abspath(__file__))
BASE_DIR = os.path.dirname(os.path.dirname(os.path.dirname(DATA_DIR)))
DATA_FILES = [
    os.path.join(BASE_DIR, "data", "huxi1csv.csv"),
    os.path.join(BASE_DIR, "data", "huxi2.csv"),
]

OUTPUT_DIR = DATA_DIR  # save .tflite and .h alongside this script

# ======================== Load Data ========================

def load_csv(path):
    resistances, timestamps = [], []
    with open(path, "r", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            resistances.append(float(row["resistanceOhm"]))
            timestamps.append(int(row["timestamp"]))
    return np.array(resistances, dtype=np.float32), np.array(timestamps)

print("=== Loading Training Data ===")
all_r = []
for path in DATA_FILES:
    r, ts = load_csv(path)
    print(f"  {os.path.basename(path)}: {len(r)} samples, {(ts[-1]-ts[0])/1000:.1f}s, "
          f"R=[{r.min():.1f}, {r.max():.1f}], mean={r.mean():.1f}")
    all_r.append(r)

# Concatenate all datasets
r_all = np.concatenate(all_r)
print(f"  Total: {len(r_all)} samples")

# ======================== Create Windows ========================

def extract_windows(signal, window, stride):
    n = (len(signal) - window) // stride + 1
    windows = np.zeros((n, window), dtype=np.float32)
    for i in range(n):
        start = i * stride
        windows[i] = signal[start : start + window]
    return windows

all_windows = []
for r in all_r:
    w = extract_windows(r, WINDOW_SIZE, STRIDE)
    all_windows.append(w)
windows = np.concatenate(all_windows)
print(f"  Extracted {len(windows)} windows ({WINDOW_SIZE}pt, stride={STRIDE})")

# ======================== Normalize ========================

def zscore_normalize(batch):
    """Per-window z-score normalization."""
    mean = batch.mean(axis=1, keepdims=True)
    std = batch.std(axis=1, keepdims=True) + 1e-8
    return (batch - mean) / std, mean, std

windows_norm, _, _ = zscore_normalize(windows)
print(f"  Normalized: mean≈{windows_norm.mean():.4f}, std≈{windows_norm.std():.4f}")

# ======================== Augmentation ========================

def augment(windows, factor):
    """Generate augmented training samples with noise + scaling + jitter."""
    n = len(windows)
    augmented = np.tile(windows, (factor, 1))
    rng = np.random.default_rng(42)

    for i in range(1, factor):
        block = augmented[i * n : (i + 1) * n]
        # Gaussian noise
        noise = rng.normal(0, 0.03, block.shape).astype(np.float32)
        block += noise
        # Random amplitude scaling
        scale = rng.uniform(0.85, 1.15, (block.shape[0], 1)).astype(np.float32)
        block *= scale
        # Random time shift (pad edges)
        shift = rng.integers(-3, 4, block.shape[0])
        for j, s in enumerate(shift):
            if s > 0:
                block[j, s:] = block[j, :-s]
            elif s < 0:
                block[j, :s] = block[j, -s:]

    return augmented.astype(np.float32)

all_data = augment(windows_norm, AUG_FACTOR)
# Re-normalize after augmentation
all_data, _, _ = zscore_normalize(all_data)
print(f"  Augmented: {len(all_data)} samples ({len(windows)} orig x {AUG_FACTOR})")

# ======================== Train/Val Split ========================

n_total = len(all_data)
indices = np.random.default_rng(123).permutation(n_total)
split = int(n_total * 0.8)
train_data = all_data[indices[:split]]
val_data = all_data[indices[split:]]
print(f"  Train: {len(train_data)}, Val: {len(val_data)}")

# ======================== Build Model ========================

model = tf.keras.Sequential([
    tf.keras.layers.Input(shape=(WINDOW_SIZE,), name="input"),
    tf.keras.layers.Dense(48, activation="relu", name="enc_dense1"),
    tf.keras.layers.Dense(16, activation="relu", name="enc_bottleneck"),
    tf.keras.layers.Dense(48, activation="relu", name="dec_dense1"),
    tf.keras.layers.Dense(WINDOW_SIZE, activation="linear", name="output"),
], name="breath_autoencoder")

model.compile(optimizer=tf.keras.optimizers.Adam(LEARNING_RATE),
              loss="mse")
model.summary()

# ======================== Train ========================

early_stop = tf.keras.callbacks.EarlyStopping(
    monitor="val_loss", patience=15, restore_best_weights=True, verbose=1)
reduce_lr = tf.keras.callbacks.ReduceLROnPlateau(
    monitor="val_loss", factor=0.5, patience=5, min_lr=1e-5, verbose=1)

history = model.fit(
    train_data, train_data,
    batch_size=BATCH_SIZE,
    epochs=EPOCHS,
    validation_data=(val_data, val_data),
    callbacks=[early_stop, reduce_lr],
    verbose=1)

# ======================== Compute Threshold ========================

train_pred = model.predict(train_data, verbose=0)
train_mse = np.mean((train_data - train_pred) ** 2, axis=1)
median_mse = np.median(train_mse)
threshold = median_mse * ANOMALY_THRESHOLD_MULT
percentile_99 = np.percentile(train_mse, 99)

print(f"\n=== Anomaly Threshold ===")
print(f"  Median train MSE: {median_mse:.6f}")
print(f"  Threshold (x{ANOMALY_THRESHOLD_MULT}): {threshold:.6f}")
print(f"  99th percentile: {percentile_99:.6f}")

# ======================== Quick Validation ========================

# Test: normal windows should have low anomaly score
val_pred = model.predict(val_data, verbose=0)
val_mse = np.mean((val_data - val_pred) ** 2, axis=1)
val_scores = val_mse / median_mse
print(f"\n=== Validation ===")
print(f"  Normal windows: mean_score={val_scores.mean():.2f}, "
      f"flagged={np.sum(val_scores > ANOMALY_THRESHOLD_MULT)}/{len(val_scores)}")

# Test: random noise should have high score
noise = np.random.default_rng(77).normal(0, 1, (200, WINDOW_SIZE)).astype(np.float32)
noise_pred = model.predict(noise, verbose=0)
noise_mse = np.mean((noise - noise_pred) ** 2, axis=1)
noise_scores = noise_mse / median_mse
print(f"  Random noise:  mean_score={noise_scores.mean():.2f}, "
      f"flagged={np.sum(noise_scores > ANOMALY_THRESHOLD_MULT)}/{len(noise_scores)}")

# Test: flat line should have high score
flat = np.zeros((50, WINDOW_SIZE), dtype=np.float32)
flat_pred = model.predict(flat, verbose=0)
flat_mse = np.mean((flat - flat_pred) ** 2, axis=1)
flat_scores = flat_mse / median_mse
print(f"  Flat signal:   mean_score={flat_scores.mean():.2f}, "
      f"flagged={np.sum(flat_scores > ANOMALY_THRESHOLD_MULT)}/{len(flat_scores)}")

# ======================== Export for TFLite Conversion ========================

# TF 2.18+ requires model.export() for SavedModel format
export_path = os.path.join(OUTPUT_DIR, "breath_model_savedmodel")
model.export(export_path)
print(f"\nExported SavedModel to: {export_path}")

# ======================== TFLite Conversion ========================

def representative_dataset():
    """Yield representative samples for int8 calibration."""
    for i in range(0, min(500, len(train_data)), 10):
        yield [train_data[i:i+1].astype(np.float32)]

converter = tf.lite.TFLiteConverter.from_saved_model(export_path)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = representative_dataset
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.float32   # keep float in/out
converter.inference_output_type = tf.float32

tflite_model = converter.convert()
tflite_path = os.path.join(OUTPUT_DIR, "breath_model.tflite")
with open(tflite_path, "wb") as f:
    f.write(tflite_model)
print(f"TFLite model: {tflite_path}  ({len(tflite_model)} bytes)")

# ======================== Verify TFLite ========================

interpreter = tf.lite.Interpreter(model_content=tflite_model)
interpreter.allocate_tensors()
input_details = interpreter.get_input_details()
output_details = interpreter.get_output_details()
print(f"  Input:  {input_details[0]['shape']} {input_details[0]['dtype']}")
print(f"  Output: {output_details[0]['shape']} {output_details[0]['dtype']}")

# Quick inference test
test_in = train_data[0:1]
interpreter.set_tensor(input_details[0]["index"], test_in)
interpreter.invoke()
test_out = interpreter.get_tensor(output_details[0]["index"])
tflite_mse = np.mean((test_in - test_out) ** 2)
print(f"  Test MSE (TF): {train_mse[0]:.6f}")
print(f"  Test MSE (TFL): {tflite_mse:.6f}")

# ======================== Export C Header ========================

def export_c_header(model_bytes, out_path):
    """Convert TFLite model bytes to a C array header."""
    hdr = f"""// Auto-generated by train_breath_ae.py
// DO NOT EDIT
// Model: Dense Autoencoder (150→48→16→48→150)
// Size: {len(model_bytes)} bytes, int8 quantized

#ifndef BREATH_MODEL_H
#define BREATH_MODEL_H

#ifdef __cplusplus
extern "C" {{
#endif

const unsigned char breath_model_data[] = {{
"""
    # Write hex bytes, 12 per line
    for i in range(0, len(model_bytes), 12):
        chunk = model_bytes[i : i + 12]
        line = "  " + ", ".join(f"0x{b:02X}" for b in chunk) + ","
        hdr += line + "\n"

    hdr += f"""}};
const unsigned int breath_model_len = {len(model_bytes)};

#ifdef __cplusplus
}}
#endif

#endif // BREATH_MODEL_H
"""
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(hdr)
    print(f"C header: {out_path}  ({len(model_bytes)} bytes)")

hdr_path = os.path.join(OUTPUT_DIR, "breath_model.h")
export_c_header(tflite_model, hdr_path)

# ======================== Export Config ========================

config_path = os.path.join(OUTPUT_DIR, "breath_model_config.h")
config = f"""// Auto-generated by train_breath_ae.py
// DO NOT EDIT

#ifndef BREATH_MODEL_CONFIG_H
#define BREATH_MODEL_CONFIG_H

#define BREATH_WINDOW_SIZE    {WINDOW_SIZE}
#define BREATH_MODEL_LEN      {len(tflite_model)}
#define BREATH_ANOMALY_THRESHOLD  {ANOMALY_THRESHOLD_MULT}f
#define BREATH_MEDIAN_MSE     {median_mse:.8f}f

#endif // BREATH_MODEL_CONFIG_H
"""
with open(config_path, "w", encoding="utf-8") as f:
    f.write(config)
print(f"Config header: {config_path}")

# ======================== Export Float32 Weights for Pure-C Inference ========================

def write_float_array(f, name, data):
    flat = data.flatten()
    f.write(f"static const float {name}[{len(flat)}] = {{\n  ")
    for i, v in enumerate(flat):
        f.write(f"{v: .8f}f")
        if i < len(flat) - 1:
            f.write(", ")
        if (i + 1) % 6 == 0 and i < len(flat) - 1:
            f.write("\n  ")
    f.write("\n};\n\n")

weights_path = os.path.join(OUTPUT_DIR, "breath_model_weights.h")
with open(weights_path, "w", encoding="utf-8") as f:
    f.write("""// Auto-generated by train_breath_ae.py — DO NOT EDIT
// Float32 weights for pure-C dense autoencoder inference.
// NO TFLite Micro dependency needed.
// Architecture: 150→48(ReLU)→16(ReLU)→48(ReLU)→150(linear)
// Layers: [enc_dense1, enc_bottleneck, dec_dense1, output]

#ifndef BREATH_MODEL_WEIGHTS_H
#define BREATH_MODEL_WEIGHTS_H

""")
    for layer in model.layers:
        if not layer.get_weights():
            continue
        kernel, bias = layer.get_weights()
        name = layer.name
        write_float_array(f, f"k_{name}", kernel)
        write_float_array(f, f"b_{name}", bias)
        f.write(f"// {name}: input_dim={kernel.shape[0]}, output_dim={kernel.shape[1]}\n\n")

    f.write("#endif // BREATH_MODEL_WEIGHTS_H\n")

print(f"Float32 weights: {weights_path}")

print("\n=== Done ===")
print(f"Files generated in {OUTPUT_DIR}:")
print(f"  breath_model.h         — C array (TFLite flatbuffer)")
print(f"  breath_model.tflite    — TFLite model")
print(f"  breath_model_config.h  — window size + threshold constants")
print(f"  breath_model_weights.h — Float32 weights (pure-C, no TFLite deps)")
print(f"\nNext: copy breath_model_weights.h and breath_model_config.h to Arduino sketch folder")
