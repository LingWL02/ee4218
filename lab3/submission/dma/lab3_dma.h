/******************************************************************************
* Copyright (C) 2013 - 2022 Xilinx, Inc.  All rights reserved.
* Copyright (c) 2022 - 2023 Advanced Micro Devices, Inc. All Rights Reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/

#ifndef LAB3_DMA_H
#define LAB3_DMA_H

#include "xparameters.h"
#include "xil_exception.h"
#include "xil_cache.h"
#include "xstatus.h"
#include "stdlib.h"
#include "xtmrctr.h"
#include "xuartps.h"
#include "xaxidma.h"
#include "xparameters.h"
#include "xdebug.h"
#include "sleep.h"
#include "stdio.h"
#include "stdbool.h"

#ifdef XPAR_UARTNS550_0_BASEADDR
#include "xuartns550_l.h"
#endif

/* Suppress xil_printf unless -DDEBUG is passed at compile time */
#ifndef ENABLE_PRINTF
#define xil_printf(...) do {} while(0)
#endif

/* ----- DMA Definitions ----- */
#ifndef SDT
#define DMA_DEV_ID		XPAR_AXIDMA_0_DEVICE_ID

#ifdef XPAR_AXI_7SDDR_0_S_AXI_BASEADDR
#define DDR_BASE_ADDR		XPAR_AXI_7SDDR_0_S_AXI_BASEADDR
#elif defined (XPAR_MIG7SERIES_0_BASEADDR)
#define DDR_BASE_ADDR	XPAR_MIG7SERIES_0_BASEADDR
#elif defined (XPAR_MIG_0_C0_DDR4_MEMORY_MAP_BASEADDR)
#define DDR_BASE_ADDR	XPAR_MIG_0_C0_DDR4_MEMORY_MAP_BASEADDR
#elif defined (XPAR_PSU_DDR_0_S_AXI_BASEADDR)
#define DDR_BASE_ADDR	XPAR_PSU_DDR_0_S_AXI_BASEADDR
#endif

#else

#ifdef XPAR_MEM0_BASEADDRESS
#define DDR_BASE_ADDR		XPAR_MEM0_BASEADDRESS
#endif
#endif

#ifndef DDR_BASE_ADDR
#warning CHECK FOR THE VALID DDR ADDRESS IN XPARAMETERS.H, \
DEFAULT SET TO 0x01000000
#define MEM_BASE_ADDR		0x01000000
#else
#define MEM_BASE_ADDR		(DDR_BASE_ADDR + 0x1000000)
#endif

#define TX_BUFFER_BASE		(MEM_BASE_ADDR + 0x00100000)
#define RX_BUFFER_BASE		(MEM_BASE_ADDR + 0x00300000)
// #define RX_BUFFER_HIGH		(MEM_BASE_ADDR + 0x004FFFFF)

#define TX_PKT_LEN		0x820 // 520 words, 2080 bytes
#define RX_PKT_LEN		0x100 // 64 words, 256 bytes

#define TEST_START_VALUE	0xC

#define NUMBER_OF_TRANSFERS	10
#define POLL_TIMEOUT_COUNTER    1000000U

/* ----- Timer Definitions ----- */
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
#define TX_ELEMENTS    (MatrixA_Size + MatrixB_Size)
#define RX_ELEMENTS    (MATRIX_A_ROWS * MATRIX_B_COLS)

/* ----- Timing stats struct ----- */
typedef struct {
    u32 TxElapsed;
    u32 RxElapsed;
    u32 MatMulElapsed;
    u32 TotalElapsed;
} Stats;

/* ----- Function declarations ----- */
int RunMatrixAssignment(XAxiDma *DmaInstancePtr, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats);

#ifndef SDT
int InitDMA(XAxiDma *DmaInstancePtr, u16 DmaDeviceId);
int InitTmrCtr(XTmrCtr *TmrCtrInstancePtr, u16 TmrCtrDeviceId, u8 TmrCtrNumber);
#else
int InitDMA(XAxiDma *DmaInstancePtr, UINTPTR DmaBaseAddress);
int InitTmrCtr(XTmrCtr *TmrCtrInstancePtr, UINTPTR TmrCtrBaseAddress, u8 TmrCtrNumber);
#endif

void FlushDCaches(u32 *SourceAddr, u32 *DestinationAddr);

int TxSend(
    XAxiDma *DmaInstancePtr, u32  *SourceAddr, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats
);

int RxReceive (
    XAxiDma *DmaInstancePtr, u32* DestinationAddr, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats
);

int ReceiveCSVData(u32 *Buffer, int TotalElements, Stats *stats);
void SendCSVResults(u32 *data, int rows, int cols);
void SendStats(Stats *stats);

void MergeArrays(u32 *dest, u32 *A, int sizeA, u32 *B, int sizeB);


#endif /* LAB3_DMA_H */
