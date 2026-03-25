#include "xllfifo.h"

XLlFifo_Config XLlFifo_ConfigTable[] __attribute__ ((section (".drvcfg_sec"))) = {

	{
		"xlnx,axi-fifo-mm-s-4.3", /* compatible */
		0xa0000000,
		0xa0010000, /* reg */
		0x1, /* xlnx,data-interface-type */
		0xffff, /* interrupts */
		0xffff /* interrupt-parent */
	},
	 {
		 NULL
	}
};