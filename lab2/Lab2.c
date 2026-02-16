/******************************************************************************
* Copyright (C) 2013 - 2022 Xilinx, Inc.  All rights reserved.
* Copyright (c) 2022 - 2023 Advanced Micro Devices, Inc. All Rights Reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/

/*****************************************************************************/
/**
 *
 * @file XLlFifo_polling_example.c
 * This file demonstrates how to use the Streaming fifo driver on the xilinx AXI
 * Streaming FIFO IP.The AXI4-Stream FIFO core allows memory mapped access to a
 * AXI-Stream interface. The core can be used to interface to AXI Streaming IPs
 * similar to the LogiCORE IP AXI Ethernet core, without having to use full DMA
 * solution.
 *
 * This is the polling example for the FIFO it assumes that at the
 * h/w level FIFO is connected in loopback.In these we write known amount of
 * data to the FIFO and Receive the data and compare with the data transmitted.
 *
 * Note: The TDEST Must be enabled in the H/W design inorder to
 * get correct RDR value.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -------------------------------------------------------
 * 3.00a adk 08/10/2013 initial release CR:727787
 * 5.1   ms  01/23/17   Modified xil_printf statement in main function to
 *                      ensure that "Successfully ran" and "Failed" strings
 *                      are available in all examples. This is a fix for
 *                      CR-965028.
 *       ms  04/05/17   Added tabspace for return statements in functions for
 *                      proper documentation and Modified Comment lines
 *                      to consider it as a documentation block while
 *                      generating doxygen.
 * 5.3  rsp 11/08/18    Modified TxSend to fill SourceBuffer with non-zero
 *                      data otherwise the test can return a false positive
 *                      because DestinationBuffer is initialized with zeros.
 *                      In fact, fixing this exposed a bug in RxReceive and
 *                      caused the test to start failing. According to the
 *                      product guide (pg080) for the AXI4-Stream FIFO, the
 *                      RDFO should be read before reading RLR. Reading RLR
 *                      first will result in the RDFO being reset to zero and
 *                      no data being received.
 * </pre>
 *
 * ***************************************************************************
 */

/***************************** Include Files *********************************/
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

#ifdef XPAR_UARTNS550_0_BASEADDR
#include "xuartns550_l.h"       /* to use uartns550 */
#endif

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

#ifndef SDT
#define FIFO_DEV_ID	   	XPAR_AXI_FIFO_0_DEVICE_ID
#endif

#define WORD_SIZE 4			/* Size of words in bytes */

// #define MAX_PACKET_LEN 4

// #define NO_OF_PACKETS 64

// #define MAX_DATA_BUFFER_SIZE NO_OF_PACKETS*MAX_PACKET_LEN
#define MATRIX_A_ROWS 64
#define MATRIX_A_COLS 8
#define MATRIX_B_ROWS 8
#define MATRIX_B_COLS 1

#define TOTAL_ELEMENTS (MATRIX_A_ROWS * MATRIX_A_COLS) + (MATRIX_B_ROWS * MATRIX_B_COLS)
#define MatrixA_Size (MATRIX_A_COLS * MATRIX_A_ROWS)
#define MatrixB_Size (MATRIX_B_COLS * MATRIX_B_ROWS)
u32 MatrixA[MatrixA_Size];
u32 MatrixB[MatrixB_Size];

#undef DEBUG

/************************** Function Prototypes ******************************/
/* Main Assignment Logic */
#ifndef SDT
int RunMatrixAssignment(XLlFifo *InstancePtr, u16 DeviceId);
#else
int RunMatrixAssignment(XLlFifo *InstancePtr, UINTPTR BaseAddress);
#endif

/* AXI Timer Setup - Required for Performance Analysis */
int TimerSetup(XTmrCtr *TimerInstancePtr, u16 TimerDeviceId);

/* Matrix Math Functions */
void MatrixMultiply(u32 *A, u32 *B, u32 *RES);

/* Data Transfer Functions */
int TxSend(XLlFifo *InstancePtr, u32 *SourceAddr, int Words);
int RxReceive(XLlFifo *InstancePtr, u32 *DestinationAddr, int Words);

/* UART/RealTerm Functions */
int ReceiveCSVData(u32 *BufferA, int TotalElements);
void SendCSVResults(u32 *RES, int TotalElements);

