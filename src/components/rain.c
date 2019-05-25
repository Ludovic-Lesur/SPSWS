/*
 * rain.c
 *
 *  Created on: 5 mai 2019
 *      Author: Ludovic
 */

#include "rain.h"

#include "exti.h"
#include "gpio.h"
#include "mapping.h"
#include "mode.h"
#include "nvic.h"

#if (defined CM || defined ATM)

/*** RAIN local macros ***/

// Pluviometry conversion ratio.
#define RAIN_EDGE_TO_UM		279

/*** RAIN local global variables ***/

volatile unsigned char rain_edge_count;

/*** RAIN functions ***/

/* INIT RAIN GAUGE.
 * @param:	None.
 * @return:	None.
 */
void RAIN_Init(void) {

	/* GPIO mapping selection */
	GPIO_RAIN = GPIO_DIO2;

	/* Init GPIOs and EXTI */
	GPIO_Configure(&GPIO_RAIN, Input, OpenDrain, LowSpeed, NoPullUpNoPullDown);
	EXTI_ConfigureInterrupt(&GPIO_RAIN, EXTI_TRIGGER_FALLING_EDGE);
}

/* START CONTINUOUS RAIN MEASUREMENTS.
 * @param:	None.
 * @return:	None.
 */
void RAIN_StartContinuousMeasure(void) {

	/* Enable required interrupt */
	NVIC_EnableInterrupt(IT_EXTI_4_15);
}

/* STOP CONTINUOUS RAIN MEASUREMENTS.
 * @param:	None.
 * @return:	None.
 */
void RAIN_StopContinuousMeasure(void) {

	/* Disable required interrupt */
	NVIC_DisableInterrupt(IT_EXTI_4_15);
}

/* GET PLUVIOMETRY SINCE LAST MEASUREMENT START.
 * @param rain_pluviometry_mm:	Pointer to char that will contain pluviometry in mm.
 * @return:						None.
 */
void RAIN_GetPluviometry(unsigned char* rain_pluviometry_mm) {
	// Convert edge count to mm of rain.
	(*rain_pluviometry_mm) = (rain_edge_count * RAIN_EDGE_TO_UM) / 1000;
}

/* RESET RAIN MEASUREMENT DATA.
 * @param:	None.
 * @return:	None.
 */
void RAIN_ResetData(void) {
	// Reset edge count.
	rain_edge_count = 0;
}

/* FUNCTION CALLED BY EXTI INTERRUPT HANDLER WHEN AN EDGE IS DETECTED ON THE RAIN SIGNAL.
 * @param:	None.
 * @return:	None.
 */
void RAIN_EdgeCallback(void) {
	// Increment edge count.
	rain_edge_count++;
}

#endif