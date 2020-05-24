/*
 * tim.h
 *
 *  Created on: 4 may 2018
 *      Author: Ludo
 */

#ifndef TIM_H
#define TIM_H

/*** TIM macros ***/

#define TIM2_TIMINGS_ARRAY_LENGTH		5
#define TIM2_TIMINGS_ARRAY_ARR_IDX		0
#define TIM2_TIMINGS_ARRAY_CCR1_IDX		1
#define TIM2_TIMINGS_ARRAY_CCR2_IDX		2
#define TIM2_TIMINGS_ARRAY_CCR3_IDX		3
#define TIM2_TIMINGS_ARRAY_CCR4_IDX		4

/*** TIM functions ***/

void TIM21_Init(void);
void TIM21_Start(void);
void TIM21_Stop(void);
void TIM21_Disable(void);

void TIM22_Init(void);
void TIM22_Start(void);
void TIM22_Stop(void);
void TIM22_Disable(void);
volatile unsigned int TIM22_GetSeconds(void);

void TIM2_Init(unsigned short timings[TIM2_TIMINGS_ARRAY_LENGTH]);
void TIM2_Enable(void);
void TIM2_Disable(void);
void TIM2_Start(void);
void TIM2_Stop(void);
volatile unsigned int TIM2_GetCounter(void);

#endif /* TIM_H */
