import random
import os

script_dir = os.path.dirname(os.path.realpath(__file__))

# ----------------------------
# Parameters
# ----------------------------
m = 2
n = 4
num_tests = 10        # number of A/B pairs
MAX_VAL = 0xFF       # allow full 8-bit range for stress testing

random.seed(1)

# ----------------------------
# Helper functions
# ----------------------------
def gen_matrix(rows, cols):
    return [[random.randint(0, MAX_VAL) for _ in range(cols)] for _ in range(rows)]

def gen_vector(length):
    return [random.randint(0, MAX_VAL) for _ in range(length)]

def compute_res(A, B):
    res = []
    for i in range(len(A)):
        acc = sum(A[i][j] * B[j] for j in range(len(B)))
        res.append((acc >> 8) & 0xFF)  # divide by 256, keep 8 bits
    return res

# ----------------------------
# Generate test vectors
# ----------------------------
tests = []

for _ in range(num_tests):
    A = gen_matrix(m, n)
    B = gen_vector(n)
    RES = compute_res(A, B)
    tests.append((A, B, RES))

# ----------------------------
# Write test_input.mem
# ----------------------------
with open(f"{script_dir}/test_input.mem", "w") as f:
    for idx, (A, B, _) in enumerate(tests, start=1):
        f.write(f"// first input vector ({idx})\n")
        for row in A:
            for val in row:
                f.write(f"{val:02X}\n")

        f.write(f"// second input vector ({idx})\n")
        for val in B:
            f.write(f"{val:02X}\n")

# ----------------------------
# Write test_result_expected.mem
# ----------------------------
with open(f"{script_dir}/test_result_expected.mem", "w") as f:
    for idx, (_, _, RES) in enumerate(tests, start=1):
        f.write(f"// output vector ({idx})\n")
        for val in RES:
            f.write(f"{val:02X}\n")

print("Generated test_input.mem and test_result_expected.mem")
