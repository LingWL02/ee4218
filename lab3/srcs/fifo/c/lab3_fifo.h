/******************************************************************************
* Copyright (C) 2013 - 2022 Xilinx, Inc.  All rights reserved.
* Copyright (c) 2022 - 2023 Advanced Micro Devices, Inc. All Rights Reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/

#ifndef LAB3_FIFO_H
#define LAB3_FIFO_H

#include "xparameters.h"
#include "xil_exception.h"
#include "xstreamer.h"
#include "xil_cache.h"
#include "xllfifo.h"
#include "xstatus.h"
#include "stdlib.h"
#include "xtmrctr.h"
#include "xuartps.h"
#include "stdio.h"
#include "stdbool.h"

#ifdef XPAR_UARTNS550_0_BASEADDR
#include "xuartns550_l.h"
#endif

/* Suppress xil_printf unless -DDEBUG is passed at compile time */
#ifndef ENABLE_PRINTF
#define xil_printf(...) do {} while(0)
#endif

/* ----- FIFO / Timer device IDs ----- */
#ifndef SDT
#define FIFO_DEV_ID         XPAR_AXI_FIFO_0_DEVICE_ID
#endif

#ifndef SDT
#define TMRCTR_DEVICE_ID    XPAR_TMRCTR_0_DEVICE_ID
#else
#define XTMRCTR_BASEADDRESS XPAR_XTMRCTR_0_BASEADDR
#endif

#define TIMER_COUNTER_0     0
#define WORD_SIZE           4

/* ----- Matrix dimensions ----- */
#define MATRIX_A_ROWS 64
#define MATRIX_A_COLS 8
#define MATRIX_B_ROWS 8
#define MATRIX_B_COLS 1

#define MatrixA_Size    (MATRIX_A_COLS * MATRIX_A_ROWS)
#define MatrixB_Size    (MATRIX_B_COLS * MATRIX_B_ROWS)
#define FIFO_TX_ELEMENTS    (MatrixA_Size + MatrixB_Size)
#define FIFO_RX_ELEMENTS    (MATRIX_A_ROWS * MATRIX_B_COLS)

/* ----- Timing stats struct ----- */
typedef struct {
    u32 TxElapsed;
    u32 RxElapsed;
    u32 MatMulElapsed;
    u32 TotalElapsed;
} Stats;

/* ----- Function declarations ----- */
#ifndef SDT
int InitFifo(XLlFifo *FifoInstancePtr, u16 FifoDeviceId);
int InitTmrCtr(XTmrCtr *TmrCtrInstancePtr, u16 TmrCtrDeviceId, u8 TmrCtrNumber);
#else
int InitFifo(XLlFifo *FifoInstancePtr, UINTPTR FifoBaseAddress);
int InitTmrCtr(XTmrCtr *TmrCtrInstancePtr, UINTPTR TmrCtrBaseAddress, u8 TmrCtrNumber);
#endif

int RunMatrixAssignment(
    XLlFifo *FifoInstancePtr,
    XTmrCtr *TmrCtrInstancePtr,
    u8 TmrCtrNumber, Stats *stats
);

int TxSend(XLlFifo *FifoInstancePtr, u32 *SourceAddr, int Words,
           XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats);

int RxReceive(XLlFifo *FifoInstancePtr, u32 *DestinationAddr, int Words,
              XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats);

int ReceiveCSVData(u32 *Buffer, int TotalElements, Stats *stats);
void SendCSVResults(u32 *data, int rows, int cols);
void SendStats(Stats *stats);

void MergeArrays(u32 *dest, u32 *A, int sizeA, u32 *B, int sizeB);

#endif /* LAB3_FIFO_H */
