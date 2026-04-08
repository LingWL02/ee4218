/******************************************************************************
* NN DMA Application for Vitis
* Input stream order:
*   1) W_hidden: 8  x 2   = 16 words
*   2) W_output: 3  x 1   = 3 words
*   3) X       : N  x 7   words  (N determined at runtime; host sends END_OF_DATA after last row)
* Output:
*   N x 1 words
******************************************************************************/

#include "xparameters.h"
#include "xstatus.h"
#include "xil_printf.h"
#include "xil_cache.h"
#include "xaxidma.h"
#include "xtmrctr.h"
#include "xuartps_hw.h"
#include "sleep.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef struct {
    u32 TxElapsed;
    u32 RxElapsed;
    u32 TotalElapsed;
} Stats;

// -------------------------
// Accelerator dimensions
// -------------------------
#define NUM_INPUTS     7
#define NUM_HIDDEN     2
#define NUM_OUTPUTS    1
#define MAX_SAMPLES    1024     // Maximum X rows this application will accept

#define WH_ROWS        (NUM_INPUTS + 1)     // 8
#define WH_COLS        NUM_HIDDEN           // 2
#define WO_ROWS        (NUM_HIDDEN + 1)     // 3
#define WO_COLS        NUM_OUTPUTS          // 1

#define WH_SIZE        (WH_ROWS * WH_COLS)  // 16  (fixed)
#define WO_SIZE        (WO_ROWS * WO_COLS)  // 3   (fixed)

#define TIMER_COUNTER_0          0
#define POLL_TIMEOUT_COUNTER     10000000

// -------------------------
// Board-specific IDs
// -------------------------
#ifndef SDT
#define DMA_DEV_ID              XPAR_AXIDMA_0_DEVICE_ID
#define TMRCTR_DEVICE_ID        XPAR_TMRCTR_0_DEVICE_ID
#else
#define DMA_BASEADDR            XPAR_XAXIDMA_0_BASEADDR
#define TMRCTR_BASEADDR         XPAR_XTMRCTR_0_BASEADDR
#endif

static XAxiDma  DmaInstance;
static XTmrCtr  TmrCtrInstance;

// Fixed-size weight buffers
static u32 W_hidden_buf[WH_SIZE];
static u32 W_output_buf[WO_SIZE];

// Dynamic X buffer and output buffer — sized to MAX_SAMPLES
static u32 X_buf[MAX_SAMPLES * NUM_INPUTS];
static u32 DestinationBuffer[MAX_SAMPLES * NUM_OUTPUTS];

// Single TX buffer: W_hidden | W_output | X (packed at runtime)
static u32 SourceBuffer[WH_SIZE + WO_SIZE + MAX_SAMPLES * NUM_INPUTS];

static char TERMINATE_TOKEN[] = "TERMINATE";
static char END_OF_DATA_TOKEN[] = "END_OF_DATA";

// -------------------------
// Forward declarations
// -------------------------
static int  RunNNInference(XAxiDma *DmaInstancePtr, XTmrCtr *TmrCtrInstancePtr,
                           u8 TmrCtrNumber, Stats *stats, bool *no_fail);
static int  ReceiveCSVFixed(u32 *Buffer, int TotalElements, Stats *stats, bool *no_fail);
static int  ReceiveCSVRows(u32 *Buffer, int cols, int max_rows,
                           int *rows_received, Stats *stats, bool *no_fail);
static void MergeThreeArrays(u32 *dest, u32 *A, int sizeA, u32 *B, int sizeB, u32 *C, int sizeC);
static void SendCSVResults(u32 *data, int rows, int cols);
static void SendStats(Stats *stats);

static void FlushDCaches(u32 *SourceAddr, u32 tx_bytes, u32 *DestinationAddr, u32 rx_bytes);
static int  TxSend(XAxiDma *DmaInstancePtr, u32 *SourceAddr, u32 tx_bytes,
                   u32 *DestinationAddr, u32 rx_bytes,
                   XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber);
