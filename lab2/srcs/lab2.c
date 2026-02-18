/******************************************************************************
* Copyright (C) 2013 - 2022 Xilinx, Inc.  All rights reserved.
* Copyright (c) 2022 - 2023 Advanced Micro Devices, Inc. All Rights Reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/

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

#ifndef SDT
#define FIFO_DEV_ID	   	XPAR_AXI_FIFO_0_DEVICE_ID
#endif

#define WORD_SIZE 4

#define MATRIX_A_ROWS 64
#define MATRIX_A_COLS 8
#define MATRIX_B_ROWS 8
#define MATRIX_B_COLS 1

#define TOTAL_ELEMENTS (MATRIX_A_ROWS * MATRIX_A_COLS) + (MATRIX_B_ROWS * MATRIX_B_COLS)
#define MatrixA_Size (MATRIX_A_COLS * MATRIX_A_ROWS)
#define MatrixB_Size (MATRIX_B_COLS * MATRIX_B_ROWS)
u32 MatrixA[MatrixA_Size];
u32 MatrixB[MatrixB_Size];

// Timer related definitions
/*
 * The following constants map to the XPAR parameters created in the
 * xparameters.h file. They are only defined here such that a user can easily
 * change all the needed parameters in one place.
*/
#ifndef SDT
#define TMRCTR_DEVICE_ID	XPAR_TMRCTR_0_DEVICE_ID
#else
#define XTMRCTR_BASEADDRESS	XPAR_XTMRCTR_0_BASEADDR
#endif
/*
 * The 1st counter
 */
#define TIMER_COUNTER_0	 0
XTmrCtr TmrCtrInstance; /* The instance of the Tmrctr Device */

#undef DEBUG

#ifndef SDT
int RunMatrixAssignment(
	XLlFifo *FifoInstancePtr, u16 FifoDeviceId, XTmrCtr *TmrCtrInstancePtr, u16 TmrCtrDeviceId, u8 TmrCtrNumber
);
#else
int RunMatrixAssignment(
	XLlFifo *FifoInstancePtr, UINTPTR FifoBaseAddress, XTmrCtr *TmrCtrInstancePtr, UINTPTR TmrCtrBaseAddress, u8 TmrCtrNumber
);
#endif

int TimerSetup(XTmrCtr *TmrCtrInstancePtr, u16 TmrCtrDeviceId);

void MatrixMultiply(u32 *A, u32 *B, u32 *RES);

int TxSend(XLlFifo *FifoInstancePtr, u32 *SourceAddr, int Words, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber);
int RxReceive(XLlFifo *FifoInstancePtr, u32 *DestinationAddr, int Words, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber);

int ReceiveCSVData(u32 *BufferA, int TotalElements);
void SendCSVResults(u32 *RES, int TotalElements);

void MergeArrays(u32 *dest, u32 *A, int sizeA, u32 *B, int sizeB);
int performMatrixMultiplication(u32 *data, int A_rows, int A_cols, int B_cols, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber);

XLlFifo FifoInstance;

u32 SourceBuffer[TOTAL_ELEMENTS];
u32 DestinationBuffer[TOTAL_ELEMENTS];

char TERMINATE_TOKEN[] = "TERMINATE";

int main()
{
	int Status = XST_SUCCESS;

	while (true) {
		#ifndef SDT
		Status = RunMatrixAssignment(&FifoInstance, FIFO_DEV_ID, &TmrCtrInstance, TMRCTR_DEVICE_ID, TIMER_COUNTER_0);
		#else
		Status = RunMatrixAssignment(&FifoInstance, XPAR_XLLFIFO_0_BASEADDR, &TmrCtrInstance, XTMRCTR_BASEADDRESS, TIMER_COUNTER_0);
		#endif
		if (Status != XST_SUCCESS) {
			xil_printf("Failed to execute\r\n");
			xil_printf("--- Exiting main() ---\r\n");
			return XST_FAILURE;
		}
	}

	return Status;
}

