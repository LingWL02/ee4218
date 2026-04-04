/*
----------------------------------------------------------------------------------
--	(c) Rajesh C Panicker, NUS,
--  Description : Self-checking testbench for AXI Stream Coprocessor (HLS) implementing the sum of 4 numbers
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "hls_stream.h"
#include "ap_axi_sdata.h"

typedef ap_axis<32,0,0,0> AXIS;

/***************** Coprocessor function declaration *********************/

void myip_v1_0_HLS_optim(hls::stream<AXIS>& S_AXIS, hls::stream<AXIS>& M_AXIS);


/***************** Macros *********************/
#define MATRIX_A_ROWS 64
#define MATRIX_A_COLS 8
#define MATRIX_B_ROWS 8
#define MATRIX_B_COLS 1

#define NUMBER_OF_INPUT_WORDS_A (MATRIX_A_ROWS * MATRIX_A_COLS)  // 512 words
#define NUMBER_OF_INPUT_WORDS_B (MATRIX_B_ROWS * MATRIX_B_COLS)  // 8 words
#define NUMBER_OF_OUTPUT_WORDS (MATRIX_A_ROWS * MATRIX_B_COLS)   // 64 words


/************************** Variable Definitions *****************************/
uint8_t matrix_A[MATRIX_A_ROWS][MATRIX_A_COLS];
uint8_t matrix_B[MATRIX_B_ROWS][MATRIX_B_COLS];
uint8_t expected_result[MATRIX_A_ROWS][MATRIX_B_COLS];
uint8_t actual_result[MATRIX_A_ROWS][MATRIX_B_COLS];


/************************** Helper Functions *****************************/

// Read Matrix A from CSV file
int read_matrix_A(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("Error: Could not open file %s\n", filename);
        return -1;
    }
    
    for (int i = 0; i < MATRIX_A_ROWS; i++) {
        for (int j = 0; j < MATRIX_A_COLS; j++) {
            int value;
            if (j < MATRIX_A_COLS - 1) {
                fscanf(file, "%d,", &value);
            } else {
                fscanf(file, "%d", &value);
            }
            matrix_A[i][j] = (uint8_t)value;
        }
    }
    
    fclose(file);
    return 0;
}

// Read Matrix B from CSV file
int read_matrix_B(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("Error: Could not open file %s\n", filename);
        return -1;
    }
    
    for (int i = 0; i < MATRIX_B_ROWS; i++) {
        int value;
        fscanf(file, "%d", &value);
        matrix_B[i][0] = (uint8_t)value;
    }
    
    fclose(file);
    return 0;
}

// Software version of matrix multiplication for verification
void compute_expected_result() {
    for (int i = 0; i < MATRIX_A_ROWS; i++) {
        for (int j = 0; j < MATRIX_B_COLS; j++) {
            uint16_t acc = 0;
            
            for (int k = 0; k < MATRIX_A_COLS; k++) {
                // Multiply A[i][k] * B[k][j]
                uint16_t product = (uint16_t)matrix_A[i][k] * (uint16_t)matrix_B[k][j];
                // Right shift by 8 bits (0.8 fixed point scale factor)
                product = product >> 8;
                // Accumulate
                acc += product;
            }
            
            // Store result as int8 (apply bitmask)
            expected_result[i][j] = (uint8_t)(acc & 0xFF);
        }
    }
}