static int  RxReceive(XAxiDma *DmaInstancePtr, u32 *DestinationAddr, u32 rx_bytes,
                      XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats);

#ifndef SDT
static int InitDMA(XAxiDma *DmaInstancePtr, u16 DmaDeviceId);
static int InitTmrCtr(XTmrCtr *TmrCtrInstancePtr, u16 TmrCtrDeviceId, u8 TmrCtrNumber);
#else
static int InitDMA(XAxiDma *DmaInstancePtr, UINTPTR DmaBaseAddress);
static int InitTmrCtr(XTmrCtr *TmrCtrInstancePtr, UINTPTR TmrCtrBaseAddress, u8 TmrCtrNumber);
#endif

// =========================================================================
int main()
// =========================================================================
{
    int   Status = XST_SUCCESS;
    Stats stats  = {0, 0, 0};
    bool  no_fail = false;

#ifndef SDT
    Status = InitDMA(&DmaInstance, DMA_DEV_ID);
#else
    Status = InitDMA(&DmaInstance, DMA_BASEADDR);
#endif
    if (Status != XST_SUCCESS) {
        xil_printf("DMA Initialization Failed\r\n");
        return XST_FAILURE;
    }

#ifndef SDT
    Status = InitTmrCtr(&TmrCtrInstance, TMRCTR_DEVICE_ID, TIMER_COUNTER_0);
#else
    Status = InitTmrCtr(&TmrCtrInstance, TMRCTR_BASEADDR, TIMER_COUNTER_0);
#endif
    if (Status != XST_SUCCESS) {
        xil_printf("Timer Initialization Failed\r\n");
        return XST_FAILURE;
    }

    xil_printf("NN Accelerator DMA Application (dynamic N)\r\n");

    while (1) {
        Status = RunNNInference(&DmaInstance, &TmrCtrInstance,
                                TIMER_COUNTER_0, &stats, &no_fail);
        if (Status != XST_SUCCESS) {
            if (!no_fail) xil_printf("Inference run failed\r\n");
            xil_printf("--- Exiting main() ---\r\n");
            return XST_FAILURE;
        }
    }

    return XST_SUCCESS;
}

// =========================================================================
// RunNNInference
// =========================================================================
static int RunNNInference(XAxiDma *DmaInstancePtr, XTmrCtr *TmrCtrInstancePtr,
                          u8 TmrCtrNumber, Stats *stats, bool *no_fail)
{
    int Status;
    int num_samples = 0;

    // Step 1: Receive fixed-size weight matrices
    xil_printf("Send w_hid.csv (%d values)\r\n", WH_SIZE);
    Status = ReceiveCSVFixed(W_hidden_buf, WH_SIZE, stats, no_fail);
    if (Status != XST_SUCCESS) {
        if (!(*no_fail)) xil_printf("Failed to receive W_hidden\r\n");
        return XST_FAILURE;
    }

    xil_printf("Send w_out.csv (%d values)\r\n", WO_SIZE);
    Status = ReceiveCSVFixed(W_output_buf, WO_SIZE, stats, no_fail);
    if (Status != XST_SUCCESS) {
        if (!(*no_fail)) xil_printf("Failed to receive W_output\r\n");
        return XST_FAILURE;
    }

    // Step 2: Receive X rows until END_OF_DATA — num_samples set at runtime
    xil_printf("Send X.csv (N x %d, send END_OF_DATA after last row)\r\n", NUM_INPUTS);
    Status = ReceiveCSVRows(X_buf, NUM_INPUTS, MAX_SAMPLES, &num_samples, stats, no_fail);
    if (Status != XST_SUCCESS) {
        if (!(*no_fail)) xil_printf("Failed to receive X\r\n");
        return XST_FAILURE;
    }
    xil_printf("Received %d samples.\r\n", num_samples);

    // Step 3: Compute exact transfer sizes now that N is known
    int x_size   = num_samples * NUM_INPUTS;
    int tx_words = WH_SIZE + WO_SIZE + x_size;
    int rx_words = num_samples * NUM_OUTPUTS;

    u32 tx_bytes = (u32)(tx_words * sizeof(u32));
    u32 rx_bytes = (u32)(rx_words * sizeof(u32));

    // Step 4: Pack W_hidden | W_output | X into one contiguous TX buffer
    xil_printf("Packing TX buffer (%d words)...\r\n", tx_words);
    MergeThreeArrays(SourceBuffer,
                     W_hidden_buf, WH_SIZE,
                     W_output_buf, WO_SIZE,
                     X_buf,        x_size);

    // Step 5: Flush caches for exactly the bytes we will transfer
    FlushDCaches(SourceBuffer, tx_bytes, DestinationBuffer, rx_bytes);

    // Step 6: Start DMA (timer starts inside TxSend, before both transfers)
    Status = TxSend(DmaInstancePtr,
                    SourceBuffer,      tx_bytes,
                    DestinationBuffer, rx_bytes,
                    TmrCtrInstancePtr, TmrCtrNumber);
    if (Status != XST_SUCCESS) {
        xil_printf("TX start failed\r\n");
        return XST_FAILURE;
    }

    // Step 7: Poll until both TX and RX complete, record cycle counts
    Status = RxReceive(DmaInstancePtr, DestinationBuffer, rx_bytes,
                       TmrCtrInstancePtr, TmrCtrNumber, stats);
    if (Status != XST_SUCCESS) {
        xil_printf("RX failed\r\n");
        return XST_FAILURE;
    }

    xil_printf("Output received: %d x %d\r\n", num_samples, NUM_OUTPUTS);
    SendCSVResults(DestinationBuffer, num_samples, NUM_OUTPUTS);

    return XST_SUCCESS;
}

