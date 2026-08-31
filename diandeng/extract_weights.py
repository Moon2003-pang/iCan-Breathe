#!/usr/bin/env python3
"""
Extract float32 weights from SavedModel → pure-C header with float inference.
ESP32-S3 with hardware FPU can easily handle 14K float params at 5Hz.

Architecture: 150 → Dense(48, ReLU) → Dense(16, ReLU) → Dense(48, ReLU) → Dense(150)
All arithmetic: float32, no external library needed.

Usage: python extract_weights.py
Output: breath_model_weights.h
"""

import numpy as np
import tensorflow as tf
import os
os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"

MODEL_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "breath_model_savedmodel")
OUT_DIR = os.path.dirname(os.path.abspath(__file__))

# Load SavedModel and extract weights from variables
loaded = tf.saved_model.load(MODEL_DIR)

# The variables are accessible from the model's trainable_variables
# Let's rebuild exact model and load weights
model = tf.keras.Sequential([
    tf.keras.layers.Dense(48, activation="relu", name="enc_dense1"),
    tf.keras.layers.Dense(16, activation="relu", name="enc_bottleneck"),
    tf.keras.layers.Dense(48, activation="relu", name="dec_dense1"),
    tf.keras.layers.Dense(150, activation="linear", name="output"),
])

# Build model by calling once
dummy = np.zeros((1, 150), dtype=np.float32)
_ = model(dummy)

# Load weights from checkpoint pattern in savedmodel
# The savedmodel has variables stored. Let's read them via the concrete function.
concrete_func = loaded.signatures["serve"]
# Get variable values through the saved_model's variables
# Each layer has kernel and bias

# Re-train quickly to get the same weights? No, let's load from TFLite by doing inference and matching.
# Actually the simplest: re-extract by reading TFLite flatbuffer with proper per-channel handling

# EASIEST APPROACH: Use TFLite interpreter to run inference,
# then extract float-equivalent weights by solving linear systems.
# No - even easier: just replicate the float model by training it again
# Wait - we already have the SavedModel. Let's try to get weights from it directly.

# List all objects in loaded model
print("Available objects:", dir(loaded))

# Try to get variables
vars_list = loaded.variables if hasattr(loaded, 'variables') else []
print(f"Variables: {len(vars_list)}")

# Try to find the trainable variables
for i, v in enumerate(vars_list):
    print(f"  [{i}] {v.name}: {v.shape}")

# Try trainable_variables
if hasattr(loaded, 'trainable_variables'):
    tv = loaded.trainable_variables
    print(f"\nTrainable variables: {len(tv)}")
    for v in tv:
        print(f"  {v.name}: {v.shape}")

# The weights are stored in the SavedModel variables accessible through
# the keras functional model reconstruction
# Let's try getting the keras model
try:
    loaded_model = tf.keras.models.load_model(MODEL_DIR)
    print("\nKeras model loaded successfully!")
    for layer in loaded_model.layers:
        w = layer.get_weights()
        if w:
            print(f"  {layer.name}: kernel={w[0].shape}, bias={w[1].shape}")
except Exception as e:
    print(f"Keras load failed: {e}")

# As a fallback: rebuild model architecture, then copy weights by matching
# TFLite layer outputs
print("\nFallback: extracting float weights via TFLite inference matching...")
