"""
generate_test_data.py

Generates X.csv and labels.csv compatible with the NN HLS accelerator.

Usage (use --w_hid and --w_out to set weight dir):
    python generateTestData.py --samples 64
    python generateTestData.py --samples 128 --seed 42
    python generateTestData.py --samples 200 --out_dir ./test_data

All values are in 0.8 unsigned fixed-point format (integers 0-255).
Labels are derived by running the same fixed-point inference as the HLS DUT,
using the weights from w_hid.csv and w_out.csv in the current directory.
"""

import argparse
import os
import random
import csv

# ── Network dimensions (must match HLS defines) ──────────────────────────────
NUM_INPUTS  = 7
NUM_HIDDEN  = 2
NUM_OUTPUTS = 1
WH_ROWS     = NUM_INPUTS + 1   # 8  (row 0 = bias)
WH_COLS     = NUM_HIDDEN       # 2
WO_ROWS     = NUM_HIDDEN + 1   # 3  (row 0 = bias)
WO_COLS     = NUM_OUTPUTS      # 1

THRESHOLD   = 128              # binary classification threshold

# ── Sigmoid LUT (identical to HLS DUT) ───────────────────────────────────────
SIGMOID_LUT = [
    12,12,12,12,13,13,13,14,14,14,15,15,15,16,16,16,17,17,18,18,18,19,19,20,
    20,21,21,21,22,22,23,23,24,24,25,26,26,27,27,28,28,29,30,30,31,32,32,33,
    34,34,35,36,36,37,38,39,39,40,41,42,43,44,44,45,46,47,48,49,50,51,52,53,
    54,55,56,57,58,59,60,61,62,63,64,66,67,68,69,70,72,73,74,75,76,78,79,80,
    82,83,84,86,87,88,90,91,92,94,95,97,98,99,101,102,104,105,107,108,110,111,
    113,114,116,117,119,120,122,123,125,126,128,129,130,132,133,135,136,138,
    139,141,142,144,145,147,148,150,151,153,154,156,157,158,160,161,163,164,
    165,167,168,169,171,172,173,175,176,177,179,180,181,182,183,185,186,187,
    188,189,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,
    207,208,209,210,211,211,212,213,214,215,216,216,217,218,219,219,220,221,
    221,222,223,223,224,225,225,226,227,227,228,228,229,229,230,231,231,232,
    232,233,233,234,234,234,235,235,236,236,237,237,237,238,238,239,239,239,
    240,240,240,241,241,241,242,242,242,243,243,243
]

def sigmoid(x):
    idx = min(int(x), 255)
    return SIGMOID_LUT[idx]

def load_csv_flat(path):
    """Load a CSV file (any shape) into a flat list of ints."""
    values = []
    with open(path, newline='') as f:
        for row in csv.reader(f):
            for cell in row:
                cell = cell.strip()
                if cell:
                    values.append(int(cell))
    return values

def infer(x_row, w_hid, w_out):
    """
    Run one sample through the network using exact same fixed-point arithmetic
    as the HLS DUT:
      hidden[j] = sigmoid( (W_hid[0][j]<<8 + sum_k(x[k]*W_hid[k+1][j])) >> 8 )
      output     = (W_out[0]<<8 + sum_k(hidden[k]*W_out[k+1])) >> 8
      label      = 1 if output > THRESHOLD else 0
    """
    # Hidden layer
    hidden = []
    for j in range(NUM_HIDDEN):
        acc = w_hid[0][j] << 8                          # bias << 8
        for k in range(NUM_INPUTS):
            acc += x_row[k] * w_hid[k + 1][j]          # accumulate products
        acc >>= 8                                        # scale down
        acc = min(acc, 255)
        hidden.append(sigmoid(acc))

    # Output layer
    acc = w_out[0][0] << 8                              # bias << 8
    for k in range(NUM_HIDDEN):
        acc += hidden[k] * w_out[k + 1][0]
    acc >>= 8
    acc = min(acc, 255)

    return 1 if acc > THRESHOLD else 0

