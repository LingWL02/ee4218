#include <cstdio>
#include <cstdint>
#include <cstring>
#include "hls_stream.h"
#include "ap_int.h"
#include "ap_axi_sdata.h"

// Match DUT types
typedef ap_axis<32, 0, 0, 0> AXIS;

// Match DUT dimensions in [NN_HLS.cpp](http://_vscodecontentref_/2)
#define NUM_INPUTS   7
#define NUM_HIDDEN   2
#define NUM_OUTPUTS  1
#define NUM_SAMPLES  64

#define HIDDEN_W_ROWS (NUM_INPUTS + 1)   // 8
#define HIDDEN_W_COLS NUM_HIDDEN         // 2
#define OUTPUT_W_ROWS (NUM_HIDDEN + 1)   // 3
#define OUTPUT_W_COLS NUM_OUTPUTS        // 1

#define NUM_INPUT_WORDS_X        (NUM_SAMPLES * NUM_INPUTS)      // 448
#define NUM_INPUT_WORDS_W_HIDDEN (HIDDEN_W_ROWS * HIDDEN_W_COLS) // 16
#define NUM_INPUT_WORDS_W_OUTPUT (OUTPUT_W_ROWS * OUTPUT_W_COLS) // 3
#define NUM_OUTPUT_WORDS         (NUM_SAMPLES * NUM_OUTPUTS)     // 64

// DUT declaration
void nn_accelerator(hls::stream<AXIS>& S_AXIS, hls::stream<AXIS>& M_AXIS);

// Test data buffers
static uint8_t X[NUM_SAMPLES][NUM_INPUTS];
static uint8_t W_hidden[HIDDEN_W_ROWS][HIDDEN_W_COLS];
static uint8_t W_output[OUTPUT_W_ROWS][OUTPUT_W_COLS];

static uint8_t expected[NUM_SAMPLES][NUM_OUTPUTS];
static uint8_t actual[NUM_SAMPLES][NUM_OUTPUTS];

// Same LUT as DUT for exact matching
static const uint8_t sigmoid_lut[256] = {
    12,12,12,12,13,13,13,14,14,14,15,15,15,16,16,16,17,17,18,18,18,19,19,20,20,21,21,21,22,22,23,23,24,24,25,26,26,27,27,28,28,29,30,30,31,32,32,33,34,34,35,36,36,37,38,39,39,40,41,42,43,44,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,66,67,68,69,70,72,73,74,75,76,78,79,80,82,83,84,86,87,88,90,91,92,94,95,97,98,99,101,102,104,105,107,108,110,111,113,114,116,117,119,120,122,123,125,126,128,129,130,132,133,135,136,138,139,141,142,144,145,147,148,150,151,153,154,156,157,158,160,161,163,164,165,167,168,169,171,172,173,175,176,177,179,180,181,182,183,185,186,187,188,189,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,211,212,213,214,215,216,216,217,218,219,219,220,221,221,222,223,223,224,225,225,226,227,227,228,228,229,229,230,231,231,232,232,233,233,234,234,234,235,235,236,236,237,237,237,238,238,239,239,239,240,240,240,241,241,241,242,242,242,243,243,243
};

static inline uint8_t sigmoid_ref(uint16_t x) {
    uint8_t idx = (x > 255) ? 255 : (uint8_t)x;
    return sigmoid_lut[idx];
}

static bool read_csv_matrix_u8_2d(const char* filename, uint8_t* buf, int rows, int cols) {
    FILE* f = fopen(filename, "r");
    if (!f) return false;

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            int v = 0;
            // Accept both comma-separated and whitespace/newline-separated
            if (fscanf(f, " %d", &v) != 1) {
                fclose(f);
                return false;
            }
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            buf[i * cols + j] = (uint8_t)v;

            // Consume an optional comma
            int c = fgetc(f);
            if (c != ',') {
                if (c != EOF) ungetc(c, f);
            }
        }
    }

    fclose(f);
    return true;
}

static void generate_deterministic_data() {
    // Deterministic pseudo-pattern, easy to reproduce
    for (int i = 0; i < NUM_SAMPLES; i++) {
        for (int j = 0; j < NUM_INPUTS; j++) {
            X[i][j] = (uint8_t)((i * 13 + j * 29 + 17) & 0xFF);
        }
    }

    for (int i = 0; i < HIDDEN_W_ROWS; i++) {
        for (int j = 0; j < HIDDEN_W_COLS; j++) {
            W_hidden[i][j] = (uint8_t)((i * 31 + j * 47 + 11) & 0xFF);
        }
    }

    for (int i = 0; i < OUTPUT_W_ROWS; i++) {
        for (int j = 0; j < OUTPUT_W_COLS; j++) {
            W_output[i][j] = (uint8_t)((i * 53 + j * 7 + 23) & 0xFF);
        }
    }
}

