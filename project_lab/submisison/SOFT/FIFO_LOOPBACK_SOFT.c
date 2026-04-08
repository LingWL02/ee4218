/******************************************************************************
* NN Software Inference - Standalone SOFT implementation
* Platform: loopback_platform (AXI FIFO, no DMA)
*
* Receives data over UART, runs NN inference purely in software on ARM,
* and returns predictions over UART.
*
* Input order (sent via RealTerm as CSV):
*   1) X       : 64 x 7  = 448 values
*   2) W_hidden: 8  x 2  = 16  values
*   3) W_output: 3  x 1  = 3   values
* Output:
*   64 x 1 predictions (0 or 1), sent back as CSV
******************************************************************************/

#include "xparameters.h"
#include "xstatus.h"
#include "xil_printf.h"
#include "xtmrctr.h"
#include "xuartps_hw.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef struct {
    u32 SoftElapsed;
} Stats;

// -------------------------
// NN dimensions
// -------------------------
#define NUM_INPUTS     7
#define NUM_HIDDEN     2
#define NUM_OUTPUTS    1
#define NUM_SAMPLES    64

#define X_ROWS         NUM_SAMPLES
#define X_COLS         NUM_INPUTS
#define WH_ROWS        (NUM_INPUTS + 1)   // 8 (bias + 7 features)
#define WH_COLS        NUM_HIDDEN          // 2
#define WO_ROWS        (NUM_HIDDEN + 1)   // 3 (bias + 2 hidden)
#define WO_COLS        NUM_OUTPUTS         // 1

#define X_SIZE         (X_ROWS  * X_COLS)   // 448
#define WH_SIZE        (WH_ROWS * WH_COLS)  // 16
#define WO_SIZE        (WO_ROWS * WO_COLS)  // 3
#define Y_SIZE         (NUM_SAMPLES * NUM_OUTPUTS)  // 64

// -------------------------
// Timer
// -------------------------
#define TIMER_COUNTER_0  0

#ifndef SDT
#define TMRCTR_DEVICE_ID  XPAR_TMRCTR_0_DEVICE_ID
#else
#define TMRCTR_BASEADDR   XPAR_XTMRCTR_0_BASEADDR
#endif

// -------------------------
// Sigmoid LUT
// Matches NN_HLS.cpp exactly (0.8 unsigned fixed-point)
// -------------------------
static const u8 sigmoid_lut[256] = {
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

// -------------------------
// Buffers
// -------------------------
static XTmrCtr TmrCtrInstance;

static u32 X_buf[X_SIZE];
static u32 W_hidden_buf[WH_SIZE];
static u32 W_output_buf[WO_SIZE];
static u32 OutputBuffer[Y_SIZE];

static char TERMINATE_TOKEN[] = "TERMINATE";

// -------------------------
// Forward declarations
// -------------------------
static int  RunSoftInference(XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats);
static int  ReceiveCSVData(u32 *Buffer, int TotalElements, Stats *stats);
static void SendCSVResults(u32 *data, int rows, int cols);
static void SendStats(Stats *stats);

#ifndef SDT
static int InitTmrCtr(XTmrCtr *TmrCtrInstancePtr, u16 TmrCtrDeviceId, u8 TmrCtrNumber);
#else
static int InitTmrCtr(XTmrCtr *TmrCtrInstancePtr, UINTPTR TmrCtrBaseAddress, u8 TmrCtrNumber);
#endif

// =========================================================================
// MAIN
// =========================================================================
int main()
{
    int Status;
    Stats stats = {0};

#ifndef SDT
    Status = InitTmrCtr(&TmrCtrInstance, TMRCTR_DEVICE_ID, TIMER_COUNTER_0);
#else
    Status = InitTmrCtr(&TmrCtrInstance, TMRCTR_BASEADDR, TIMER_COUNTER_0);
#endif
    if (Status != XST_SUCCESS) {
        xil_printf("Timer init failed\r\n");
        return XST_FAILURE;
    }

    xil_printf("NN Software Inference Ready\r\n");

    while (1) {
        xil_printf("Send X.csv (%d values)\r\n", X_SIZE);
        Status = ReceiveCSVData(X_buf, X_SIZE, &stats);
        if (Status != XST_SUCCESS) continue;

        xil_printf("Send w_hid.csv (%d values)\r\n", WH_SIZE);
        Status = ReceiveCSVData(W_hidden_buf, WH_SIZE, &stats);
        if (Status != XST_SUCCESS) continue;

        xil_printf("Send w_out.csv (%d values)\r\n", WO_SIZE);
        Status = ReceiveCSVData(W_output_buf, WO_SIZE, &stats);
        if (Status != XST_SUCCESS) continue;

        Status = RunSoftInference(&TmrCtrInstance, TIMER_COUNTER_0, &stats);
        if (Status != XST_SUCCESS) {
            xil_printf("Inference failed\r\n");
            continue;
        }

        xil_printf("Output (%d x %d):\r\n", NUM_SAMPLES, NUM_OUTPUTS);
        SendCSVResults(OutputBuffer, NUM_SAMPLES, NUM_OUTPUTS);
    }

    return XST_SUCCESS;
}

// =========================================================================
// SOFTWARE INFERENCE
// Mirrors NN_HLS.cpp exactly in 0.8 unsigned fixed-point arithmetic.
//
// Hidden layer:
//   acc  = W_hidden[0][j] << 8          (bias scaled by 256)
//   acc += X[i][k] * W_hidden[k+1][j]   (for k = 0..6)
//   acc  = acc >> 8                      (scale back to 0.8)
//   hidden[i][j] = sigmoid_lut[clamp(acc, 255)]
//
// Output layer (linear + threshold):
//   acc  = W_output[0][0] << 8
//   acc += hidden[i][k] * W_output[k+1][0]  (for k = 0..1)
//   acc  = acc >> 8
//   output[i] = (acc > 128) ? 1 : 0
// =========================================================================
static int RunSoftInference(XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats)
{
    u8 hidden[NUM_SAMPLES][NUM_HIDDEN];
    int i, j, k;

    xil_printf("Running software inference...\r\n");

    XTmrCtr_Reset(TmrCtrInstancePtr, TmrCtrNumber);
    XTmrCtr_Start(TmrCtrInstancePtr, TmrCtrNumber);

    // ------------------------------------------------------------------
    // Hidden layer
    // ------------------------------------------------------------------
    for (i = 0; i < NUM_SAMPLES; i++) {
        for (j = 0; j < NUM_HIDDEN; j++) {
            u32 acc = 0;

            // Bias: W_hidden[0][j] * 256
            acc = (u32)W_hidden_buf[0 * WH_COLS + j] << 8;

            // Weighted sum of inputs
            for (k = 0; k < NUM_INPUTS; k++) {
                acc += (u32)X_buf[i * X_COLS + k] * (u32)W_hidden_buf[(k + 1) * WH_COLS + j];
            }

            // Scale down by 256
            acc = acc >> 8;

            // Clamp then sigmoid
            if (acc > 255) acc = 255;
            hidden[i][j] = sigmoid_lut[acc];
        }
    }

    // ------------------------------------------------------------------
    // Output layer
    // ------------------------------------------------------------------
    for (i = 0; i < NUM_SAMPLES; i++) {
        u32 acc = 0;

        // Bias: W_output[0][0] * 256
        acc = (u32)W_output_buf[0 * WO_COLS + 0] << 8;

        // Weighted sum of hidden outputs
        for (k = 0; k < NUM_HIDDEN; k++) {
            acc += (u32)hidden[i][k] * (u32)W_output_buf[(k + 1) * WO_COLS + 0];
        }

        // Scale down by 256
        acc = acc >> 8;

        // Clamp and threshold
        if (acc > 255) acc = 255;
        OutputBuffer[i] = (acc > 128) ? 1 : 0;
    }

    u32 SoftElapsed = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);
    XTmrCtr_Stop(TmrCtrInstancePtr, TmrCtrNumber);
    stats->SoftElapsed = SoftElapsed;

    xil_printf("Soft inference cycles: %u\r\n", SoftElapsed);

    return XST_SUCCESS;
}

