#ifndef PROJECT_H
#define PROJECT_H

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

/************************** CONSTANTS *********************************/

#define N_INPUT 7
#define N_HIDDEN 2
#define BATCH_SIZE 64

#define X_SIZE (BATCH_SIZE * N_INPUT)
#define W_HID_SIZE ((N_INPUT + 1) * N_HIDDEN)
#define W_OUT_SIZE (N_HIDDEN + 1)
#define TOTAL_ELEMENTS (X_SIZE + W_HID_SIZE + W_OUT_SIZE)

#define SCALE 256
#define WORD_SIZE 4

#define TIMER_COUNTER_0 0

/************************** STRUCTS *********************************/

typedef struct {
    u32 TxElapsed;
    u32 RxElapsed;
    u32 SoftElapsed;
} Stats;

/************************** GLOBALS *********************************/

extern XLlFifo FifoInstance;
extern XTmrCtr TmrCtrInstance;

extern u32 X_Buffer[X_SIZE];
extern u32 W_Hid_Buffer[W_HID_SIZE];
extern u32 W_Out_Buffer[W_OUT_SIZE];

extern u32 SourceBuffer[TOTAL_ELEMENTS];
extern u32 DestinationBuffer[BATCH_SIZE];
extern u32 Y_soft[BATCH_SIZE];

extern u32 sigmoid_lut[256];

extern char TERMINATE_TOKEN[];

/************************** FUNCTION DECLARATIONS *********************************/

int RunMLP(XLlFifo*, XTmrCtr*, u8, Stats*);

int TxSend(XLlFifo*, u32*, int, XTmrCtr*, u8, Stats*);

int RxReceive(XLlFifo*, u32*, int, XTmrCtr*, u8, Stats*);

int ReceiveCSVData(u32*, int);

void MergeMLPData(u32*);

void mlp_soft(u32*, u32*, u32*, u32*);

void CompareResults(u32*, u32*);

u32 sigmoid(u32);

#endif
