/******************************************************************************
* NN DMA Application for Vitis
* Input stream order:
*   1) X       : 64 x 7   = 448 words
*   2) W_hidden: 8  x 2   = 16 words
*   3) W_output: 3  x 1   = 3 words
* Output:
*   64 x 1 = 64 words
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
#define NUM_SAMPLES    64

#define X_ROWS         NUM_SAMPLES
#define X_COLS         NUM_INPUTS
#define WH_ROWS        (NUM_INPUTS + 1)
#define WH_COLS        NUM_HIDDEN
#define WO_ROWS        (NUM_HIDDEN + 1)
#define WO_COLS        NUM_OUTPUTS
#define Y_ROWS         NUM_SAMPLES
#define Y_COLS         NUM_OUTPUTS

#define X_SIZE         (X_ROWS * X_COLS)     // 448
#define WH_SIZE        (WH_ROWS * WH_COLS)   // 16
#define WO_SIZE        (WO_ROWS * WO_COLS)   // 3
#define TX_WORDS       (X_SIZE + WH_SIZE + WO_SIZE)  // 467
#define RX_WORDS       (Y_ROWS * Y_COLS)     // 64

#define TX_PKT_LEN     (TX_WORDS * sizeof(u32))
#define RX_PKT_LEN     (RX_WORDS * sizeof(u32))

#define TIMER_COUNTER_0          0
#define POLL_TIMEOUT_COUNTER     10000000

// -------------------------
// Board-specific IDs
// Update these if your xparameters.h uses different names
// -------------------------
#ifndef SDT
#define DMA_DEV_ID              XPAR_AXIDMA_0_DEVICE_ID
#define TMRCTR_DEVICE_ID        XPAR_TMRCTR_0_DEVICE_ID
#else
#define DMA_BASEADDR            XPAR_XAXIDMA_0_BASEADDR
#define TMRCTR_BASEADDR         XPAR_XTMRCTR_0_BASEADDR
#endif

static XAxiDma DmaInstance;
static XTmrCtr TmrCtrInstance;

static u32 X_buf[X_SIZE];
static u32 W_hidden_buf[WH_SIZE];
static u32 W_output_buf[WO_SIZE];

static u32 SourceBuffer[TX_WORDS];
static u32 DestinationBuffer[RX_WORDS];

static char TERMINATE_TOKEN[] = "TERMINATE";

static int RunNNInference(XAxiDma *DmaInstancePtr, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats, bool *no_fail);
static int ReceiveCSVData(u32 *Buffer, int TotalElements, Stats *stats, bool *no_fail);
static void MergeThreeArrays(u32 *dest, u32 *A, int sizeA, u32 *B, int sizeB, u32 *C, int sizeC);
static void SendCSVResults(u32 *data, int rows, int cols);
static void SendStats(Stats *stats);

static void FlushDCaches(u32 *SourceAddr, u32 *DestinationAddr);
static int TxSend(XAxiDma *DmaInstancePtr, u32 *SourceAddr, u32 *DestinationAddr, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber);
static int RxReceive(XAxiDma *DmaInstancePtr, u32 *DestinationAddr, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats);

#ifndef SDT
static int InitDMA(XAxiDma *DmaInstancePtr, u16 DmaDeviceId);
static int InitTmrCtr(XTmrCtr *TmrCtrInstancePtr, u16 TmrCtrDeviceId, u8 TmrCtrNumber);
#else
static int InitDMA(XAxiDma *DmaInstancePtr, UINTPTR DmaBaseAddress);
static int InitTmrCtr(XTmrCtr *TmrCtrInstancePtr, UINTPTR TmrCtrBaseAddress, u8 TmrCtrNumber);
#endif

int main()
{
    int Status = XST_SUCCESS;
    Stats stats = {0, 0, 0};
    bool no_fail = false;

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

    xil_printf("NN Accelerator DMA Application\r\n");

    while (1) {
        Status = RunNNInference(&DmaInstance, &TmrCtrInstance, TIMER_COUNTER_0, &stats, &no_fail);
        if (Status != XST_SUCCESS) {
            if (!no_fail) {
                xil_printf("Inference run failed\r\n");
            }
            xil_printf("--- Exiting main() ---\r\n");
            return XST_FAILURE;
        }
    }

    return XST_SUCCESS;
}

static int RunNNInference(XAxiDma *DmaInstancePtr, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats, bool *no_fail)
{
    int Status;

    xil_printf("Send X.csv (%d values)\r\n", X_SIZE);
    Status = ReceiveCSVData(X_buf, X_SIZE, stats, no_fail);
    if (Status != XST_SUCCESS) {
        if (!(*no_fail)) xil_printf("Failed to receive X\r\n");
        return XST_FAILURE;
    }

    xil_printf("Send w_hid.csv (%d values)\r\n", WH_SIZE);
    Status = ReceiveCSVData(W_hidden_buf, WH_SIZE, stats, no_fail);
    if (Status != XST_SUCCESS) {
        if (!(*no_fail)) xil_printf("Failed to receive W_hidden\r\n");
        return XST_FAILURE;
    }

    xil_printf("Send w_out.csv (%d values)\r\n", WO_SIZE);
    Status = ReceiveCSVData(W_output_buf, WO_SIZE, stats, no_fail);
    if (Status != XST_SUCCESS) {
        if (!(*no_fail)) xil_printf("Failed to receive W_output\r\n");
        return XST_FAILURE;
    }

    xil_printf("All input received. Packing TX buffer...\r\n");
    MergeThreeArrays(SourceBuffer, X_buf, X_SIZE, W_hidden_buf, WH_SIZE, W_output_buf, WO_SIZE);

    FlushDCaches(SourceBuffer, DestinationBuffer);

    Status = TxSend(DmaInstancePtr, SourceBuffer, DestinationBuffer, TmrCtrInstancePtr, TmrCtrNumber);
    if (Status != XST_SUCCESS) {
        xil_printf("TX start failed\r\n");
        return XST_FAILURE;
    }

    Status = RxReceive(DmaInstancePtr, DestinationBuffer, TmrCtrInstancePtr, TmrCtrNumber, stats);
    if (Status != XST_SUCCESS) {
        xil_printf("RX failed\r\n");
        return XST_FAILURE;
    }

    xil_printf("Output received: %d x %d\r\n", Y_ROWS, Y_COLS);
    SendCSVResults(DestinationBuffer, Y_ROWS, Y_COLS);

    return XST_SUCCESS;
}

static void FlushDCaches(u32 *SourceAddr, u32 *DestinationAddr)
{
    Xil_DCacheFlushRange((UINTPTR)SourceAddr, TX_PKT_LEN);
    Xil_DCacheFlushRange((UINTPTR)DestinationAddr, RX_PKT_LEN);
}

static int TxSend(XAxiDma *DmaInstancePtr, u32 *SourceAddr, u32 *DestinationAddr, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber)
{
    int Status;

    xil_printf("Starting DMA transfers...\r\n");

    XTmrCtr_Reset(TmrCtrInstancePtr, TmrCtrNumber);
    XTmrCtr_Start(TmrCtrInstancePtr, TmrCtrNumber);

    Status = XAxiDma_SimpleTransfer(DmaInstancePtr, (UINTPTR)DestinationAddr, RX_PKT_LEN, XAXIDMA_DEVICE_TO_DMA);
    if (Status != XST_SUCCESS) {
        xil_printf("Failed to start RX DMA\r\n");
        return XST_FAILURE;
    }

    Status = XAxiDma_SimpleTransfer(DmaInstancePtr, (UINTPTR)SourceAddr, TX_PKT_LEN, XAXIDMA_DMA_TO_DEVICE);
    if (Status != XST_SUCCESS) {
        xil_printf("Failed to start TX DMA\r\n");
        return XST_FAILURE;
    }

    return XST_SUCCESS;
}

static int RxReceive(XAxiDma *DmaInstancePtr, u32 *DestinationAddr, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats)
{
    int TimeOut = POLL_TIMEOUT_COUNTER;
    bool txDone = false;
    bool rxDone = false;
    u32 TxElapsed = 0;
    u32 TotalElapsed = 0;

    while (TimeOut) {
        if (!txDone && !XAxiDma_Busy(DmaInstancePtr, XAXIDMA_DMA_TO_DEVICE)) {
            txDone = true;
            TxElapsed = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);
        }

        if (!rxDone && !XAxiDma_Busy(DmaInstancePtr, XAXIDMA_DEVICE_TO_DMA)) {
            rxDone = true;
            TotalElapsed = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);
        }

        if (txDone && rxDone) {
            break;
        }

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

    Xil_DCacheInvalidateRange((UINTPTR)DestinationAddr, RX_PKT_LEN);

    stats->TxElapsed = TxElapsed;
    stats->RxElapsed = TotalElapsed - TxElapsed;
    stats->TotalElapsed = TotalElapsed;

    xil_printf("Tx cycles   : %u\r\n", stats->TxElapsed);
    xil_printf("Rx cycles   : %u\r\n", stats->RxElapsed);
    xil_printf("Total cycles: %u\r\n", stats->TotalElapsed);

    return XST_SUCCESS;
}

static int ReceiveCSVData(u32 *Buffer, int TotalElements, Stats *stats, bool *no_fail)
{
    char msg[20];
    int msg_idx = 0;
    int count = 0;
    char RecvChar;

    xil_printf("Receiving %d values...\r\n", TotalElements);

    while (count < TotalElements) {
        RecvChar = XUartPs_RecvByte(XPAR_XUARTPS_0_BASEADDR);

        if (RecvChar == '\r') {
            continue;
        }

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
    return (count == TotalElements) ? XST_SUCCESS : XST_FAILURE;
}

static void MergeThreeArrays(u32 *dest, u32 *A, int sizeA, u32 *B, int sizeB, u32 *C, int sizeC)
{
    int idx = 0;
    int i;

    for (i = 0; i < sizeA; i++) dest[idx++] = A[i];
    for (i = 0; i < sizeB; i++) dest[idx++] = B[i];
    for (i = 0; i < sizeC; i++) dest[idx++] = C[i];
}

static void SendStats(Stats *stats)
{
    char buf[12];
    const char *labels[] = {"STATS:TX=", ",RX=", ",TOTAL="};
    u32 values[] = {stats->TxElapsed, stats->RxElapsed, stats->TotalElapsed};

    for (int l = 0; l < 3; l++) {
        for (const char *p = labels[l]; *p != '\0'; p++) {
            XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, *p);
        }
        sprintf(buf, "%u", (unsigned int)values[l]);
        for (char *p = buf; *p != '\0'; p++) {
            XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, *p);
        }
    }

    XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, '\r');
    XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, '\n');
}

static void SendCSVResults(u32 *data, int rows, int cols)
{
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            char buffer[12];
            sprintf(buffer, "%u", (unsigned int)(data[i * cols + j] & 0xFFU));
            for (char *p = buffer; *p != '\0'; p++) {
                XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, *p);
            }
            if (j < cols - 1) {
                XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, ',');
            }
        }
        XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, '\r');
        XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, '\n');
    }
}

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