// =========================================================================
// FlushDCaches
// =========================================================================
static void FlushDCaches(u32 *SourceAddr, u32 tx_bytes,
                         u32 *DestinationAddr, u32 rx_bytes)
{
    Xil_DCacheFlushRange((UINTPTR)SourceAddr,      tx_bytes);
    Xil_DCacheFlushRange((UINTPTR)DestinationAddr, rx_bytes);
}

// =========================================================================
// TxSend
// Timer starts here, then RX is armed, then TX fires — correct order.
// =========================================================================
static int TxSend(XAxiDma *DmaInstancePtr,
                  u32 *SourceAddr,      u32 tx_bytes,
                  u32 *DestinationAddr, u32 rx_bytes,
                  XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber)
{
    int Status;

    xil_printf("Starting DMA transfers...\r\n");

    // Start the timer BEFORE arming any DMA channel
    XTmrCtr_Reset(TmrCtrInstancePtr, TmrCtrNumber);
    XTmrCtr_Start(TmrCtrInstancePtr, TmrCtrNumber);

    // Arm RX first so it is ready before data arrives
    Status = XAxiDma_SimpleTransfer(DmaInstancePtr,
                                    (UINTPTR)DestinationAddr,
                                    rx_bytes, XAXIDMA_DEVICE_TO_DMA);
    if (Status != XST_SUCCESS) {
        xil_printf("Failed to start RX DMA\r\n");
        return XST_FAILURE;
    }

    // Fire TX — DMA asserts TLAST on the final word of X
    Status = XAxiDma_SimpleTransfer(DmaInstancePtr,
                                    (UINTPTR)SourceAddr,
                                    tx_bytes, XAXIDMA_DMA_TO_DEVICE);
    if (Status != XST_SUCCESS) {
        xil_printf("Failed to start TX DMA\r\n");
        return XST_FAILURE;
    }

    return XST_SUCCESS;
}

