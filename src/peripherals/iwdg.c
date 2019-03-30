/*
 * iwdg.c
 *
 *  Created on: 30 mars 2019
 *      Author: Ludovic
 */

#include "iwdg.h"

#include "iwdg_reg.h"

/*** IWDG functions ***/

/* INIT AND START INDEPENDENT WATCHDOG.
 * @param:	None.
 * @return:	None.
 */
void IWDG_Init(void) {

	/* Configure peripheral */
	IWDG -> KR = 0x0000CCCC; // Enable peripheral.
	IWDG -> KR = 0x00005555; // Enable register access.
	// Prescaler=256 -> watchdog clock = 32kHz / 256 = 125Hz (8ms period).
	IWDG -> PR = (0b111 << 0); // PR='111'.
	// Set reload value.
	IWDG -> RLR = 4095; // 4095 * 8ms = 32s.
	// Wait for register to be updated.
	while (IWDG -> SR != 0); // Wait for WVU='0', RVU='0' and PVU='0'.
}

/* RELOAD WATCHDOG COUNTER.
 * @param:	None.
 * @return:	None.
 */
void IWDG_Reload(void) {
	// Reload counter.
	IWDG -> KR = 0x0000AAAA;
}
