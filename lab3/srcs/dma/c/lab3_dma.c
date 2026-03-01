/******************************************************************************
* Copyright (C) 2013 - 2022 Xilinx, Inc.  All rights reserved.
* Copyright (c) 2022 - 2023 Advanced Micro Devices, Inc. All Rights Reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/

#include "lab3_dma.h"

XAxiDma DmaInstance;
XTmrCtr TmrCtrInstance;

u32 MatrixA[MatrixA_Size];
u32 MatrixB[MatrixB_Size];
u32 SourceBuffer[TX_ELEMENTS];
u32 DestinationBuffer[RX_ELEMENTS];

char TERMINATE_TOKEN[] = "TERMINATE";

int main()
{
	int Status = XST_SUCCESS;
	Stats stats = {0, 0, 0, 0};

#ifdef XPAR_UARTNS550_0_BASEADDR
	Uart550_Setup();
#endif

#ifndef SDT
    Status = InitDMA(&DmaInstance, DMA_DEV_ID);
#else
    Status = InitDMA(&DmaInstance, XPAR_XAXIDMA_0_BASEADDR);
#endif
    if (Status != XST_SUCCESS) {
        xil_printf("DMA Initialization Failed\r\n");
        return XST_FAILURE;
    }

#ifndef SDT
    Status = InitTmrCtr(&TmrCtrInstance, TMRCTR_DEVICE_ID, TIMER_COUNTER_0);
#else
    Status = InitTmrCtr(&TmrCtrInstance, XTMRCTR_BASEADDRESS, TIMER_COUNTER_0);
#endif
    if (Status != XST_SUCCESS) {
        xil_printf("Timer Initialization Failed\r\n");
        return XST_FAILURE;
    }

	xil_printf("DMA IP Implementation\r\n");
	while (true) {
		Status = RunMatrixAssignment(&DmaInstance, &TmrCtrInstance, TIMER_COUNTER_0, &stats);
		if (Status != XST_SUCCESS) {
			xil_printf("Failed to execute\r\n");
			xil_printf("--- Exiting main() ---\r\n");
			return XST_FAILURE;
		}
	}

	return Status;
}


int RunMatrixAssignment(XAxiDma *DmaInstancePtr, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats)
{
	int Status;

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

    FlushDCaches(SourceBuffer, DestinationBuffer);

	Status = TxSend(DmaInstancePtr, SourceBuffer, TmrCtrInstancePtr, TmrCtrNumber, stats);
	if (Status != XST_SUCCESS){
		xil_printf("Transmission of Data failed\r\n");
		return XST_FAILURE;
	}

	Status = RxReceive(DmaInstancePtr, DestinationBuffer, TmrCtrInstancePtr, TmrCtrNumber, stats);
	if (Status != XST_SUCCESS){
		xil_printf("Receiving data failed");
		return XST_FAILURE;
	}

	xil_printf("Data received successfully!, output is a %dx%d matrix\r\n", MATRIX_A_ROWS, MATRIX_B_COLS);

	SendCSVResults(DestinationBuffer, MATRIX_A_ROWS, MATRIX_B_COLS);

	return Status;
}


void FlushDCaches(u32 *SourceAddr, u32 *DestinationAddr)
{
    Xil_DCacheFlushRange((UINTPTR) SourceAddr, TX_PKT_LEN);
    Xil_DCacheFlushRange((UINTPTR) DestinationAddr, RX_PKT_LEN);
}


int TxSend(XAxiDma *DmaInstancePtr, u32  *SourceAddr, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats)
{
    int Status;
    int TimeOut = POLL_TIMEOUT_COUNTER;
	// Print before starting the timer to avoid affecting timing results, but still provide feedback to user
	xil_printf("Transmitting Data...\r\n");

	XTmrCtr_Reset(TmrCtrInstancePtr, TmrCtrNumber);
	XTmrCtr_Start(TmrCtrInstancePtr, TmrCtrNumber);

    Status = XAxiDma_SimpleTransfer(DmaInstancePtr, (UINTPTR) SourceAddr, TX_PKT_LEN, XAXIDMA_DMA_TO_DEVICE);
    if (Status != XST_SUCCESS) {
        xil_printf("Failed to start DMA transfer\r\n");
        return XST_FAILURE;
    }
    while (TimeOut) {
        if (!(XAxiDma_Busy(DmaInstancePtr, XAXIDMA_DMA_TO_DEVICE))) {
            break;
        }
        TimeOut--;
        usleep(1U);
    }

	u32 TxElapsed = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);

	stats->TxElapsed = TxElapsed;

	return XST_SUCCESS;
}


int RxReceive (XAxiDma *DmaInstancePtr, u32* DestinationAddr, XTmrCtr *TmrCtrInstancePtr, u8 TmrCtrNumber, Stats *stats)
{
	int Status;
    int TimeOut = POLL_TIMEOUT_COUNTER;

    u32 Elapsed = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);

    Status = XAxiDma_SimpleTransfer(DmaInstancePtr, (UINTPTR) DestinationAddr, RX_PKT_LEN, XAXIDMA_DEVICE_TO_DMA);
    if (Status != XST_SUCCESS) {
        xil_printf("Failed to start DMA receive\r\n");
        return XST_FAILURE;
    }

    while (TimeOut) {
            if (!(XAxiDma_Busy(DmaInstancePtr, XAXIDMA_DEVICE_TO_DMA))) {
            break;
        }
        TimeOut--;
        usleep(1U);
    }

    u32 TotalElapsed = XTmrCtr_GetValue(TmrCtrInstancePtr, TmrCtrNumber);
	XTmrCtr_Stop(TmrCtrInstancePtr, TmrCtrNumber);
    Xil_DCacheInvalidateRange((UINTPTR) DestinationAddr, RX_PKT_LEN);

	stats->TotalElapsed = TotalElapsed;  // Total elapsed time since Tx started, which includes MatMul and Rx
	stats->RxElapsed = TotalElapsed - Elapsed;

	// Minimal print statements to avoid affecting timing too much, but still provide feedback to user
	// Therefore only print after all data is received, and include timing stats in the output
	xil_printf("Receiving data...\r\n");
	xil_printf("TxSend elapsed: %u cycles\r\n", stats->TxElapsed);
	xil_printf("RxReceive elapsed: %u cycles\r\n", stats->RxElapsed);
	xil_printf("Total elapsed: %u cycles\r\n", stats->TotalElapsed);

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


#ifndef SDT
int InitDMA(XAxiDma *DmaInstancePtr, u16 DmaDeviceId)
#else
int InitDMA(XAxiDma *DmaInstancePtr, UINTPTR DmaBaseAddress)
#endif
{
	XAxiDma_Config *CfgPtr;
	int Status;

#ifndef SDT
	CfgPtr = XAxiDma_LookupConfig(DmaDeviceId);
	if (!CfgPtr) {
		xil_printf("No config found for %d\r\n", DmaDeviceId);
		return XST_FAILURE;
	}
#else
	CfgPtr = XAxiDma_LookupConfig(DmaBaseAddress);
	if (!CfgPtr) {
		xil_printf("No config found for %d\r\n", DmaBaseAddress);
		return XST_FAILURE;
	}
#endif

	Status = XAxiDma_CfgInitialize(DmaInstancePtr, CfgPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("Initialization failed %d\r\n", Status);
		return XST_FAILURE;
	}

	if (XAxiDma_HasSg(DmaInstancePtr)) {
		xil_printf("Device configured as SG mode \r\n");
		return XST_FAILURE;
	}

	/* Disable interrupts, we use polling mode
	 */
	XAxiDma_IntrDisable(DmaInstancePtr, XAXIDMA_IRQ_ALL_MASK,
			    XAXIDMA_DEVICE_TO_DMA);
	XAxiDma_IntrDisable(DmaInstancePtr, XAXIDMA_IRQ_ALL_MASK,
			    XAXIDMA_DMA_TO_DEVICE);

    Status = XAxiDma_Selftest(DmaInstancePtr);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

    return XST_SUCCESS;
}


#ifndef SDT
int InitTmrCtr(XTmrCtr *TmrCtrInstancePtr, u16 TmrCtrDeviceId, u8 TmrCtrNumber)
#else
int InitTmrCtr(XTmrCtr *TmrCtrInstancePtr, UINTPTR TmrCtrBaseAddress, u8 TmrCtrNumber)
#endif
{
	int Status;

#ifndef SDT
	Status = XTmrCtr_Initialize(TmrCtrInstancePtr, TmrCtrDeviceId);
#else
	Status = XTmrCtr_Initialize(TmrCtrInstancePtr, TmrCtrBaseAddress);
#endif
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	// Default timer settings (count up, no auto reload, interrupt disabled)
	XTmrCtr_SetOptions(TmrCtrInstancePtr, TmrCtrNumber, 0);
	XTmrCtr_SetResetValue(TmrCtrInstancePtr, TmrCtrNumber, 0);

    /*
	 * Perform a self-test to ensure that the hardware was built
	 * correctly, use the 1st timer in the device (0)
	 */
	Status = XTmrCtr_SelfTest(TmrCtrInstancePtr, TmrCtrNumber);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}