// =========================================================================
// RxReceive — poll until TX and RX both done, then record stats
// =========================================================================
static int RxReceive(XAxiDma *DmaInstancePtr, u32 *DestinationAddr, u32 rx_bytes,
                     XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats)
{
    int  TimeOut     = POLL_TIMEOUT_COUNTER;
    bool txDone      = false;
    bool rxDone      = false;
    u32  TxElapsed   = 0;
    u32  TotalElapsed = 0;

    while (TimeOut) {
        if (!txDone && !XAxiDma_Busy(DmaInstancePtr, XAXIDMA_DMA_TO_DEVICE)) {
            txDone    = true;
            TxElapsed = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);
        }

        if (!rxDone && !XAxiDma_Busy(DmaInstancePtr, XAXIDMA_DEVICE_TO_DMA)) {
            rxDone       = true;
            TotalElapsed = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);
        }

        if (txDone && rxDone) break;

        TimeOut--;
        usleep(1U);
    }

    if (!(txDone && rxDone)) {
        XTmrCtr_Stop(TmrCtrInstancePtr, TmrCtrNumber);
        xil_printf("DMA timeout\r\n");
        return XST_FAILURE;
    }

    TotalElapsed = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);
    XTmrCtr_Stop(TmrCtrInstancePtr, TmrCtrNumber);

    Xil_DCacheInvalidateRange((UINTPTR)DestinationAddr, rx_bytes);

    stats->TxElapsed    = TxElapsed;
    stats->RxElapsed    = TotalElapsed - TxElapsed;
    stats->TotalElapsed = TotalElapsed;

    xil_printf("Tx cycles   : %u\r\n", stats->TxElapsed);
    xil_printf("Rx cycles   : %u\r\n", stats->RxElapsed);
    xil_printf("Total cycles: %u\r\n", stats->TotalElapsed);

    return XST_SUCCESS;
}

// =========================================================================
// ReceiveCSVFixed — receive exactly TotalElements values over UART
// Used for the fixed-size weight matrices.
// =========================================================================
static int ReceiveCSVFixed(u32 *Buffer, int TotalElements, Stats *stats, bool *no_fail)
{
    char msg[20];
    int  msg_idx = 0;
    int  count   = 0;
    char RecvChar;

    xil_printf("Receiving %d values...\r\n", TotalElements);

    while (count < TotalElements) {
        RecvChar = XUartPs_RecvByte(XPAR_XUARTPS_0_BASEADDR);

        if (RecvChar == '\r') continue;

        if (RecvChar == ',' || RecvChar == '\n') {
            if (msg_idx > 0) {
                msg[msg_idx] = '\0';

                if (strcmp(msg, TERMINATE_TOKEN) == 0) {
                    xil_printf("Termination command received.\r\n");
                    SendStats(stats);
                    *no_fail = true;
                    return XST_FAILURE;
                }

                Buffer[count] = (u32)atoi(msg);
                count++;
                msg_idx = 0;

                if ((count % 64) == 0) {
                    xil_printf("Progress: %d/%d\r\n", count, TotalElements);
                }
            }
        } else {
            if (msg_idx < (int)(sizeof(msg) - 1)) {
                msg[msg_idx++] = RecvChar;
            }
        }
    }

    xil_printf("Receive complete: %d values\r\n", count);
    return XST_SUCCESS;
}

