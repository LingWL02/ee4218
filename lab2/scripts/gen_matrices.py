import numpy as np
import sys
import os

def main():
    if len(sys.argv) != 3:
        print("Usage: python gen_matrices.py m n")
        return

    m: int = int(sys.argv[1])
    n: int = int(sys.argv[2])

    A = np.random.randint(0, 256, size=(m, n), dtype=np.uint8).astype(np.uint16)
    B = np.random.randint(0, 256, size=(n,), dtype=np.uint8).astype(np.uint16)
    RES = []
    for i in range(len(A)):
        acc = sum(((A[i][j] * B[j]) >> 8) for j in range(len(B)))  # divide by 256,
        RES.append(acc & 0xFF)  # keep 8 bits
    RES = np.array(RES, dtype=np.uint8)

    # Ensure the data directory exists
    output_dir = "../data"
    os.makedirs(output_dir, exist_ok=True)

    # Save matrices to CSV files
    np.savetxt(os.path.join(output_dir, "A.csv"), A, delimiter=",", fmt="%d")
    np.savetxt(os.path.join(output_dir, "B.csv"), B, delimiter=",", fmt="%d")
    np.savetxt(os.path.join(output_dir, "LABELS.csv"), RES, delimiter=",", fmt="%d")

    print(f"Matrices saved to {output_dir}")

if __name__ == "__main__":
    main()