/* Utility Functions */
void MergeArrays(u32 *dest, u32 *A, int sizeA, u32 *B, int sizeB);
int performMatrixMultiplication(u32 *data, int A_rows, int A_cols, int B_cols);
/************************** Variable Definitions *****************************/
/*
 * Device instance definitions
 */
XLlFifo FifoInstance;

u32 SourceBuffer[TOTAL_ELEMENTS];		//send to FPGA
u32 DestinationBuffer[TOTAL_ELEMENTS];		//received from FPGA, right now its in loopback.

/*****************************************************************************/
/**
*
* Main function
*
* This function is the main entry of the Axi FIFO Polling test.
*
* @param	None
*
* @return
*		- XST_SUCCESS if tests pass
* 		- XST_FAILURE if fails.
*
* @note		None
*
******************************************************************************/
int main()
{
	int Status;
	//TODO
    // xil_printf("Ready! Please use RealTerm -> 'Send File' to send A.csv\r\n");
    // ReceiveCSVData(MatrixA, MatrixA_Size);
    // xil_printf("Matrix A Received. Now please send B.csv\r\n");
    // ReceiveCSVData(MatrixB, MatrixB_Size);
    // xil_printf("All data received successfully!\r\n");
	// xil_printf("Merging data into SourceBuffer...\r\n");
	// Merge MatrixA and MatrixB into SourceBuffer
	// for (int i = 0; i < MatrixA_Size; i++) {
	// 	SourceBuffer[i] = MatrixA[i];
	// }
	// for (int i = 0; i < MatrixB_Size; i++) {
	// 	SourceBuffer[MatrixA_Size + i] = MatrixB[i];
	// }

	Status = RunMatrixAssignment(&FifoInstance, XPAR_XLLFIFO_0_BASEADDR);
	if (Status != XST_SUCCESS) {
		xil_printf("Failed to execute\n\r");
		xil_printf("--- Exiting main() ---\n\r");
		return XST_FAILURE;
	}
	return Status;
}