// =========================================================================
// ReceiveCSVRows — receive an unknown number of rows over UART.
// Each row has `cols` comma-separated values followed by '\n'.
// Stops when it receives a token of "END_OF_DATA".
//
// Host should send:
//   44,90,0,0,24,81,22\n       <- row 0
//   159,250,140,176,...\n      <- row 1
//   ...
//   END_OF_DATA\n              <- signals end of X data
//
// *rows_received is set to the number of complete rows read.
// =========================================================================
static int ReceiveCSVRows(u32 *Buffer, int cols, int max_rows,
                          int *rows_received, Stats *stats, bool *no_fail)
{
    char msg[20];
    int  msg_idx = 0;
    int  col     = 0;   // current column within the row being received
    int  row     = 0;   // number of complete rows received so far
    char RecvChar;

    *rows_received = 0;

    while (row < max_rows) {
        RecvChar = XUartPs_RecvByte(XPAR_XUARTPS_0_BASEADDR);

        if (RecvChar == '\r') continue;

        if (RecvChar == ',' || RecvChar == '\n') {
            if (msg_idx > 0) {
                msg[msg_idx] = '\0';
                msg_idx = 0;

                if (strcmp(msg, TERMINATE_TOKEN) == 0) {
                    xil_printf("Termination command received.\r\n");
                    SendStats(stats);
                    *no_fail = true;
                    return XST_FAILURE;
                }

                if (strcmp(msg, END_OF_DATA_TOKEN) == 0) {
                    *rows_received = row;
                    xil_printf("END_OF_DATA received after %d rows.\r\n", row);
                    return (row > 0) ? XST_SUCCESS : XST_FAILURE;
                }

                // Store value and advance column/row counters
                Buffer[row * cols + col] = (u32)atoi(msg);
                col++;

                if (col == cols) {
                    col = 0;
                    row++;
                    if ((row % 64) == 0)
                        xil_printf("Progress: %d rows received\r\n", row);
                }
            }
        } else {
            if (msg_idx < (int)(sizeof(msg) - 1))
                msg[msg_idx++] = RecvChar;
        }
    }

    // Reached MAX_SAMPLES without seeing END_OF_DATA — treat as done
    xil_printf("Warning: MAX_SAMPLES (%d) reached without END_OF_DATA\r\n", max_rows);
    *rows_received = row;
    return XST_SUCCESS;
}

// =========================================================================
// MergeThreeArrays — pack A | B | C into dest contiguously
// =========================================================================
static void MergeThreeArrays(u32 *dest, u32 *A, int sizeA,
                              u32 *B, int sizeB, u32 *C, int sizeC)
{
    int idx = 0, i;
    for (i = 0; i < sizeA; i++) dest[idx++] = A[i];
    for (i = 0; i < sizeB; i++) dest[idx++] = B[i];
    for (i = 0; i < sizeC; i++) dest[idx++] = C[i];
}

// =========================================================================
// SendStats
// =========================================================================
static void SendStats(Stats *stats)
{
    char buf[12];
    const char *labels[] = {"STATS:TX=", ",RX=", ",TOTAL="};
    u32 values[] = {stats->TxElapsed, stats->RxElapsed, stats->TotalElapsed};

    for (int l = 0; l < 3; l++) {
        for (const char *p = labels[l]; *p != '\0'; p++)
            XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, *p);
        sprintf(buf, "%u", (unsigned int)values[l]);
        for (char *p = buf; *p != '\0'; p++)
            XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, *p);
    }
    XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, '\r');
    XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, '\n');
}

// =========================================================================
// SendCSVResults — send output predictions back to the host over UART
// =========================================================================
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

// =========================================================================
// InitDMA
// =========================================================================
#ifndef SDT
static int InitDMA(XAxiDma *DmaInstancePtr, u16 DmaDeviceId)
#else
static int InitDMA(XAxiDma *DmaInstancePtr, UINTPTR DmaBaseAddress)
#endif
{
    XAxiDma_Config *CfgPtr;
    int Status;

#ifndef SDT
    CfgPtr = XAxiDma_LookupConfig(DmaDeviceId);
#else
    CfgPtr = XAxiDma_LookupConfig(DmaBaseAddress);
#endif
    if (!CfgPtr) {
        xil_printf("No DMA config found\r\n");
        return XST_FAILURE;
    }

    Status = XAxiDma_CfgInitialize(DmaInstancePtr, CfgPtr);
    if (Status != XST_SUCCESS) {
        xil_printf("DMA init failed: %d\r\n", Status);
        return XST_FAILURE;
    }

    if (XAxiDma_HasSg(DmaInstancePtr)) {
        xil_printf("DMA is in SG mode, expected simple mode\r\n");
        return XST_FAILURE;
    }

    XAxiDma_IntrDisable(DmaInstancePtr, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrDisable(DmaInstancePtr, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);

    Status = XAxiDma_Selftest(DmaInstancePtr);
    if (Status != XST_SUCCESS) {
        xil_printf("DMA self-test failed\r\n");
        return XST_FAILURE;
    }

    return XST_SUCCESS;
}

// =========================================================================
// InitTmrCtr
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