#ifndef SDT
int RunMatrixAssignment(
	XLlFifo *FifoInstancePtr, u16 FifoDeviceId, XTmrCtr *TmrCtrInstancePtr, u16 TmrCtrDeviceId, u8 TmrCtrNumber
)
#else
int RunMatrixAssignment(
	XLlFifo *FifoInstancePtr, UINTPTR FifoBaseAddress, XTmrCtr *TmrCtrInstancePtr, UINTPTR TmrCtrBaseAddress, u8 TmrCtrNumber
)
#endif
{
	XLlFifo_Config *FifoConfig;
	int Status;
	Status = XST_SUCCESS;

#ifdef XPAR_UARTNS550_0_BASEADDR

	Uart550_Setup();

#endif

// Initialize Fifo
#ifndef SDT
	FifoConfig = XLlFfio_LookupConfig(FifoDeviceId);
#else
	FifoConfig = XLlFfio_LookupConfig(FifoBaseAddress);
#endif
	if (!FifoConfig) {
#ifndef SDT
		xil_printf("No config found for %d\r\n", FifoDeviceId);
#endif
		return XST_FAILURE;
	}

	Status = XLlFifo_CfgInitialize(FifoInstancePtr, FifoConfig, FifoConfig->BaseAddress);
	if (Status != XST_SUCCESS) {
		xil_printf("Initialization failed\r\n");
		return Status;
	}

	Status = XLlFifo_Status(FifoInstancePtr);
	XLlFifo_IntClear(FifoInstancePtr, 0xffffffff);
	Status = XLlFifo_Status(FifoInstancePtr);
	if(Status != 0x0) {
		xil_printf("\n ERROR : Reset value of ISR0 : 0x%x\t"
		    "Expected : 0x0\r\n",
			    XLlFifo_Status(FifoInstancePtr));
		return XST_FAILURE;
	}

	// Initialize Timer
#ifndef SDT
	Status = XTmrCtr_Initialize(TmrCtrInstancePtr, TmrCtrDeviceId);
#else
	Status = XTmrCtr_Initialize(TmrCtrInstancePtr, TmrCtrBaseAddress);
#endif
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	/*
	 * Perform a self-test to ensure that the hardware was built
	 * correctly, use the 1st timer in the device (0)
	 */
	Status = XTmrCtr_SelfTest(TmrCtrInstancePtr, TmrCtrNumber);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	// Default timer settings (count up, no auto reload, interrupt disabled)
	XTmrCtr_SetOptions(TmrCtrInstancePtr, TmrCtrNumber, 0);
	XTmrCtr_SetResetValue(TmrCtrInstancePtr, TmrCtrNumber, 0);

	xil_printf("Ready! Please use RealTerm -> 'Send File' to send A.csv\r\n");
    Status = ReceiveCSVData(MatrixA, MatrixA_Size);
    if (Status != XST_SUCCESS) {
        xil_printf("Failed to receive Matrix A\r\n");
        return XST_FAILURE;
    }
    xil_printf("Matrix A Received. Now please send B.csv\r\n");
    Status = ReceiveCSVData(MatrixB, MatrixB_Size);
    if (Status != XST_SUCCESS) {
        xil_printf("Failed to receive Matrix B\r\n");
        return XST_FAILURE;
    }
	if(Status == XST_SUCCESS){
		xil_printf("All data received successfully!\r\n");
		xil_printf("Merging data into SourceBuffer...\r\n");
		MergeArrays(SourceBuffer, MatrixA, MatrixA_Size, MatrixB, MatrixB_Size);
	}


	Status = TxSend(FifoInstancePtr, SourceBuffer, TOTAL_ELEMENTS, TmrCtrInstancePtr, TmrCtrNumber);
	if (Status != XST_SUCCESS){
		xil_printf("Transmission of Data failed\r\n");
		return XST_FAILURE;
	}

	Status = RxReceive(FifoInstancePtr, DestinationBuffer, TOTAL_ELEMENTS, TmrCtrInstancePtr, TmrCtrNumber);
	if (Status != XST_SUCCESS){
		xil_printf("Receiving data failed");
		return XST_FAILURE;
	}
	Status = performMatrixMultiplication(DestinationBuffer, MATRIX_A_ROWS, MATRIX_A_COLS, MATRIX_B_COLS, TmrCtrInstancePtr, TmrCtrNumber);

	if (Status != XST_SUCCESS) {
		xil_printf("Matrix multiplication failed\r\n");
		return XST_FAILURE;
	}
	xil_printf("Matrix multiplication completed successfully!, output is a %dx%d matrix\r\n", MATRIX_A_ROWS, MATRIX_B_COLS);

	SendCSVResults(DestinationBuffer, MATRIX_A_ROWS * MATRIX_B_COLS);

	return Status;
}


int TxSend(XLlFifo *FifoInstancePtr, u32  *SourceAddr, int Words, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber)
{
	int i;
	xil_printf("Transmitting Data ...\r\n");

	XTmrCtr_Reset(TmrCtrInstancePtr, TmrCtrNumber);
	XTmrCtr_Start(TmrCtrInstancePtr, TmrCtrNumber);

	for (i=0 ; i < Words ; i++){
		if( XLlFifo_iTxVacancy(FifoInstancePtr) ){
			XLlFifo_TxPutWord(FifoInstancePtr, SourceAddr[i]);
		}
	}

	XLlFifo_iTxSetLen(FifoInstancePtr, (Words * WORD_SIZE));

	while( !(XLlFifo_IsTxDone(FifoInstancePtr)) ){

	}

	u32 TxElapsed = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);
	XTmrCtr_Stop(TmrCtrInstancePtr, TmrCtrNumber);
	xil_printf("TxSend elapsed: %u cycles\r\n", TxElapsed);

	return XST_SUCCESS;
}


