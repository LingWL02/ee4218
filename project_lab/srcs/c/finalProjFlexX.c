/******************************************************************************
* NN DMA Application for Vitis
*
* Stream order to accelerator (three separate DMA TX transfers):
*   1) W_hidden : 8  x 2  = 16 words  — TLAST asserted by DMA at end of buffer
*   2) W_output : 3  x 1  =  3 words  — TLAST asserted by DMA at end of buffer
*   3) X        : N  x 7  words       — TLAST asserted by DMA at end of buffer
*                                        N is determined at runtime from UART input
* Output:
*   N x 1 words  (one prediction per sample, DUT asserts TLAST on last word)
*
* Key point: The DMA hardware automatically asserts TLAST on the last beat of
* whatever buffer size you give it. You never set TLAST manually in software —
* you just tell the DMA how many bytes to send per transfer.
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
// Fixed accelerator dimensions
// (only X rows are dynamic)
// -------------------------
#define NUM_INPUTS      7
#define NUM_HIDDEN      2
#define NUM_OUTPUTS     1
#define MAX_SAMPLES     1024    // Maximum X rows this application will accept

#define WH_ROWS         (NUM_INPUTS + 1)    // 8
#define WH_COLS         NUM_HIDDEN          // 2
#define WO_ROWS         (NUM_HIDDEN + 1)    // 3
#define WO_COLS         NUM_OUTPUTS         // 1

#define WH_SIZE         (WH_ROWS * WH_COLS) // 16  (fixed)
#define WO_SIZE         (WO_ROWS * WO_COLS) // 3   (fixed)

#define TIMER_COUNTER_0         0
#define POLL_TIMEOUT_COUNTER    10000000

#ifndef SDT
#define DMA_DEV_ID          XPAR_AXIDMA_0_DEVICE_ID
#define TMRCTR_DEVICE_ID    XPAR_TMRCTR_0_DEVICE_ID
#else
#define DMA_BASEADDR        XPAR_XAXIDMA_0_BASEADDR
#define TMRCTR_BASEADDR     XPAR_XTMRCTR_0_BASEADDR
#endif

static XAxiDma  DmaInstance;
static XTmrCtr  TmrCtrInstance;

// Fixed-size weight buffers
static u32 W_hidden_buf[WH_SIZE];
static u32 W_output_buf[WO_SIZE];

// Dynamic X buffer and output buffer — sized to MAX_SAMPLES
static u32 X_buf[MAX_SAMPLES * NUM_INPUTS];
static u32 DestinationBuffer[MAX_SAMPLES * NUM_OUTPUTS];

static char TERMINATE_TOKEN[] = "TERMINATE";

// -------------------------
// Forward declarations
// -------------------------
static int  RunNNInference(XAxiDma *DmaInstancePtr, XTmrCtr *TmrCtrInstancePtr,
                           u8 TmrCtrNumber, Stats *stats, bool *no_fail);
static int  ReceiveCSVFixed(u32 *Buffer, int TotalElements, Stats *stats, bool *no_fail);
static int  ReceiveCSVRows(u32 *Buffer, int cols, int max_rows,
                           int *rows_received, Stats *stats, bool *no_fail);
static int  WaitDmaDone(XAxiDma *DmaInstancePtr, int direction, int timeout_us);
static void SendCSVResults(u32 *data, int rows, int cols);
static void SendStats(Stats *stats);

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
    int   Status  = XST_SUCCESS;
    Stats stats   = {0, 0, 0};
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
//
// Execution order:
//   1. Receive w_hid.csv over UART  (fixed 16 values)
//   2. Receive w_out.csv over UART  (fixed  3 values)
//   3. Receive X.csv over UART      (N rows x 7 cols, N unknown until done)
//   4. Arm RX DMA for N output words
//   5. TX packet 1: W_hidden  → DMA asserts TLAST after 16 words
//   6. Wait TX1 done
//   7. TX packet 2: W_output  → DMA asserts TLAST after  3 words
//   8. Wait TX2 done
//   9. TX packet 3: X data    → DMA asserts TLAST after N*7 words
//  10. Wait TX3 + RX done, collect timing
//  11. Send results back over UART
// =========================================================================
static int RunNNInference(XAxiDma *DmaInstancePtr, XTmrCtr *TmrCtrInstancePtr,
                          u8 TmrCtrNumber, Stats *stats, bool *no_fail)
{
    int Status;
    int num_samples = 0;

    // ------------------------------------------------------------------
    // Step 1: Receive W_hidden over UART (always exactly WH_SIZE values)
    // ------------------------------------------------------------------
    xil_printf("Send w_hid.csv (%d values)\r\n", WH_SIZE);
    Status = ReceiveCSVFixed(W_hidden_buf, WH_SIZE, stats, no_fail);
    if (Status != XST_SUCCESS) {
        if (!(*no_fail)) xil_printf("Failed to receive W_hidden\r\n");
        return XST_FAILURE;
    }

    // ------------------------------------------------------------------
    // Step 2: Receive W_output over UART (always exactly WO_SIZE values)
    // ------------------------------------------------------------------
    xil_printf("Send w_out.csv (%d values)\r\n", WO_SIZE);
    Status = ReceiveCSVFixed(W_output_buf, WO_SIZE, stats, no_fail);
    if (Status != XST_SUCCESS) {
        if (!(*no_fail)) xil_printf("Failed to receive W_output\r\n");
        return XST_FAILURE;
    }

    // ------------------------------------------------------------------
    // Step 3: Receive X over UART — row by row, stop on END_OF_DATA token
    //
    // The host PC sends rows as comma-separated values, one row per line.
    // After the last row it sends the token "END_OF_DATA\n".
    // We count rows as they arrive so num_samples is known by the end.
    // ------------------------------------------------------------------
    xil_printf("Send X.csv (N x %d, send END_OF_DATA after last row)\r\n", NUM_INPUTS);
    Status = ReceiveCSVRows(X_buf, NUM_INPUTS, MAX_SAMPLES,
                            &num_samples, stats, no_fail);
    if (Status != XST_SUCCESS) {
        if (!(*no_fail)) xil_printf("Failed to receive X\r\n");
        return XST_FAILURE;
    }
    xil_printf("Received %d samples.\r\n", num_samples);

    // Derived sizes now that N is known
    int x_size      = num_samples * NUM_INPUTS;   // words in X buffer
    int rx_words    = num_samples * NUM_OUTPUTS;  // words expected back

    u32 wh_bytes = WH_SIZE   * sizeof(u32);
    u32 wo_bytes = WO_SIZE   * sizeof(u32);
    u32 x_bytes  = x_size    * sizeof(u32);
    u32 rx_bytes = rx_words  * sizeof(u32);

    // ------------------------------------------------------------------
    // Step 4: Flush caches for all buffers before DMA touches them
    // ------------------------------------------------------------------
    Xil_DCacheFlushRange((UINTPTR)W_hidden_buf,    wh_bytes);
    Xil_DCacheFlushRange((UINTPTR)W_output_buf,    wo_bytes);
    Xil_DCacheFlushRange((UINTPTR)X_buf,           x_bytes);
    Xil_DCacheFlushRange((UINTPTR)DestinationBuffer, rx_bytes);

    // ------------------------------------------------------------------
    // Step 5: Arm RX DMA BEFORE any TX fires.
    //
    // The DUT may start producing output as soon as it finishes processing
    // the first sample. If RX isn't armed by then, output beats are lost.
    // Arm it now for exactly rx_words words — the DUT's TLAST will signal
    // the DMA when the transfer is complete.
    // ------------------------------------------------------------------
    xil_printf("Arming RX DMA (%d words)...\r\n", rx_words);
    Status = XAxiDma_SimpleTransfer(DmaInstancePtr,
                                    (UINTPTR)DestinationBuffer,
                                    rx_bytes,
                                    XAXIDMA_DEVICE_TO_DMA);
    if (Status != XST_SUCCESS) {
        xil_printf("Failed to arm RX DMA\r\n");
        return XST_FAILURE;
    }

    // Start timer — measures total time from first TX beat to RX complete
    XTmrCtr_Reset(TmrCtrInstancePtr, TmrCtrNumber);
    XTmrCtr_Start(TmrCtrInstancePtr, TmrCtrNumber);

    // ------------------------------------------------------------------
    // Step 6: TX packet 1 — W_hidden (16 words)
    // DMA asserts TLAST automatically on the 16th word.
    // Wait for this transfer to fully complete before sending packet 2,
    // because Simple DMA only supports one TX transfer at a time.
    // ------------------------------------------------------------------
    xil_printf("TX packet 1: W_hidden (%d words)...\r\n", WH_SIZE);
    Status = XAxiDma_SimpleTransfer(DmaInstancePtr,
                                    (UINTPTR)W_hidden_buf,
                                    wh_bytes,
                                    XAXIDMA_DMA_TO_DEVICE);
    if (Status != XST_SUCCESS) {
        xil_printf("TX1 start failed\r\n");
        return XST_FAILURE;
    }
    Status = WaitDmaDone(DmaInstancePtr, XAXIDMA_DMA_TO_DEVICE, POLL_TIMEOUT_COUNTER);
    if (Status != XST_SUCCESS) {
        xil_printf("TX1 timeout\r\n");
        return XST_FAILURE;
    }

    // ------------------------------------------------------------------
    // Step 7: TX packet 2 — W_output (3 words)
    // DMA asserts TLAST automatically on the 3rd word.
    // ------------------------------------------------------------------
    xil_printf("TX packet 2: W_output (%d words)...\r\n", WO_SIZE);
    Status = XAxiDma_SimpleTransfer(DmaInstancePtr,
                                    (UINTPTR)W_output_buf,
                                    wo_bytes,
                                    XAXIDMA_DMA_TO_DEVICE);
    if (Status != XST_SUCCESS) {
        xil_printf("TX2 start failed\r\n");
        return XST_FAILURE;
    }
    Status = WaitDmaDone(DmaInstancePtr, XAXIDMA_DMA_TO_DEVICE, POLL_TIMEOUT_COUNTER);
    if (Status != XST_SUCCESS) {
        xil_printf("TX2 timeout\r\n");
        return XST_FAILURE;
    }

    // ------------------------------------------------------------------
    // Step 8: TX packet 3 — X data (N*7 words)
    // DMA asserts TLAST automatically on the last word.
    // This is the signal the DUT uses to stop reading samples.
    // ------------------------------------------------------------------
    xil_printf("TX packet 3: X data (%d words, %d samples)...\r\n", x_size, num_samples);
    Status = XAxiDma_SimpleTransfer(DmaInstancePtr,
                                    (UINTPTR)X_buf,
                                    x_bytes,
                                    XAXIDMA_DMA_TO_DEVICE);
    if (Status != XST_SUCCESS) {
        xil_printf("TX3 start failed\r\n");
        return XST_FAILURE;
    }

    // ------------------------------------------------------------------
    // Step 9: Wait for both TX3 and RX to complete, collect timing
    // ------------------------------------------------------------------
    int TimeOut  = POLL_TIMEOUT_COUNTER;
    bool txDone  = false;
    bool rxDone  = false;
    u32  TxElapsed    = 0;
    u32  TotalElapsed = 0;

    while (TimeOut) {
        if (!txDone && !XAxiDma_Busy(DmaInstancePtr, XAXIDMA_DMA_TO_DEVICE)) {
            txDone     = true;
            TxElapsed  = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);
        }
        if (!rxDone && !XAxiDma_Busy(DmaInstancePtr, XAXIDMA_DEVICE_TO_DMA)) {
            rxDone        = true;
            TotalElapsed  = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);
        }
        if (txDone && rxDone) break;
        TimeOut--;
        usleep(1U);
    }

    XTmrCtr_Stop(TmrCtrInstancePtr, TmrCtrNumber);

    if (!(txDone && rxDone)) {
        xil_printf("DMA timeout waiting for TX3/RX\r\n");
        return XST_FAILURE;
    }

    // Invalidate RX cache region before CPU reads it
    Xil_DCacheInvalidateRange((UINTPTR)DestinationBuffer, rx_bytes);

    stats->TxElapsed    = TxElapsed;
    stats->RxElapsed    = TotalElapsed - TxElapsed;
    stats->TotalElapsed = TotalElapsed;

    xil_printf("Tx cycles   : %u\r\n", stats->TxElapsed);
    xil_printf("Rx cycles   : %u\r\n", stats->RxElapsed);
    xil_printf("Total cycles: %u\r\n", stats->TotalElapsed);

    // ------------------------------------------------------------------
    // Step 10: Send results back to host PC over UART
    // ------------------------------------------------------------------
    xil_printf("Output received: %d x %d\r\n", num_samples, NUM_OUTPUTS);
    SendCSVResults(DestinationBuffer, num_samples, NUM_OUTPUTS);

    return XST_SUCCESS;
}

// =========================================================================
// ReceiveCSVFixed
// Receive exactly TotalElements comma/newline separated integers over UART.
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

                Buffer[count++] = (u32)atoi(msg);
                msg_idx = 0;
            }
        } else {
            if (msg_idx < (int)(sizeof(msg) - 1))
                msg[msg_idx++] = RecvChar;
        }
    }

    xil_printf("Receive complete: %d values\r\n", count);
    return XST_SUCCESS;
}

// =========================================================================
// ReceiveCSVRows
// Receive an unknown number of rows (each row has `cols` values) over UART.
// Stops when it receives a line containing only "END_OF_DATA".
//
// The host PC should send:
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
    int  msg_idx   = 0;
    int  col       = 0;     // current column within the row being received
    int  row       = 0;     // number of complete rows received so far
    char RecvChar;

    *rows_received = 0;

    while (row < max_rows) {
        RecvChar = XUartPs_RecvByte(XPAR_XUARTPS_0_BASEADDR);

        if (RecvChar == '\r') continue;

        if (RecvChar == ',' || RecvChar == '\n') {
            if (msg_idx > 0) {
                msg[msg_idx] = '\0';
                msg_idx = 0;

                // Check for termination / end-of-data tokens
                if (strcmp(msg, TERMINATE_TOKEN) == 0) {
                    xil_printf("Termination command received.\r\n");
                    SendStats(stats);
                    *no_fail = true;
                    return XST_FAILURE;
                }

                if (strcmp(msg, "END_OF_DATA") == 0) {
                    // End of X stream — row count is finalised
                    *rows_received = row;
                    xil_printf("END_OF_DATA received after %d rows.\r\n", row);
                    return (row > 0) ? XST_SUCCESS : XST_FAILURE;
                }

                // Store the value
                Buffer[row * cols + col] = (u32)atoi(msg);
                col++;

                // When we have collected a full row, advance to the next
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

    // Reached max_rows without seeing END_OF_DATA
    xil_printf("Warning: MAX_SAMPLES (%d) reached without END_OF_DATA\r\n", max_rows);
    *rows_received = row;
    return XST_SUCCESS;
}

// =========================================================================
// WaitDmaDone — poll until a DMA channel is no longer busy
// =========================================================================
static int WaitDmaDone(XAxiDma *DmaInstancePtr, int direction, int timeout_us)
{
    while (timeout_us--) {
        if (!XAxiDma_Busy(DmaInstancePtr, direction))
            return XST_SUCCESS;
        usleep(1U);
    }
    return XST_FAILURE;
}

// =========================================================================
// SendCSVResults — send output predictions back to the host PC over UART
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
    if (!CfgPtr) { xil_printf("No DMA config found\r\n"); return XST_FAILURE; }

    Status = XAxiDma_CfgInitialize(DmaInstancePtr, CfgPtr);
    if (Status != XST_SUCCESS) { xil_printf("DMA init failed\r\n"); return XST_FAILURE; }

    if (XAxiDma_HasSg(DmaInstancePtr)) {
        xil_printf("DMA is in SG mode, expected simple mode\r\n");
        return XST_FAILURE;
    }

    XAxiDma_IntrDisable(DmaInstancePtr, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrDisable(DmaInstancePtr, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);

    Status = XAxiDma_Selftest(DmaInstancePtr);
    if (Status != XST_SUCCESS) { xil_printf("DMA self-test failed\r\n"); return XST_FAILURE; }

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
    if (Status != XST_SUCCESS) { xil_printf("Timer init failed\r\n"); return XST_FAILURE; }

    XTmrCtr_SetOptions(TmrCtrInstancePtr, TmrCtrNumber, 0);
    XTmrCtr_SetResetValue(TmrCtrInstancePtr, TmrCtrNumber, 0);

    Status = XTmrCtr_SelfTest(TmrCtrInstancePtr, TmrCtrNumber);
    if (Status != XST_SUCCESS) { xil_printf("Timer self-test failed\r\n"); return XST_FAILURE; }

    return XST_SUCCESS;
}