/*
----------------------------------------------------------------------------------
--	(c) Rajesh C Panicker, NUS,
--  Description : AXI Stream Coprocessor (HLS), implementing the sum of 4 numbers
--	License terms :
--	You are free to use this code as long as you
--		(i) DO NOT post a modified version of this on any public repository;
--		(ii) use it only for educational purposes;
--		(iii) accept the responsibility to ensure that your implementation does not violate any intellectual property of any entity.
--		(iv) accept that the program is provided "as is" without warranty of any kind or assurance regarding its suitability for any particular purpose;
--		(v) send an email to rajesh.panicker@ieee.org briefly mentioning its use (except when used for the course EE4218/CEG5203 at the National University of Singapore);
--		(vi) retain this notice in this file or any files derived from this.
----------------------------------------------------------------------------------
*/

#include "hls_stream.h"
#include "ap_int.h"
#include "ap_axi_sdata.h"

#define MATRIX_A_ROWS 64
#define MATRIX_A_COLS 8
#define MATRIX_B_ROWS 8
#define MATRIX_B_COLS 1

#define NUMBER_OF_INPUT_WORDS_A (MATRIX_A_ROWS * MATRIX_A_COLS)  // 64 * 8 = 512 words
#define NUMBER_OF_INPUT_WORDS_B (MATRIX_B_ROWS * MATRIX_B_COLS)  // 8 * 1 = 8 words
#define NUMBER_OF_OUTPUT_WORDS (MATRIX_A_ROWS * MATRIX_B_COLS)   // 64 * 1 = 64 words

typedef ap_axis<32,0,0,0> AXIS;  //data, user, id, dest

void myip_v1_0_HLS(hls::stream<AXIS>& S_AXIS, hls::stream<AXIS>& M_AXIS){
#pragma HLS INTERFACE ap_ctrl_none port=return
#pragma HLS INTERFACE axis port=S_AXIS
#pragma HLS INTERFACE axis port=M_AXIS

    // Storage for matrices
    ap_uint<8> A[MATRIX_A_ROWS][MATRIX_A_COLS];
    ap_uint<8> B[MATRIX_B_ROWS][MATRIX_B_COLS];
    ap_uint<8> C[MATRIX_A_ROWS][MATRIX_B_COLS];
    
#pragma HLS ARRAY_PARTITION variable=A dim=2 complete
#pragma HLS ARRAY_PARTITION variable=B dim=1 complete
    
    int i, j, k;
    AXIS read_input, write_output;
    
    // Read Matrix A (64 x 8 = 512 words)
    read_matrix_A: for(i = 0; i < MATRIX_A_ROWS; i++){
        for(j = 0; j < MATRIX_A_COLS; j++){
#pragma HLS PIPELINE II=1
            read_input = S_AXIS.read();
            A[i][j] = (ap_uint<8>)(read_input.data & 0xFF);
        }
    }
    
    // Read Matrix B (8 x 1 = 8 words)
    read_matrix_B: for(i = 0; i < MATRIX_B_ROWS; i++){
        for(j = 0; j < MATRIX_B_COLS; j++){
#pragma HLS PIPELINE II=1
            read_input = S_AXIS.read();
            B[i][j] = (ap_uint<8>)(read_input.data & 0xFF);
        }
    }
    
    // Matrix Multiplication: C = A * B
    // C is 64 x 1
    matmul_outer: for(i = 0; i < MATRIX_A_ROWS; i++){
        matmul_middle: for(j = 0; j < MATRIX_B_COLS; j++){
#pragma HLS PIPELINE II=1
            ap_uint<16> acc = 0;  // Accumulator with extra bits to prevent overflow
            
            matmul_inner: for(k = 0; k < MATRIX_A_COLS; k++){
                // Multiply A[i][k] * B[k][j]
                ap_uint<16> product = A[i][k] * B[k][j];
                // Right shift by 8 bits (0.8 fixed point scale factor)
                product = product >> 8;
                // Accumulate
                acc += product;
            }
            
            // Store result as int8 (apply bitmask or truncate)
            C[i][j] = (ap_uint<8>)(acc & 0xFF);
        }
    }
    
    // Write output Matrix C (64 x 1 = 64 words)
    write_output_matrix: for(i = 0; i < MATRIX_A_ROWS; i++){
        for(j = 0; j < MATRIX_B_COLS; j++){
#pragma HLS PIPELINE II=1
            write_output.data = (ap_uint<32>)C[i][j];
            write_output.keep = 0xFU;
            write_output.strb = 0xFU;
            write_output.last = 0;
            
            // Assert TLAST on the final word
            if(i == MATRIX_A_ROWS-1 && j == MATRIX_B_COLS-1){
                write_output.last = 1;
            }
            
            M_AXIS.write(write_output);
        }
    }
}
