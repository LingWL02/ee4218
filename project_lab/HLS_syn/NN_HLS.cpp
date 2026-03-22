/*
----------------------------------------------------------------------------------
--  NN Hardware Accelerator - HLS Implementation
--  Inference only (no training)
--  Input: 64 x 7 data matrix, weights matrices
--  Output: 64 x 1 prediction vector
----------------------------------------------------------------------------------
*/

#include "hls_stream.h"
#include "ap_int.h"
#include "ap_axi_sdata.h"
#include <cmath>

// Neural Network Architecture
#define NUM_INPUTS 7
#define NUM_HIDDEN 2
#define NUM_OUTPUTS 1
#define NUM_SAMPLES 64

// Matrix dimensions
#define HIDDEN_W_ROWS (NUM_INPUTS + 1)  // 8 (7 features + 1 bias)
#define HIDDEN_W_COLS NUM_HIDDEN         // 2
#define OUTPUT_W_ROWS (NUM_HIDDEN + 1)   // 3 (2 hidden + 1 bias)
#define OUTPUT_W_COLS NUM_OUTPUTS        // 1

// Fixed-point format: 0.8 (8-bit unsigned, scale factor 256)
typedef ap_uint<8> fixed_point;
typedef ap_axis<32, 0, 0, 0> AXIS;  // data, user, id, dest

// Sigmoid array
static const fixed_point sigmoid_lut[256] = {
    12,12,12,12,13,13,13,14,14,14,15,15,15,16,16,16,17,17,18,18,18,19,19,20,20,21,21,21,22,22,23,23,24,24,25,26,26,27,27,28,28,29,30,30,31,32,32,33,34,34,35,36,36,37,38,39,39,40,41,42,43,44,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,66,67,68,69,70,72,73,74,75,76,78,79,80,82,83,84,86,87,88,90,91,92,94,95,97,98,99,101,102,104,105,107,108,110,111,113,114,116,117,119,120,122,123,125,126,128,129,130,132,133,135,136,138,139,141,142,144,145,147,148,150,151,153,154,156,157,158,160,161,163,164,165,167,168,169,171,172,173,175,176,177,179,180,181,182,183,185,186,187,188,189,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,211,212,213,214,215,216,216,217,218,219,219,220,221,221,222,223,223,224,225,225,226,227,227,228,228,229,229,230,231,231,232,232,233,233,234,234,234,235,235,236,236,237,237,237,238,238,239,239,239,240,240,240,241,241,241,242,242,242,243,243,243
};

// Sigmoid lookup table or computation function
fixed_point sigmoid(ap_uint<16> x) {
#pragma HLS INLINE
    // x is in range [0, 255] representing 0.8 fixed point
    ap_uint<8> idx = (x > 255) ? 255 : (ap_uint<8>)x;
    return sigmoid_lut[idx];
}