/*****************************************************************************
* Main function
******************************************************************************/
int main()
{
    int i, j;
    int success;
    AXIS write_input, read_output;
    hls::stream<AXIS> S_AXIS;
    hls::stream<AXIS> M_AXIS;

    printf("=================================================\n");
    printf("Matrix Multiplication Test Bench\n");
    printf("=================================================\n\n");

    /************** Read input matrices from CSV files ************/
    printf("Reading Matrix A from A.csv...\n");
    if (read_matrix_A("A.csv") != 0) {
        printf("Failed to read Matrix A\n");
        return 1;
    }
    
    printf("Reading Matrix B from B.csv...\n");
    if (read_matrix_B("B.csv") != 0) {
        printf("Failed to read Matrix B\n");
        return 1;
    }
    
    printf("Matrices loaded successfully.\n");
    printf("Matrix A: %dx%d\n", MATRIX_A_ROWS, MATRIX_A_COLS);
    printf("Matrix B: %dx%d\n", MATRIX_B_ROWS, MATRIX_B_COLS);
    printf("Expected Output: %dx%d\n\n", MATRIX_A_ROWS, MATRIX_B_COLS);

    /************** Compute expected result using software ************/
    printf("Computing expected result using software...\n");
    compute_expected_result();
    printf("Expected result computed.\n\n");

    /******************** Transmit Matrix A to Coprocessor ***********************/
    printf("Transmitting Matrix A (%d words)...\n", NUMBER_OF_INPUT_WORDS_A);
    for (i = 0; i < MATRIX_A_ROWS; i++) {
        for (j = 0; j < MATRIX_A_COLS; j++) {
            write_input.data = (uint32_t)(uint8_t)matrix_A[i][j];
            write_input.last = 0;
            write_input.keep = 0xFU;
            write_input.strb = 0xFU;
            S_AXIS.write(write_input);
        }
    }
    printf("Matrix A transmitted.\n");

    /******************** Transmit Matrix B to Coprocessor ***********************/
    printf("Transmitting Matrix B (%d words)...\n", NUMBER_OF_INPUT_WORDS_B);
    for (i = 0; i < MATRIX_B_ROWS; i++) {
        for (j = 0; j < MATRIX_B_COLS; j++) {
            write_input.data = (uint32_t)(uint8_t)matrix_B[i][j];
            write_input.last = 0;
            write_input.keep = 0xFU;
            write_input.strb = 0xFU;
            S_AXIS.write(write_input);
        }
    }
    printf("Matrix B transmitted.\n\n");

    /********************* Call the hardware function ***************/
    printf("Calling hardware function (myip_v1_0_HLS)...\n");
    myip_v1_0_HLS_optim(S_AXIS, M_AXIS);
    printf("Hardware function completed.\n\n");

    /******************** Receive Result from Coprocessor ***********************/
    printf("Receiving result (%d words)...\n", NUMBER_OF_OUTPUT_WORDS);
    for (i = 0; i < MATRIX_A_ROWS; i++) {
        for (j = 0; j < MATRIX_B_COLS; j++) {
            read_output = M_AXIS.read();
            actual_result[i][j] = (int8_t)(read_output.data & 0xFF);
        }
    }
    printf("Result received.\n\n");

    /************************** Verify Results *****************************/
    printf("Verifying results...\n");
    success = 1;
    int error_count = 0;
    
    for (i = 0; i < MATRIX_A_ROWS; i++) {
        for (j = 0; j < MATRIX_B_COLS; j++) {
            if (actual_result[i][j] != expected_result[i][j]) {
                success = 0;
                error_count++;
            }
        }
    }
    
    printf("\n=================================================\n");
    if (success) {
        printf("TEST PASSED!\n");
        printf("All %d output values match expected results.\n", NUMBER_OF_OUTPUT_WORDS);
    } else {
        printf("TEST FAILED!\n");
        printf("Found %d mismatches out of %d values.\n", error_count, NUMBER_OF_OUTPUT_WORDS);
    }
    printf("=================================================\n");

    // Print all results in unsigned format
    printf("\nAll Results (Unsigned):\n");
    printf("Index | Expected (dec) | Expected (hex) | Actual (dec) | Actual (hex) | Match\n");
    printf("------|----------------|----------------|--------------|--------------|------\n");
    for (i = 0; i < MATRIX_A_ROWS; i++) {
        uint8_t expected_u8 = (uint8_t)expected_result[i][0];
        uint8_t actual_u8 = (uint8_t)actual_result[i][0];
        printf("  %2d  |      %3u       |      0x%02X      |     %3u      |     0x%02X     |  %s\n", 
               i, 
               expected_u8,
               expected_u8,
               actual_u8,
               actual_u8,
               expected_result[i][0] == actual_result[i][0] ? "yes" : "no");
    }

    return success ? 0 : 1;
}