def load_weights(wh_path, wo_path):
    """Load w_hid.csv and w_out.csv into 2D lists."""
    wh_flat = load_csv_flat(wh_path)
    wo_flat = load_csv_flat(wo_path)

    assert len(wh_flat) == WH_ROWS * WH_COLS, \
        f"w_hid.csv: expected {WH_ROWS*WH_COLS} values, got {len(wh_flat)}"
    assert len(wo_flat) == WO_ROWS * WO_COLS, \
        f"w_out.csv: expected {WO_ROWS*WO_COLS} values, got {len(wo_flat)}"

    w_hid = [[wh_flat[i * WH_COLS + j] for j in range(WH_COLS)] for i in range(WH_ROWS)]
    w_out = [[wo_flat[i * WO_COLS + j] for j in range(WO_COLS)] for i in range(WO_ROWS)]
    return w_hid, w_out

def generate(num_samples, seed, out_dir, wh_path, wo_path):
    rng = random.Random(seed)

    # Load weights if available, otherwise use the known defaults from the project
    if os.path.exists(wh_path) and os.path.exists(wo_path):
        w_hid, w_out = load_weights(wh_path, wo_path)
        print(f"Loaded weights from {wh_path} and {wo_path}")
    else:
        print("Weight files not found — using built-in default weights.")
        print("Place w_hid.csv and w_out.csv alongside this script for custom weights.")
        # Default weights from the project
        w_hid = [
            [26,  6],
            [25, 18],
            [31,  6],
            [29, 26],
            [22,  1],
            [ 1, 28],
            [11,  9],
            [26, 45],
        ]
        w_out = [[80], [50], [200]]

    os.makedirs(out_dir, exist_ok=True)
    x_path      = os.path.join(out_dir, "X.csv")
    labels_path = os.path.join(out_dir, "labels.csv")

    X      = []
    labels = []

    for _ in range(num_samples):
        row = [rng.randint(0, 255) for _ in range(NUM_INPUTS)]
        X.append(row)
        labels.append(infer(row, w_hid, w_out))

    # Write X.csv  — one row per line, comma separated
    with open(x_path, "w", newline='') as f:
        writer = csv.writer(f)
        for row in X:
            writer.writerow(row)

    # Write labels.csv — one label per line
    with open(labels_path, "w", newline='') as f:
        for lbl in labels:
            f.write(f"{lbl}\n")

    # Summary
    ones  = sum(labels)
    zeros = num_samples - ones
    print(f"\nGenerated {num_samples} samples")
    print(f"  Class 1 (output > {THRESHOLD}): {ones}  ({100*ones/num_samples:.1f}%)")
    print(f"  Class 0 (output ≤ {THRESHOLD}): {zeros} ({100*zeros/num_samples:.1f}%)")
    print(f"\nFiles written:")
    print(f"  {x_path}")
    print(f"  {labels_path}")

def main():
    parser = argparse.ArgumentParser(
        description="Generate X.csv and labels.csv for the NN HLS accelerator."
    )
    parser.add_argument(
        "--samples", type=int, default=64,
        help="Number of samples (rows) to generate (default: 64)"
    )
    parser.add_argument(
        "--seed", type=int, default=0,
        help="Random seed for reproducibility (default: 0)"
    )
    parser.add_argument(
        "--out_dir", type=str, default=".",
        help="Output directory for X.csv and labels.csv (default: current directory)"
    )
    parser.add_argument(
        "--w_hid", type=str, default="w_hid.csv",
        help="Path to w_hid.csv (default: w_hid.csv in current directory)"
    )
    parser.add_argument(
        "--w_out", type=str, default="w_out.csv",
        help="Path to w_out.csv (default: w_out.csv in current directory)"
    )
    args = parser.parse_args()

    if args.samples < 1:
        print("Error: --samples must be at least 1")
        return

    generate(
        num_samples = args.samples,
        seed        = args.seed,
        out_dir     = args.out_dir,
        wh_path     = args.w_hid,
        wo_path     = args.w_out,
    )

if __name__ == "__main__":
    main()