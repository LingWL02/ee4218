/*
----------------------------------------------------------------------------------
--  NN Hardware Accelerator - HLS Implementation  (Streaming / TLAST-transparent)
--  Inference only (no training)
--
--  Stream order:
--    1. W_hidden  (8 x 2 = 16 words)  — TLAST on word 16
--    2. W_output  (3 x 1 =  3 words)  — TLAST on word  3
--    3. X         (N x 7 words)       — TLAST on last feature of last sample
--
--  X is never buffered. Each sample is computed and output as it arrives.
--  N is completely unbounded — the accelerator runs until TLAST is seen on X.
----------------------------------------------------------------------------------
*/

#include "hls_stream.h"
#include "ap_int.h"
#include "ap_axi_sdata.h"

// Neural Network Architecture
#define NUM_INPUTS   7
#define NUM_HIDDEN   2
#define NUM_OUTPUTS  1

// Matrix dimensions
#define HIDDEN_W_ROWS (NUM_INPUTS + 1)   // 8  (7 features + 1 bias)
#define HIDDEN_W_COLS  NUM_HIDDEN        // 2
#define OUTPUT_W_ROWS (NUM_HIDDEN + 1)   // 3  (2 hidden + 1 bias)
#define OUTPUT_W_COLS  NUM_OUTPUTS       // 1

typedef ap_uint<8>           fixed_point;
typedef ap_axis<32, 0, 0, 0> AXIS;

// Sigmoid LUT
static const fixed_point sigmoid_lut[256] = {
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
};

fixed_point sigmoid(ap_uint<16> x) {
#pragma HLS INLINE
    ap_uint<8> idx = (x > 255) ? ap_uint<8>(255) : ap_uint<8>(x);
    return sigmoid_lut[idx];
}

void nn_accelerator(hls::stream<AXIS>& S_AXIS, hls::stream<AXIS>& M_AXIS) {
#pragma HLS INTERFACE ap_ctrl_none port=return
#pragma HLS INTERFACE axis port=S_AXIS
#pragma HLS INTERFACE axis port=M_AXIS

    // Weight matrices — loaded once upfront, reused for every sample
    ap_uint<8> W_hidden[HIDDEN_W_ROWS][HIDDEN_W_COLS];   // 8 x 2
    ap_uint<8> W_output[OUTPUT_W_ROWS][OUTPUT_W_COLS];   // 3 x 1

#pragma HLS ARRAY_PARTITION variable=W_hidden dim=2 complete
#pragma HLS ARRAY_PARTITION variable=W_output dim=1 complete

    AXIS pkt, out_pkt;
    int i, j, k;

    // =========================================================
    // 1. READ HIDDEN LAYER WEIGHTS  (16 words, TLAST on word 16)
    // =========================================================
    read_hidden_weights:
    for (i = 0; i < HIDDEN_W_ROWS; i++) {
        for (j = 0; j < HIDDEN_W_COLS; j++) {
#pragma HLS PIPELINE II=1
            pkt = S_AXIS.read();
            W_hidden[i][j] = (ap_uint<8>)(pkt.data & 0xFF);
        }
    }

    // =========================================================
    // 2. READ OUTPUT LAYER WEIGHTS  (3 words, TLAST on word 3)
    // =========================================================
    read_output_weights:
    for (i = 0; i < OUTPUT_W_ROWS; i++) {
        for (j = 0; j < OUTPUT_W_COLS; j++) {
#pragma HLS PIPELINE II=1
            pkt = S_AXIS.read();
            W_output[i][j] = (ap_uint<8>)(pkt.data & 0xFF);
        }
    }

    // =========================================================
    // 3. PROCESS SAMPLES — one at a time, truly unbounded
    //
    //    For each sample:
    //      a) Read NUM_INPUTS features into a small row register (7 bytes)
    //      b) Compute hidden layer activations
    //      c) Compute output
    //      d) Write result, asserting TLAST when last_sample is true
    //
    //    X is NEVER fully buffered. Only one row lives in registers
    //    at a time. N has no upper bound.
    // =========================================================

    bool last_sample = false;

    process_samples:
    while (!last_sample) {

        // ----- (a) Read one sample row (7 features) into registers -----
        ap_uint<8> x_row[NUM_INPUTS];
#pragma HLS ARRAY_PARTITION variable=x_row complete

        read_features:
        for (j = 0; j < NUM_INPUTS; j++) {
#pragma HLS PIPELINE II=1
            pkt = S_AXIS.read();
            x_row[j] = (ap_uint<8>)(pkt.data & 0xFF);

            // TLAST must be asserted on the last feature of the last sample.
            // We latch it here and act on it after the output is written,
            // so the result for this final sample is still produced correctly.
            if (j == NUM_INPUTS - 1 && pkt.last) {
                last_sample = true;
            }
        }

        // ----- (b) Compute hidden layer -----
        ap_uint<8> hidden_out[NUM_HIDDEN];
#pragma HLS ARRAY_PARTITION variable=hidden_out complete

        compute_hidden:
        for (j = 0; j < NUM_HIDDEN; j++) {
#pragma HLS PIPELINE II=1
            // Bias weight is in 0.8 format — shift left by 8 to match
            // the scale of the 16-bit products before the final >>8
            ap_uint<32> acc = (ap_uint<32>)W_hidden[0][j] << 8;

            hidden_mac:
            for (k = 0; k < NUM_INPUTS; k++) {
                acc += (ap_uint<32>)x_row[k] * (ap_uint<32>)W_hidden[k+1][j];
            }

            acc >>= 8;
            if (acc > 255) acc = 255;
            hidden_out[j] = sigmoid((ap_uint<16>)acc);
        }

        // ----- (c) Compute output layer (linear → threshold) -----
        ap_uint<8> result[NUM_OUTPUTS];
#pragma HLS ARRAY_PARTITION variable=result complete

        compute_output:
        for (j = 0; j < NUM_OUTPUTS; j++) {
#pragma HLS PIPELINE II=1
            ap_uint<32> acc = (ap_uint<32>)W_output[0][j] << 8;

            output_mac:
            for (k = 0; k < NUM_HIDDEN; k++) {
                acc += (ap_uint<32>)hidden_out[k] * (ap_uint<32>)W_output[k+1][j];
            }

            acc >>= 8;
            if (acc > 255) acc = 255;

            // Binary classification threshold
            result[j] = (acc > 128) ? ap_uint<8>(1) : ap_uint<8>(0);
        }

        // ----- (d) Write result — mirror TLAST to output -----
        write_result:
        for (j = 0; j < NUM_OUTPUTS; j++) {
#pragma HLS PIPELINE II=1
            out_pkt.data = (ap_uint<32>)result[j];
            out_pkt.keep = 0xFU;
            out_pkt.strb = 0xFU;
            out_pkt.last = (last_sample && (j == NUM_OUTPUTS - 1)) ? 1 : 0;
            M_AXIS.write(out_pkt);
        }
    }
}