/*****************************************************************************/
/**
*
* This function demonstrates the usage AXI FIFO
* It does the following:
*       - Set up the output terminal if UART16550 is in the hardware build
*       - Initialize the Axi FIFO Device.
*	- Transmit the data
*	- Receive the data from fifo
*	- Compare the data
*	- Return the result
*
* @param	InstancePtr is a pointer to the instance of the
*		XLlFifo component.
* @param	DeviceId is Device ID of the Axi Fifo Device instance,
*		typically XPAR_<AXI_FIFO_instance>_DEVICE_ID value from
*		xparameters.h.
*
* @return
*		-XST_SUCCESS to indicate success
*		-XST_FAILURE to indicate failure
*
******************************************************************************/
#ifndef SDT
int RunMatrixAssignment(XLlFifo *InstancePtr, u16 DeviceId)
#else
int RunMatrixAssignment(XLlFifo *InstancePtr, UINTPTR BaseAddress)
#endif
{
	XLlFifo_Config *Config;
	int Status;
	int i;
	int Error;
	Status = XST_SUCCESS;

	/* Initial setup for Uart16550 */
#ifdef XPAR_UARTNS550_0_BASEADDR

	Uart550_Setup();

#endif

	/* Initialize the Device Configuration Interface driver */
#ifndef SDT
	Config = XLlFfio_LookupConfig(DeviceId);
#else
	Config = XLlFfio_LookupConfig(BaseAddress);
#endif
	if (!Config) {
#ifndef SDT
		xil_printf("No config found for %d\r\n", DeviceId);
#endif
		return XST_FAILURE;
	}

	/*
	 * This is where the virtual address would be used, this example
	 * uses physical address.
	 */
	Status = XLlFifo_CfgInitialize(InstancePtr, Config, Config->BaseAddress);
	if (Status != XST_SUCCESS) {
		xil_printf("Initialization failed\n\r");
		return Status;
	}

	/* Check for the Reset value */
	Status = XLlFifo_Status(InstancePtr);
	XLlFifo_IntClear(InstancePtr,0xffffffff);
	Status = XLlFifo_Status(InstancePtr);
	if(Status != 0x0) {
		xil_printf("\n ERROR : Reset value of ISR0 : 0x%x\t"
			    "Expected : 0x0\n\r",
			    XLlFifo_Status(InstancePtr));
		return XST_FAILURE;
	}

	/* receive the data and store in respective matrix array*/
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
		// Merge MatrixA and MatrixB into SourceBuffer
		MergeArrays(SourceBuffer, MatrixA, MatrixA_Size, MatrixB, MatrixB_Size);
	}
	//DEBUG: printing merged data
	// for(i=0; i<TOTAL_ELEMENTS; i++){
	// 	xil_printf("SourceBuffer[%d] = %d\r\n", i, SourceBuffer[i]);
	// }


	//TODO: START TIMER HERE FOR PERFORMANCE ANALYSIS


	/* Transmit the Data Stream */
	Status = TxSend(InstancePtr, SourceBuffer, TOTAL_ELEMENTS);
	if (Status != XST_SUCCESS){
		xil_printf("Transmission of Data failed\n\r");
		return XST_FAILURE;
	}

	/* Receive the Data Stream */
	Status = RxReceive(InstancePtr, DestinationBuffer, TOTAL_ELEMENTS);
	if (Status != XST_SUCCESS){
		xil_printf("Receiving data failed");
		return XST_FAILURE;
	}
	// // DEBUG: printing received data
	// for(i=0; i<TOTAL_ELEMENTS; i++){
	// 	xil_printf("DestinationBuffer[%d] = %d\r\n", i, DestinationBuffer[i]);
	// }

	Status = performMatrixMultiplication(DestinationBuffer, MATRIX_A_ROWS, MATRIX_A_COLS, MATRIX_B_COLS);

	if (Status != XST_SUCCESS) {
		xil_printf("Matrix multiplication failed\r\n");
		return XST_FAILURE;
	}
	xil_printf("Matrix multiplication completed successfully!, output is a %dx%d matrix\r\n", MATRIX_A_ROWS, MATRIX_B_COLS);
	// // DEBUG: printing result data, same as destination array
	// for(i=0; i<MATRIX_A_ROWS*MATRIX_B_COLS; i++){
	// 	xil_printf("DestinationBuffer[%d] = %d\r\n", i, DestinationBuffer[i]);
	// }
	// Error = 0;

	SendCSVResults(DestinationBuffer, MATRIX_A_ROWS * MATRIX_B_COLS);

	return Status;

	/* Compare the data send with the data received */
	// xil_printf(" Comparing data ...\n\r");
	// for( i=0 ; i<MAX_DATA_BUFFER_SIZE ; i++ ){
	// 	if ( *(SourceBuffer + i) != *(DestinationBuffer + i) ){
	// 		Error = 1;
	// 		break;
	// 	}

	// }

	// if (Error != 0){
	// 	return XST_FAILURE;
	// }

	// return Status;
}

/*****************************************************************************/
/**
*
* TxSend routine, It will send the requested amount of data at the
* specified addr.
*
* @param	InstancePtr is a pointer to the instance of the
*		XLlFifo component.
*
* @param	SourceAddr is the address where the FIFO stars writing
*
* @return
*		-XST_SUCCESS to indicate success
*		-XST_FAILURE to indicate failure
*
* @note		None
*
******************************************************************************/
//TODO: Check if there is a need to send data by packets or we can just send the whole buffer at once.
//If packets are needed, we will need to modify the function signature to include packet size and number of packets.
int TxSend(XLlFifo *InstancePtr, u32  *SourceAddr, int Words)
{

	int i;
	// int j;
	xil_printf(" Transmitting Data ... \r\n");

	/* Fill the transmit buffer with incremental pattern */
	// for (i=0;i<MAX_DATA_BUFFER_SIZE;i++)
	// 	*(SourceAddr + i) = i;

	/* Writing into the FIFO Transmit Port Buffer */
	for (i=0 ; i < Words ; i++){
		if( XLlFifo_iTxVacancy(InstancePtr) ){ //wait until there is space in the FIFO
			XLlFifo_TxPutWord(InstancePtr, SourceAddr[i]);
		}
	}

	/* Start Transmission by writing transmission length into the TLR */
	XLlFifo_iTxSetLen(InstancePtr, (Words * WORD_SIZE));

	/* Check for Transmission completion */
	while( !(XLlFifo_IsTxDone(InstancePtr)) ){

	}

	/* Transmission Complete */
	return XST_SUCCESS;
}

