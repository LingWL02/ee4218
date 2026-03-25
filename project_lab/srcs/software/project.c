/******************************************************************************
* MLP Hardware Accelerator - Single File Implementation
******************************************************************************/

#include "xparameters.h"
#include "xil_printf.h"
#include "xllfifo.h"
#include "xtmrctr.h"
#include "xuartps_hw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "project.h"

/************************** GLOBALS *********************************/

XLlFifo FifoInstance;
XTmrCtr TmrCtrInstance;

u32 X_Buffer[X_SIZE];
u32 W_Hid_Buffer[W_HID_SIZE];
u32 W_Out_Buffer[W_OUT_SIZE];

u32 SourceBuffer[TOTAL_ELEMENTS];
u32 DestinationBuffer[BATCH_SIZE];
u32 Y_soft[BATCH_SIZE];

u32 sigmoid_lut[256] = {
    12,12,12,12,13,13,13,14,14,14,15,15,15,16,16,16,17,17,18,18,18,19,19,20,20,21,21,21,22,22,23,23,24,24,25,26,26,27,27,28,28,29,30,30,31,32,32,33,34,34,35,36,36,37,38,39,39,40,41,42,43,44,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,66,67,68,69,70,72,73,74,75,76,78,79,80,82,83,84,86,87,88,90,91,92,94,95,97,98,99,101,102,104,105,107,108,110,111,113,114,116,117,119,120,122,123,125,126,128,129,130,132,133,135,136,138,139,141,142,144,145,147,148,150,151,153,154,156,157,158,160,161,163,164,165,167,168,169,171,172,173,175,176,177,179,180,181,182,183,185,186,187,188,189,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,211,212,213,214,215,216,216,217,218,219,219,220,221,221,222,223,223,224,225,225,226,227,227,228,228,229,229,230,231,231,232,232,233,233,234,234,234,235,235,236,236,237,237,237,238,238,239,239,239,240,240,240,241,241,241,242,242,242,243,243,243
};

char TERMINATE_TOKEN[] = "TERMINATE";

/************************** MAIN *********************************/

int main()
{
    int Status;
    Stats stats = {0};

    xil_printf("MLP Accelerator Program Start\r\n");

    Status = RunMLP(&FifoInstance, &TmrCtrInstance, TIMER_COUNTER_0, &stats);

    if (Status != XST_SUCCESS) {
        xil_printf("Execution Failed\r\n");
        return XST_FAILURE;
    }

    xil_printf("Execution Successful\r\n");
    return 0;
}

/************************** MAIN LOGIC *********************************/

int RunMLP(XLlFifo *FifoInstancePtr, XTmrCtr *TmrCtrInstancePtr,
           u8 TmrCtrNumber, Stats *stats)
{
    int Status;

    // Initialize FIFO
    XLlFifo_Config *FifoConfig;
    FifoConfig = XLlFfio_LookupConfig(XPAR_XLLFIFO_0_DEVICE_ID);

    if (!FifoConfig) return XST_FAILURE;

    Status = XLlFifo_CfgInitialize(FifoInstancePtr, FifoConfig,
                                   FifoConfig->BaseAddress);
    if (Status != XST_SUCCESS) return XST_FAILURE;

    // Initialize Timer
    XTmrCtr_Initialize(TmrCtrInstancePtr, XPAR_TMRCTR_0_DEVICE_ID);
    XTmrCtr_SetOptions(TmrCtrInstancePtr, TmrCtrNumber, 0);

    /************ RECEIVE DATA ************/
    xil_printf("Send X.csv\r\n");
    ReceiveCSVData(X_Buffer, X_SIZE);

    xil_printf("Send w_hid.csv\r\n");
    ReceiveCSVData(W_Hid_Buffer, W_HID_SIZE);

    xil_printf("Send w_out.csv\r\n");
    ReceiveCSVData(W_Out_Buffer, W_OUT_SIZE);

    xil_printf("All data received!\r\n");

    /************ SOFT EXECUTION ************/
    XTmrCtr_Reset(TmrCtrInstancePtr, TmrCtrNumber);
    XTmrCtr_Start(TmrCtrInstancePtr, TmrCtrNumber);

    mlp_soft(X_Buffer, W_Hid_Buffer, W_Out_Buffer, Y_soft);

    stats->SoftElapsed = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);
    XTmrCtr_Stop(TmrCtrInstancePtr, TmrCtrNumber);

    xil_printf("SOFT Time: %u cycles\r\n", stats->SoftElapsed);

    /************ HARD EXECUTION ************/
    MergeMLPData(SourceBuffer);

    TxSend(FifoInstancePtr, SourceBuffer, TOTAL_ELEMENTS,
           TmrCtrInstancePtr, TmrCtrNumber, stats);

    RxReceive(FifoInstancePtr, DestinationBuffer, BATCH_SIZE,
              TmrCtrInstancePtr, TmrCtrNumber, stats);

    xil_printf("HARD execution done\r\n");

    /************ COMPARE ************/
    CompareResults(Y_soft, DestinationBuffer);

    return XST_SUCCESS;
}

