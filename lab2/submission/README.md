# EE4218 Lab 2

**Group No: 14**

## Remarks
### Execution Loop

The program runs an infinite loop: on each iteration it receives Matrix A and B over UART (as CSV via RealTerm "Send File"), streams the data through the AXI FIFO, and performs the matrix multiplication. This repeats indefinitely, allowing multiple datasets to be tested without reprogramming the board.

To stop, send `TERMINATE_TOKEN.txt` in place of a CSV file. The board will immediately transmit the most recent timing statistics over UART in the format:

```
STATS:TX=<cycles>,RX=<cycles>,MATMUL=<cycles>
```

after which the application exits.

### Silent Mode & Output Capture (`ENABLE_PRINTF`)

By default, `ENABLE_PRINTF` is **not** defined. All `xil_printf` calls are suppressed via the macro in `lab2.h`:

```c
#ifndef ENABLE_PRINTF
#define xil_printf(...) do {} while(0)
#endif
```

This means the only bytes sent over UART are the raw CSV results and the `STATS:` line, allowing RealTerm's capture output to be saved directly as `RES.csv` and `STATS.txt` without any debug noise.

To re-enable verbose output (progress messages, elapsed cycle prints, etc.), add `-DENABLE_PRINTF` to the compiler flags in Vitis. `ENABLE_PRINTF.txt` in this submission shows what the full debug output looks like with this flag set.