// =========================================================================
// UART HELPERS
// =========================================================================
static int ReceiveCSVData(u32 *Buffer, int TotalElements, Stats *stats)
{
    char msg[20];
    int msg_idx = 0;
    int count = 0;
    char RecvChar;

    xil_printf("Receiving %d values...\r\n", TotalElements);

    while (count < TotalElements) {
        RecvChar = XUartPs_RecvByte(XPAR_XUARTPS_0_BASEADDR);

        if (RecvChar == '\r') continue;

        if (RecvChar == ',' || RecvChar == '\n') {
            if (msg_idx > 0) {
                msg[msg_idx] = '\0';

                if (strcmp(msg, TERMINATE_TOKEN) == 0) {
                    xil_printf("Termination received.\r\n");
                    SendStats(stats);
                    return XST_FAILURE;
                }

                Buffer[count] = (u32)atoi(msg);
                count++;
                msg_idx = 0;

                if ((count % 64) == 0)
                    xil_printf("Progress: %d/%d\r\n", count, TotalElements);
            }
        } else {
            if (msg_idx < (int)(sizeof(msg) - 1))
                msg[msg_idx++] = RecvChar;
        }
    }

    xil_printf("Received %d values.\r\n", count);
    return XST_SUCCESS;
}

static void SendCSVResults(u32 *data, int rows, int cols)
{
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            char buffer[12];
            sprintf(buffer, "%u", (unsigned int)(data[i * cols + j] & 0xFFU));
            for (char *p = buffer; *p != '\0'; p++)
                XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, *p);
            if (j < cols - 1)
                XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, ',');
        }
        XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, '\r');
        XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, '\n');
    }
}

static void SendStats(Stats *stats)
{
    char buf[12];
    const char *label = "STATS:SOFT=";
    for (const char *p = label; *p != '\0'; p++)
        XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, *p);
    sprintf(buf, "%u", (unsigned int)stats->SoftElapsed);
    for (char *p = buf; *p != '\0'; p++)
        XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, *p);
    XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, '\r');
    XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, '\n');
}

// =========================================================================
// TIMER INIT
// =========================================================================
#ifndef SDT
static int InitTmrCtr(XTmrCtr *TmrCtrInstancePtr, u16 TmrCtrDeviceId, u8 TmrCtrNumber)
#else
static int InitTmrCtr(XTmrCtr *TmrCtrInstancePtr, UINTPTR TmrCtrBaseAddress, u8 TmrCtrNumber)
#endif
{
    int Status;

#ifndef SDT
    Status = XTmrCtr_Initialize(TmrCtrInstancePtr, TmrCtrDeviceId);
#else
    Status = XTmrCtr_Initialize(TmrCtrInstancePtr, TmrCtrBaseAddress);
#endif
    if (Status != XST_SUCCESS) {
        xil_printf("Timer init failed\r\n");
        return XST_FAILURE;
    }

    XTmrCtr_SetOptions(TmrCtrInstancePtr, TmrCtrNumber, 0);
    XTmrCtr_SetResetValue(TmrCtrInstancePtr, TmrCtrNumber, 0);

    Status = XTmrCtr_SelfTest(TmrCtrInstancePtr, TmrCtrNumber);
    if (Status != XST_SUCCESS) {
        xil_printf("Timer self-test failed\r\n");
        return XST_FAILURE;
    }

    return XST_SUCCESS;
}