/************************** MLP SOFT *********************************/

void mlp_soft(u32 *X, u32 *W_hid, u32 *W_out, u32 *Y)
{
    for (int n = 0; n < BATCH_SIZE; n++) {

        u32 hidden[N_HIDDEN];

        for (int j = 0; j < N_HIDDEN; j++) {

            u32 acc = W_hid[j] * SCALE;

            for (int i = 0; i < N_INPUT; i++) {
                u32 product = X[n*N_INPUT + i] *
                              W_hid[(i+1)*N_HIDDEN + j];
                product >>= 8;
                acc += product;
            }

            if (acc > 255) acc = 255;
            hidden[j] = sigmoid(acc);
        }

        u32 acc = W_out[0] * SCALE;

        for (int j = 0; j < N_HIDDEN; j++) {
            u32 product = hidden[j] * W_out[j+1];
            product >>= 8;
            acc += product;
        }

        if (acc > 255) acc = 255;
        Y[n] = acc;
    }
}

/************************** SIGMOID *********************************/

u32 sigmoid(u32 x)
{
    if (x > 255) x = 255;
    return sigmoid_lut[x];
}

/************************** DATA MERGE *********************************/

void MergeMLPData(u32 *dest)
{
    int idx = 0;

    for (int i = 0; i < X_SIZE; i++)
        dest[idx++] = X_Buffer[i];

    for (int i = 0; i < W_HID_SIZE; i++)
        dest[idx++] = W_Hid_Buffer[i];

    for (int i = 0; i < W_OUT_SIZE; i++)
        dest[idx++] = W_Out_Buffer[i];
}

/************************** FIFO TX *********************************/

int TxSend(XLlFifo *FifoInstancePtr, u32 *SourceAddr, int Words,
           XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats)
{
    XTmrCtr_Reset(TmrCtrInstancePtr, TmrCtrNumber);
    XTmrCtr_Start(TmrCtrInstancePtr, TmrCtrNumber);

    for (int i = 0; i < Words; i++) {
        if (XLlFifo_iTxVacancy(FifoInstancePtr)) {
            XLlFifo_TxPutWord(FifoInstancePtr, SourceAddr[i]);
        }
    }

    XLlFifo_iTxSetLen(FifoInstancePtr, Words * WORD_SIZE);

    while (!XLlFifo_IsTxDone(FifoInstancePtr));

    stats->TxElapsed = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);
    XTmrCtr_Stop(TmrCtrInstancePtr, TmrCtrNumber);

    xil_printf("TX Time: %u cycles\r\n", stats->TxElapsed);

    return XST_SUCCESS;
}

/************************** FIFO RX *********************************/

int RxReceive(XLlFifo *FifoInstancePtr, u32 *DestinationAddr, int Words,
              XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats)
{
    int count = 0;

    XTmrCtr_Reset(TmrCtrInstancePtr, TmrCtrNumber);
    XTmrCtr_Start(TmrCtrInstancePtr, TmrCtrNumber);

    while (count < Words) {
        if (XLlFifo_iRxOccupancy(FifoInstancePtr)) {
            DestinationAddr[count++] =
                XLlFifo_RxGetWord(FifoInstancePtr);
        }
    }

    stats->RxElapsed = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);
    XTmrCtr_Stop(TmrCtrInstancePtr, TmrCtrNumber);

    xil_printf("RX Time: %u cycles\r\n", stats->RxElapsed);

    return XST_SUCCESS;
}

/************************** CSV INPUT *********************************/

int ReceiveCSVData(u32 *Buffer, int TotalElements)
{
    char msg[20];
    int idx = 0, count = 0;
    char c;

    while (count < TotalElements) {

        c = XUartPs_RecvByte(XPAR_XUARTPS_0_BASEADDR);

        if (c == ',' || c == '\n') {
            msg[idx] = '\0';
            Buffer[count++] = atoi(msg);
            idx = 0;
        } else if (idx < 19) {
            msg[idx++] = c;
        }
    }

    xil_printf("Received %d values\r\n", count);
    return XST_SUCCESS;
}

/************************** COMPARE *********************************/

void CompareResults(u32 *soft, u32 *hard)
{
    int correct = 0;

    for (int i = 0; i < BATCH_SIZE; i++) {
        if (soft[i] == hard[i]) correct++;
    }

    xil_printf("Accuracy: %d / %d\r\n", correct, BATCH_SIZE);
}