int RxReceive (XLlFifo *FifoInstancePtr, u32* DestinationAddr, int Words, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber)
{
	int Status;
	u32 RxWord;
	int count = 0;

	xil_printf("Receiving data ...\r\n");

	XTmrCtr_Reset(TmrCtrInstancePtr, TmrCtrNumber);
	XTmrCtr_Start(TmrCtrInstancePtr, TmrCtrNumber);

	while (count < Words) {
		if(XLlFifo_iRxOccupancy(FifoInstancePtr)) {
			RxWord = XLlFifo_RxGetWord(FifoInstancePtr);
			DestinationAddr[count] = RxWord;
			count++;
		}
	}

	u32 RxElapsed = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);
	XTmrCtr_Stop(TmrCtrInstancePtr, TmrCtrNumber);
	xil_printf("RxReceive elapsed: %u cycles\r\n", RxElapsed);

	Status = XLlFifo_IsRxDone(FifoInstancePtr);
	if(Status != TRUE){
		xil_printf("Failing in receive complete ...\r\n");
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}


int ReceiveCSVData(u32 *Buffer, int TotalElements)
{
    char msg[20];
    int msg_idx = 0;
    int count = 0;
    char RecvChar;

    xil_printf("Receiving CSV data for %d elements...\r\n", TotalElements);

    while (count < TotalElements) {
        RecvChar = XUartPs_RecvByte(XPAR_XUARTPS_0_BASEADDR);

        if (RecvChar == '\r') {
            continue;
        }

        if (RecvChar == ',' || RecvChar == '\n') {
            if (msg_idx > 0) {
                msg[msg_idx] = '\0';

				if (strcmp(msg, TERMINATE_TOKEN) == 0) {
					xil_printf("Termination command received. Stopping reception.\r\n");
					return XST_FAILURE;
				}
                Buffer[count] = atoi(msg);
                count++;
                msg_idx = 0;

                if (count % 64 == 0) {
                    xil_printf("Progress: %d/%d values received\r\n", count, TotalElements);
                }
            }
        }
        else {  // Risky but required to capture "TERMINATE"
            if (msg_idx < 19) {
                msg[msg_idx++] = RecvChar;
            }
        }
    }

    xil_printf("CSV data reception complete. %d values received.\r\n", count);

	if (count != TotalElements) {
        xil_printf("ERROR: Expected %d elements but received %d!\r\n", TotalElements, count);
        xil_printf("Please check your CSV file and resend.\r\n");
		return XST_FAILURE;
    } else {
        xil_printf("All %d elements received successfully.\r\n", TotalElements);
		return XST_SUCCESS;
    }
	return XST_SUCCESS;
}


void MergeArrays(u32 *dest, u32 *A, int sizeA, u32 *B, int sizeB)
{
    for (int i = 0; i < sizeA; i++) {
        dest[i] = A[i];
    }
    for (int i = 0; i < sizeB; i++) {
        dest[sizeA + i] = B[i];
    }
}


int performMatrixMultiplication(u32 *data, int A_rows, int A_cols, int B_cols, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber)
{
	u32 *A = data;
	u32 *B = data + (A_rows * A_cols);
	u32 RES[A_rows * B_cols];

	for (int i = 0; i < A_rows * B_cols; i++) {
		RES[i] = 0;
	}

	XTmrCtr_Reset(TmrCtrInstancePtr, TmrCtrNumber);
	XTmrCtr_Start(TmrCtrInstancePtr, TmrCtrNumber);

	for (int i = 0; i < A_rows; i++) {
		for (int j = 0; j < B_cols; j++) {
			for (int k = 0; k < A_cols; k++) {
				RES[i * B_cols + j] += (A[i * A_cols + k] * B[k * B_cols + j]) >> 8;
			}
		}
	}

	u32 MatMulElapsed = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);
	XTmrCtr_Stop(TmrCtrInstancePtr, TmrCtrNumber);
	xil_printf("MatMul elapsed: %u cycles\r\n", MatMulElapsed);

	for (int i = 0; i < A_rows * B_cols; i++) {
		DestinationBuffer[i] = RES[i] & 0xFF;
	}

	return XST_SUCCESS;
}


void SendCSVResults(u32 *data, int size)
{
	for (int i = 0; i < size; i++) {
		char buffer[12];
		sprintf(buffer, "%d", data[i]);
		for (char *p = buffer; *p != '\0'; p++) {
			XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, *p);
		}
		if (i < size - 1) {
			XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, ',');
		}
	}
	XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, '\r');
	XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, '\n');
}
