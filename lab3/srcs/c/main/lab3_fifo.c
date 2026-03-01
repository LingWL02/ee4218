/******************************************************************************
* Copyright (C) 2013 - 2022 Xilinx, Inc.  All rights reserved.
* Copyright (c) 2022 - 2023 Advanced Micro Devices, Inc. All Rights Reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/

#include "lab3.h"

XLlFifo FifoInstance;
XTmrCtr TmrCtrInstance;

u32 MatrixA[MatrixA_Size];
u32 MatrixB[MatrixB_Size];
u32 SourceBuffer[FIFO_TX_ELEMENTS];
u32 DestinationBuffer[FIFO_RX_ELEMENTS];

char TERMINATE_TOKEN[] = "TERMINATE";

int main()
{
	int Status = XST_SUCCESS;
	Stats stats = {0, 0, 0};

	while (true) {
		#ifndef SDT
		Status = RunMatrixAssignment(&FifoInstance, FIFO_DEV_ID, &TmrCtrInstance, TMRCTR_DEVICE_ID, TIMER_COUNTER_0, &stats);
		#else
		Status = RunMatrixAssignment(&FifoInstance, XPAR_XLLFIFO_0_BASEADDR, &TmrCtrInstance, XTMRCTR_BASEADDRESS, TIMER_COUNTER_0, &stats);
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
	XLlFifo *FifoInstancePtr, u16 FifoDeviceId, XTmrCtr *TmrCtrInstancePtr, u16 TmrCtrDeviceId, u8 TmrCtrNumber, Stats *stats
)
#else
int RunMatrixAssignment(
	XLlFifo *FifoInstancePtr, UINTPTR FifoBaseAddress, XTmrCtr *TmrCtrInstancePtr, UINTPTR TmrCtrBaseAddress, u8 TmrCtrNumber, Stats *stats
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
    Status = ReceiveCSVData(MatrixA, MatrixA_Size, stats);
    if (Status != XST_SUCCESS) {
        xil_printf("Failed to receive Matrix A\r\n");
        return XST_FAILURE;
    }
    xil_printf("Matrix A Received. Now please send B.csv\r\n");
    Status = ReceiveCSVData(MatrixB, MatrixB_Size, stats);
    if (Status != XST_SUCCESS) {
        xil_printf("Failed to receive Matrix B\r\n");
        return XST_FAILURE;
    }
	if(Status == XST_SUCCESS){
		xil_printf("All data received successfully!\r\n");
		xil_printf("Merging data into SourceBuffer...\r\n");
		MergeArrays(SourceBuffer, MatrixA, MatrixA_Size, MatrixB, MatrixB_Size);
	}


	Status = TxSend(FifoInstancePtr, SourceBuffer, FIFO_TX_ELEMENTS, TmrCtrInstancePtr, TmrCtrNumber, stats);
	if (Status != XST_SUCCESS){
		xil_printf("Transmission of Data failed\r\n");
		return XST_FAILURE;
	}

	Status = RxReceive(FifoInstancePtr, DestinationBuffer, FIFO_RX_ELEMENTS, TmrCtrInstancePtr, TmrCtrNumber, stats);
	if (Status != XST_SUCCESS){
		xil_printf("Receiving data failed");
		return XST_FAILURE;
	}

	xil_printf("Data received successfully!, output is a %dx%d matrix\r\n", MATRIX_A_ROWS, MATRIX_B_COLS);

	SendCSVResults(DestinationBuffer, MATRIX_A_ROWS, MATRIX_B_COLS);

	return Status;
}


int TxSend(XLlFifo *FifoInstancePtr, u32  *SourceAddr, int Words, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats)
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
	// Do not stop the timer, we want to measure how long the MM takes as well.
	// XTmrCtr_Stop(TmrCtrInstancePtr, TmrCtrNumber);
	stats->TxElapsed = TxElapsed;
	xil_printf("TxSend elapsed: %u cycles\r\n", TxElapsed);

	return XST_SUCCESS;
}


int RxReceive (XLlFifo *FifoInstancePtr, u32* DestinationAddr, int Words, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats)
{
	int Status;
	u32 RxWord;
	int count = 0;

	xil_printf("Receiving data ...\r\n");

	// XTmrCtr_Reset(TmrCtrInstancePtr, TmrCtrNumber);
	// XTmrCtr_Start(TmrCtrInstancePtr, TmrCtrNumber);
	u32 MatMulElapsed = 0;

	while (count < Words) {
		if(XLlFifo_iRxOccupancy(FifoInstancePtr)) {
			MatMulElapsed = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);
			RxWord = XLlFifo_RxGetWord(FifoInstancePtr);
			DestinationAddr[count] = RxWord;
			count++;
		}
	}

	u32 RxElapsed = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);
	XTmrCtr_Stop(TmrCtrInstancePtr, TmrCtrNumber);
	stats->MatMulElapsed = MatMulElapsed - stats->TxElapsed;
	stats->RxElapsed = RxElapsed - MatMulElapsed;
	xil_printf("RxReceive elapsed: %u cycles\r\n", RxElapsed);

	Status = XLlFifo_IsRxDone(FifoInstancePtr);
	if(Status != TRUE){
		xil_printf("Failing in receive complete ...\r\n");
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}


int ReceiveCSVData(u32 *Buffer, int TotalElements, Stats *stats)
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
					SendStats(stats);
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


void SendStats(Stats *stats)
{
	char buf[12];
	const char *labels[] = {"STATS:TX=", ",RX=", ",MATMUL="};
	u32 values[] = {stats->TxElapsed, stats->RxElapsed, stats->MatMulElapsed};
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


void SendCSVResults(u32 *data, int rows, int cols)
{
	for (int i = 0; i < rows; i++) {
		for (int j = 0; j < cols; j++) {
			char buffer[12];
			sprintf(buffer, "%d", data[i * cols + j]);
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