// /*****************************************************************************/
// /**
// *
// * RxReceive routine.It will receive the data from the FIFO.
// *
// * @param	InstancePtr is a pointer to the instance of the
// *		XLlFifo instance.
// *
// * @param	DestinationAddr is the address where to copy the received data.
// *
// * @return
// *		-XST_SUCCESS to indicate success
// *		-XST_FAILURE to indicate failure
// *
// * @note		None
// *
// ******************************************************************************/
int RxReceive (XLlFifo *InstancePtr, u32* DestinationAddr, int Words)
{

	int i;
	int Status;
	u32 RxWord;
	int count = 0;
	// static u32 ReceiveLength;

	xil_printf(" Receiving data ....\n\r");

	// while(XLlFifo_iRxOccupancy(InstancePtr)) {
	// 	/* Read Receive Length */
	// 	// ReceiveLength = (XLlFifo_iRxGetLen(InstancePtr))/WORD_SIZE;
	// 	for (i=0; i < Words; i++) {
	// 		RxWord = XLlFifo_RxGetWord(InstancePtr);
	// 		DestinationAddr[i] = RxWord;
	// 	}
	// }
	while (count < Words) {
		if(XLlFifo_iRxOccupancy(InstancePtr)) {
			RxWord = XLlFifo_RxGetWord(InstancePtr);
			DestinationAddr[count] = RxWord;
			count++;
		}
	}

	Status = XLlFifo_IsRxDone(InstancePtr);
	if(Status != TRUE){
		xil_printf("Failing in receive complete ... \r\n");
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

// #ifdef XPAR_UARTNS550_0_BASEADDR
/*****************************************************************************/
/*
*
* Uart16550 setup routine, need to set baudrate to 9600 and data bits to 8
*
* @param	None
*
* @return	None
*
* @note		None
*
******************************************************************************/
// static void Uart550_Setup(void)
// {

// 	XUartNs550_SetBaud(XPAR_UARTNS550_0_BASEADDR,
// 			XPAR_XUARTNS550_CLOCK_HZ, 9600);

// 	XUartNs550_SetLineControlReg(XPAR_UARTNS550_0_BASEADDR,
// 			XUN_LCR_8_DATA_BITS);
// }
// #endif

/*****************************************************************************/
/**
 * ReceiveCSVData routine. It will receive CSV data from RealTerm and store it in the provided buffer.
 *
 * @param	Buffer is the address where to copy the received data.
 * @param	TotalElements is the total number of elements expected in the CSV data.
 * @return
 *		-XST_SUCCESS to indicate success
 *		-XST_FAILURE to indicate failure
 * @note		None
 *******************************************************************************/
int ReceiveCSVData(u32 *Buffer, int TotalElements) {
    char msg[20];
    int msg_idx = 0;
    int count = 0;
    char RecvChar;

    xil_printf("Receiving CSV data for %d elements...\r\n", TotalElements);

    while (count < TotalElements) {
        RecvChar = XUartPs_RecvByte(XPAR_XUARTPS_0_BASEADDR);

        // Skip carriage return
        if (RecvChar == '\r') {
            continue;
        }

        // Delimiter found (comma or newline)
        if (RecvChar == ',' || RecvChar == '\n') {
            if (msg_idx > 0) {
                msg[msg_idx] = '\0';
                Buffer[count] = atoi(msg);
                count++;
                msg_idx = 0;
                
                // Print progress every 64 values
                if (count % 64 == 0) {
                    xil_printf("Progress: %d/%d values received\r\n", count, TotalElements);
                }
            }
            // Ignore empty fields (consecutive delimiters)
        }
        // Valid digit or negative sign
        else if ((RecvChar >= '0' && RecvChar <= '9') || RecvChar == '-') {
            if (msg_idx < 19) {
                msg[msg_idx++] = RecvChar;
            }
        }
        // Ignore whitespace and other characters
    }

    xil_printf("CSV data reception complete. %d values received.\r\n", count);
	
	if (count != TotalElements) {
        xil_printf("ERROR: Expected %d elements but received %d!\r\n", TotalElements, count);
        xil_printf("Please check your CSV file and resend.\r\n");
		return XST_FAILURE;
        // Optionally, you can clear the buffer or halt further processing here
    } else {
        xil_printf("All %d elements received successfully.\r\n", TotalElements);
		return XST_SUCCESS;
    }

	
	// DEBUG: After CSV data reception is complete
	// xil_printf("Sanity check: First 5 elements:\r\n");
	// for (int i = 0; i < 5; i++) {
	// 	xil_printf("%d ", Buffer[i]);
	// }
	// xil_printf("\r\n");

	// xil_printf("Sanity check: Last 5 elements:\r\n");
	// for (int i = TotalElements - 5; i < TotalElements; i++) {
	// 	xil_printf("%d ", Buffer[i]);
	// }
	// xil_printf("\r\n");
	return XST_SUCCESS;
}
/*****************************************************************************/
/**
 * MergeArrays routine. It will merge two arrays A and B into a single array.
 *
 * @param	A is the first input array.
 * @param	B is the second input array.
 * @param	SizeA is the number of elements in array A.
 * @param	SizeB is the number of elements in array B.
 * @return
 *		Pointer to the merged array containing all elements of A followed by all elements of B.
 * @note		The merged array is stored in a static variable, so it will persist after the function returns. However, it will be overwritten on subsequent calls to this function.
 *******************************************************************************/
void MergeArrays(u32 *dest, u32 *A, int sizeA, u32 *B, int sizeB) {
    // Copy A into dest
    for (int i = 0; i < sizeA; i++) {
        dest[i] = A[i];
    }
    // Copy B into dest after A
    for (int i = 0; i < sizeB; i++) {
        dest[sizeA + i] = B[i];
    }
}

/*****************************************************************************/
/**
 * performMatrixMultiplication routine. It will perform matrix multiplication on the data received from RealTerm. The first part of the data is Matrix A and the second part is Matrix B. The result will be stored back in DestinationBuffer.
 *
 * @param	data is the pointer to the merged data containing both Matrix A and Matrix B.
 * @param	A_rows is the number of rows in Matrix A.
 * @param	A_cols is the number of columns in Matrix A (and also the number of rows in Matrix B).
 * @param	B_cols is the number of columns in Matrix B.
 * @return
 *		-XST_SUCCESS to indicate success
 *		-XST_FAILURE to indicate failure
 * @note		The result of the matrix multiplication will be stored in DestinationBuffer, which will then be sent back to RealTerm for verification.
 *******************************************************************************/

int performMatrixMultiplication(u32 *data, int A_rows, int A_cols, int B_cols) {
	u32 *A = data; // First part of data is Matrix A
	u32 *B = data + (A_rows * A_cols); // Second part of data is Matrix B
	u32 RES[A_rows * B_cols]; // Result matrix

	// Initialize RES with zeros
	for (int i = 0; i < A_rows * B_cols; i++) {
		RES[i] = 0;
	}

	// Perform matrix multiplication: RES = A * B
	for (int i = 0; i < A_rows; i++) {
		for (int j = 0; j < B_cols; j++) {
			for (int k = 0; k < A_cols; k++) {
				RES[i * B_cols + j] += A[i * A_cols + k] * B[k * B_cols + j];
			}
		}
	}

	// Copy result back to DestinationBuffer for sending back to RealTerm
	for (int i = 0; i < A_rows * B_cols; i++) {
		DestinationBuffer[i] = RES[i] / 256; 
	}
	
	return XST_SUCCESS;
}

/*****************************************************************************/
/**
 * SendCSVResults routine. It will send the results of matrix multiplication back to RealTerm in CSV format.
 *
 * @param	data is the pointer to the result data that needs to be sent back.
 * @param	size is the number of elements in the result data.
 * @return
 *		None
 * @note		The results will be sent back as a single line of CSV, with values separated by commas and ending with a newline character.
 *******************************************************************************/
void SendCSVResults(u32 *data, int size) {
	// Send the results back to RealTerm in CSV format
	for (int i = 0; i < size; i++) {
		char buffer[12]; // Buffer to hold string representation of number
		sprintf(buffer, "%d", data[i]);
		for (char *p = buffer; *p != '\0'; p++) {
			XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, *p);
		}
		if (i < size - 1) {
			XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, ','); // Send comma between values
		}
	}
	XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, '\n'); // Newline at the end
}