void nn_accelerator(hls::stream<AXIS>& S_AXIS, hls::stream<AXIS>& M_AXIS) {
#pragma HLS INTERFACE ap_ctrl_none port=return
#pragma HLS INTERFACE axis port=S_AXIS
#pragma HLS INTERFACE axis port=M_AXIS

    // Input data matrix: 64 x 7
    ap_uint<8> X[NUM_SAMPLES][NUM_INPUTS];
    
    // Weight matrices (NOT hardcoded - received from host)
    ap_uint<8> W_hidden[HIDDEN_W_ROWS][HIDDEN_W_COLS];  // 8 x 2
    ap_uint<8> W_output[OUTPUT_W_ROWS][OUTPUT_W_COLS];  // 3 x 1
    
    // Intermediate results
    ap_uint<8> hidden[NUM_SAMPLES][NUM_HIDDEN];  // Hidden layer output: 64 x 2
    ap_uint<8> output_data[NUM_SAMPLES][NUM_OUTPUTS];  // Output layer: 64 x 1

#pragma HLS ARRAY_PARTITION variable=W_hidden dim=2 complete
#pragma HLS ARRAY_PARTITION variable=W_output dim=1 complete

    int i, j, k;
    AXIS read_input, write_output;
    ap_uint<16> acc;

    // ========== READ INPUT DATA ==========
    // Read X matrix (64 x 7 = 448 words)
    read_input_data: for (i = 0; i < NUM_SAMPLES; i++) {
        for (j = 0; j < NUM_INPUTS; j++) {
#pragma HLS PIPELINE II=1
            read_input = S_AXIS.read();
            X[i][j] = (ap_uint<8>)(read_input.data & 0xFF);
        }
    }

    // ========== READ HIDDEN LAYER WEIGHTS ==========
    // Read W_hidden (8 x 2 = 16 words)
    read_hidden_weights: for (i = 0; i < HIDDEN_W_ROWS; i++) {
        for (j = 0; j < HIDDEN_W_COLS; j++) {
#pragma HLS PIPELINE II=1
            read_input = S_AXIS.read();
            W_hidden[i][j] = (ap_uint<8>)(read_input.data & 0xFF);
        }
    }

    // ========== READ OUTPUT LAYER WEIGHTS ==========
    // Read W_output (3 x 1 = 3 words)
    read_output_weights: for (i = 0; i < OUTPUT_W_ROWS; i++) {
        for (j = 0; j < OUTPUT_W_COLS; j++) {
#pragma HLS PIPELINE II=1
            read_input = S_AXIS.read();
            W_output[i][j] = (ap_uint<8>)(read_input.data & 0xFF);
        }
    }

    // ========== COMPUTE HIDDEN LAYER ==========
    // hidden[i][j] = sigmoid(X[i] * W_hidden[:, j])
    hidden_layer: for (i = 0; i < NUM_SAMPLES; i++) { //for each of the input rows
        for (j = 0; j < NUM_HIDDEN; j++) { //for each hidden neuron
#pragma HLS PIPELINE II=1
            acc = 0;
            
            // Compute weighted sum: bias + X[i] * W_hidden[1..7, j]
            acc = (ap_uint<16>)W_hidden[0][j] << 8;  // Bias term (scale by 256)
            
            hidden_dot_product: for (k = 0; k < NUM_INPUTS; k++) { // compute the weighted matrix mul
                ap_uint<16> product = (ap_uint<16>)X[i][k] * (ap_uint<16>)W_hidden[k+1][j];
                product = product >> 8;  // Scale down by 256 (0.8 fixed point)
                acc += product;
            }
            
            // Clamp accumulator to 8-bit range and apply sigmoid (TODO: should i truncate here? &0xFF)
            if (acc > 255) acc = 255;
            hidden[i][j] = sigmoid(acc);
        }
    }

    // ========== COMPUTE OUTPUT LAYER ==========
    // output[i][0] = hidden[i] * W_output[1..2] + W_output[0] (linear activation)
    output_layer: for (i = 0; i < NUM_SAMPLES; i++) {
        for (j = 0; j < NUM_OUTPUTS; j++) {
#pragma HLS PIPELINE II=1
            acc = 0;
            
            // Bias term
            acc = (ap_uint<16>)W_output[0][j] << 8;  // Scale by 256
            
            output_dot_product: for (k = 0; k < NUM_HIDDEN; k++) {
                ap_uint<16> product = (ap_uint<16>)hidden[i][k] * (ap_uint<16>)W_output[k+1][j];
                product = product >> 8;  // Scale down by 256
                acc += product;
            }
            
            // Clamp to 8-bit range
            if (acc > 255) acc = 255;
            output_data[i][j] = (ap_uint<8>)acc;
        }
    }

    // ========== WRITE OUTPUT ==========
    // Write output matrix (64 x 1 = 64 words)
    write_output_data: for (i = 0; i < NUM_SAMPLES; i++) {
        for (j = 0; j < NUM_OUTPUTS; j++) {
#pragma HLS PIPELINE II=1
            write_output.data = (ap_uint<32>)output_data[i][j];
            write_output.keep = 0xFU;
            write_output.strb = 0xFU;
            write_output.last = 0;
            
            // Assert TLAST on final word
            if (i == NUM_SAMPLES - 1 && j == NUM_OUTPUTS - 1) {
                write_output.last = 1;
            }
            
            M_AXIS.write(write_output);
        }
    }
}