static void compute_expected_sw() {
    uint8_t hidden[NUM_SAMPLES][NUM_HIDDEN];

    // Hidden layer: bias<<8 + sum((x*w)>>8), then clamp-to-255 before sigmoid
    for (int i = 0; i < NUM_SAMPLES; i++) {
        for (int j = 0; j < NUM_HIDDEN; j++) {
            uint16_t acc = ((uint16_t)W_hidden[0][j]) << 8;
            for (int k = 0; k < NUM_INPUTS; k++) {
                uint16_t product = (uint16_t)X[i][k] * (uint16_t)W_hidden[k + 1][j];
                acc += (product >> 8);
            }
            if (acc > 255) acc = 255;
            hidden[i][j] = sigmoid_ref(acc);
        }
    }

    // Output layer: bias<<8 + sum((hidden*w)>>8), then clamp-to-255 and cast to u8
    for (int i = 0; i < NUM_SAMPLES; i++) {
        for (int j = 0; j < NUM_OUTPUTS; j++) {
            uint16_t acc = ((uint16_t)W_output[0][j]) << 8;
            for (int k = 0; k < NUM_HIDDEN; k++) {
                uint16_t product = (uint16_t)hidden[i][k] * (uint16_t)W_output[k + 1][j];
                acc += (product >> 8);
            }
            if (acc > 255) acc = 255;
            expected[i][j] = (uint8_t)acc;
        }
    }
}

int main() {
    hls::stream<AXIS> S_AXIS;
    hls::stream<AXIS> M_AXIS;
    AXIS pkt;

    std::printf("=============================================\n");
    std::printf("NN HLS Coprocessor Testbench\n");
    std::printf("=============================================\n");

    // Try CSV first; fallback to generated test vectors.
    // Expected filenames in current working dir:
    // X.csv (64x7), w_hid.csv (8x2), w_out.csv (3x1)
    bool okX  = read_csv_matrix_u8_2d("X.csv",     &X[0][0],        NUM_SAMPLES,   NUM_INPUTS);
    bool okWh = read_csv_matrix_u8_2d("w_hid.csv", &W_hidden[0][0], HIDDEN_W_ROWS, HIDDEN_W_COLS);
    bool okWo = read_csv_matrix_u8_2d("w_out.csv", &W_output[0][0], OUTPUT_W_ROWS, OUTPUT_W_COLS);

    if (!(okX && okWh && okWo)) {
        std::printf("CSV files not fully available. Using deterministic generated vectors.\n");
        generate_deterministic_data();
    } else {
        std::printf("Loaded X.csv, w_hid.csv, w_out.csv successfully.\n");
    }

    compute_expected_sw();

    // Stream input in exact DUT order:
    // 1) X (64x7), 2) W_hidden (8x2), 3) W_output (3x1)
    for (int i = 0; i < NUM_SAMPLES; i++) {
        for (int j = 0; j < NUM_INPUTS; j++) {
            pkt.data = (uint32_t)X[i][j];
            pkt.keep = 0xF;
            pkt.strb = 0xF;
            pkt.last = 0;
            S_AXIS.write(pkt);
        }
    }

    for (int i = 0; i < HIDDEN_W_ROWS; i++) {
        for (int j = 0; j < HIDDEN_W_COLS; j++) {
            pkt.data = (uint32_t)W_hidden[i][j];
            pkt.keep = 0xF;
            pkt.strb = 0xF;
            pkt.last = 0;
            S_AXIS.write(pkt);
        }
    }

    for (int i = 0; i < OUTPUT_W_ROWS; i++) {
        for (int j = 0; j < OUTPUT_W_COLS; j++) {
            pkt.data = (uint32_t)W_output[i][j];
            pkt.keep = 0xF;
            pkt.strb = 0xF;
            pkt.last = 0;
            S_AXIS.write(pkt);
        }
    }

    // Call DUT
    nn_accelerator(S_AXIS, M_AXIS);

    // Read and verify outputs
    int mismatches = 0;
    int tlast_errors = 0;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        for (int j = 0; j < NUM_OUTPUTS; j++) {
            AXIS out = M_AXIS.read();
            actual[i][j] = (uint8_t)(out.data & 0xFF);

            bool should_last = (i == (NUM_SAMPLES - 1) && j == (NUM_OUTPUTS - 1));
            if ((bool)out.last != should_last) {
                tlast_errors++;
            }

            if (actual[i][j] != expected[i][j]) {
                if (mismatches < 10) {
                    std::printf("Mismatch at sample %d: exp=%u act=%u\n",
                                i, (unsigned)expected[i][j], (unsigned)actual[i][j]);
                }
                mismatches++;
            }
        }
    }

    std::printf("---------------------------------------------\n");
    std::printf("Input words sent : %d\n",
                NUM_INPUT_WORDS_X + NUM_INPUT_WORDS_W_HIDDEN + NUM_INPUT_WORDS_W_OUTPUT);
    std::printf("Output words read: %d\n", NUM_OUTPUT_WORDS);
    std::printf("Data mismatches  : %d\n", mismatches);
    std::printf("TLAST errors     : %d\n", tlast_errors);
    std::printf("---------------------------------------------\n");

    if (mismatches == 0 && tlast_errors == 0) {
        std::printf("TEST PASSED\n");
        return 0;
    }

    std::printf("TEST FAILED\n");
    return 1;
}