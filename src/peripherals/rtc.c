/*
 * rtc.c
 *
 *  Created on: 25 nov. 2018
 *      Author: Ludo
 */

#include "rtc.h"

#include "at.h"
#include "exti.h"
#include "exti_reg.h"
#include "mode.h"
#include "nvic.h"
#include "rcc_reg.h"
#include "rtc_reg.h"

/*** RTC local macros ***/

#define RTC_INIT_TIMEOUT_COUNT		1000
#define RTC_WAKEUP_TIMER_DELAY_MAX	65536

/*** RTC local global variables ***/

static volatile unsigned char rtc_alarm_a_flag = 0;
static volatile unsigned char rtc_alarm_b_flag = 0;
static volatile unsigned char rtc_wakeup_timer_flag = 0;

/*** RTC local functions ***/

/* RTC INTERRUPT HANDLER.
 * @param:	None.
 * @return:	None.
 */
void __attribute__((optimize("-O0"))) RTC_IRQHandler(void) {
	// Alarm A interrupt.
	if (((RTC -> ISR) & (0b1 << 8)) != 0) {
		// Update flags.
		if (((RTC -> CR) & (0b1 << 12)) != 0) {
			rtc_alarm_a_flag = 1;
		}
		RTC -> ISR &= ~(0b1 << 8); // ALRAF='0'.
		EXTI -> PR |= (0b1 << EXTI_LINE_RTC_ALARM);
	}
	// Alarm B interrupt.
	if (((RTC -> ISR) & (0b1 << 9)) != 0) {
		// Update flags.
		if (((RTC -> CR) & (0b1 << 13)) != 0) {
			rtc_alarm_b_flag = 1;
		}
		RTC -> ISR &= ~(0b1 << 9); // ALRBF='0'.
		EXTI -> PR |= (0b1 << EXTI_LINE_RTC_ALARM);
	}
	// Wake-up timer interrupt.
	if (((RTC -> ISR) & (0b1 << 10)) != 0) {
		// Update flags.
		if (((RTC -> CR) & (0b1 << 14)) != 0) {
			rtc_wakeup_timer_flag = 1;
		}
		RTC -> ISR &= ~(0b1 << 10); // WUTF='0'.
		EXTI -> PR |= (0b1 << EXTI_LINE_RTC_WAKEUP_TIMER);
	}
}

/* ENTER INITIALIZATION MODE TO ENABLE RTC REGISTERS UPDATE.
 * @param:						None.
 * @return rtc_initf_success:	1 if RTC entered initialization mode, 0 otherwise.
 */
static unsigned char RTC_EnterInitializationMode(void) {
	// Local variables.
	unsigned char rtc_initf_success = 1;
	// Enter key.
	RTC -> WPR = 0xCA;
	RTC -> WPR = 0x53;
	RTC -> ISR |= (0b1 << 7); // INIT='1'.
	unsigned int loop_count = 0;
	while (((RTC -> ISR) & (0b1 << 6)) == 0) {
		// Wait for INITF='1' or timeout.
		if (loop_count > RTC_INIT_TIMEOUT_COUNT) {
			rtc_initf_success = 0;
			break;
		}
		loop_count++;
	}
	return rtc_initf_success;
}

/* EXIT INITIALIZATION MODE TO PROTECT RTC REGISTERS.
 * @param:	None.
 * @return:	None.
 */
static void RTC_ExitInitializationMode(void) {
	RTC -> ISR &= ~(0b1 << 7); // INIT='0'.
}

/*** RTC functions ***/

/* RESET RTC PERIPHERAL.
 * @param:	None.
 * @return:	None.
 */
void RTC_Reset(void) {
	// Reset RTC peripheral.
	RCC -> CSR |= (0b1 << 19); // RTCRST='1'.
	unsigned char j = 0;
	for (j=0 ; j<100 ; j++) {
		// Poll a bit always read as '0'.
		// This is required to avoid for loop removal by compiler (size optimization for HW1.0).
		if (((RCC -> CR) & (0b1 << 24)) != 0) {
			break;
		}
	}
	RCC -> CSR &= ~(0b1 << 19); // RTCRST='0'.
}

/* INIT HARDWARE RTC PERIPHERAL.
 * @param rtc_use_lse:	RTC will be clocked on LSI if 0, on LSE otherwise.
 * @param lsi_freq_hz:	Effective LSI oscillator frequency used to compute the accurate prescaler value (only if LSI is used as source).
 * @return:				None.
 */
void RTC_Init(unsigned char* rtc_use_lse, unsigned int lsi_freq_hz) {
	// Manage RTC clock source.
	if ((*rtc_use_lse) != 0) {
		// Use LSE.
		RCC -> CSR |= (0b01 << 16); // RTCSEL='01'.
	}
	else {
		// Use LSI.
		RCC -> CSR |= (0b10 << 16); // RTCSEL='10'.
	}
	// Enable RTC and register access.
	RCC -> CSR |= (0b1 << 18); // RTCEN='1'.
	// Switch to LSI if RTC failed to enter initialization mode.
	if (RTC_EnterInitializationMode() == 0) {
		RTC_Reset();
		RCC -> CSR |= (0b10 << 16); // RTCSEL='10'.
		RCC -> CSR |= (0b1 << 18); // RTCEN='1'.
		RTC_EnterInitializationMode();
		// Update flag.
		(*rtc_use_lse) = 0;
	}
	// Configure prescaler.
	if ((*rtc_use_lse) != 0) {
		// LSE frequency is 32.768kHz typical.
		RTC -> PRER = (127 << 16) | (255 << 0); // PREDIV_A=127 and PREDIV_S=255 (128*256 = 32768).
	}
	else {
		// Compute prescaler according to measured LSI frequency.
		RTC -> PRER = (127 << 16) | (((lsi_freq_hz / 128) - 1) << 0); // PREDIV_A=127 and PREDIV_S=((lsi_freq_hz/128)-1).
	}
	// Bypass shadow registers.
	RTC -> CR |= (0b1 << 5); // BYPSHAD='1'.
	// Configure alarm A to wake-up MCU every hour.
	RTC -> ALRMAR = 0; // Reset all bits.
	RTC -> ALRMAR |= (0b1 << 31) | (0b1 << 23); // Mask day and hour (only check minutes and seconds).
	//RTC -> ALRMAR |= (0b1 << 15); // Mask minutes (to wake-up every minute for debug).
	RTC -> CR |= (0b1 << 8); // Enable Alarm A.
	// Configure alarm B to wake-up every seconds (watchdog reload and wind measurements for CM).
	RTC -> ALRMBR = 0;
	RTC -> ALRMBR |= (0b1 << 31) | (0b1 << 23) | (0b1 << 15) | (0b1 << 7); // Mask all fields.
	RTC -> CR |= (0b1 << 9); // Enable Alarm B.
	// Configure wake-up timer.
	RTC -> CR &= ~(0b1 << 10); // Disable wake-up timer.
	RTC -> CR &= ~(0b111 << 0);
	RTC -> CR |= (0b100 << 0); // Wake-up timer clocked by RTC clock (1Hz).
	RTC_ExitInitializationMode();
	// Configure EXTI lines.
	EXTI_ConfigureLine(EXTI_LINE_RTC_ALARM, EXTI_TRIGGER_RISING_EDGE);
	EXTI_ConfigureLine(EXTI_LINE_RTC_WAKEUP_TIMER, EXTI_TRIGGER_RISING_EDGE);
	// Disable interrupts and clear all flags.
	RTC -> CR &= ~(0b111 << 12);
	RTC -> ISR &= 0xFFFE0000;
	EXTI -> PR |= (0b1 << EXTI_LINE_RTC_ALARM);
	EXTI -> PR |= (0b1 << EXTI_LINE_RTC_WAKEUP_TIMER);
	// Set interrupt priority.
	NVIC_SetPriority(NVIC_IT_RTC, 1);
	NVIC_EnableInterrupt(NVIC_IT_RTC);
}

/* UPDATE RTC CALENDAR WITH A GPS TIMESTAMP.
 * @param gps_timestamp:	Pointer to timestamp from GPS.
 * @return:					None.
 */
void RTC_Calibrate(Timestamp* gps_timestamp) {
	// Compute register values.
	unsigned int tr_value = 0; // Reset all bits.
	unsigned int dr_value = 0; // Reset all bits.
	// Year.
	unsigned char tens = ((gps_timestamp -> year)-2000) / 10;
	unsigned char units = ((gps_timestamp -> year)-2000) - (tens*10);
	dr_value |= (tens << 20) | (units << 16);
	// Month.
	tens = (gps_timestamp -> month) / 10;
	units = (gps_timestamp -> month) - (tens*10);
	dr_value |= (tens << 12) | (units << 8);
	// Date.
	tens = (gps_timestamp -> date) / 10;
	units = (gps_timestamp -> date) - (tens*10);
	dr_value |= (tens << 4) | (units << 0);
	// Hour.
	tens = (gps_timestamp -> hours) / 10;
	units = (gps_timestamp -> hours) - (tens*10);
	tr_value |= (tens << 20) | (units << 16);
	// Minutes.
	tens = (gps_timestamp -> minutes) / 10;
	units = (gps_timestamp -> minutes) - (tens*10);
	tr_value |= (tens << 12) | (units << 8);
	// Seconds.
	tens = (gps_timestamp -> seconds) / 10;
	units = (gps_timestamp -> seconds) - (tens*10);
	tr_value |= (tens << 4) | (units << 0);
	// Enter initialization mode.
	RTC_EnterInitializationMode();
	// Perform update.
	RTC -> TR = tr_value;
	RTC -> DR = dr_value;
	// Exit initialization mode and restart RTC.
	RTC_ExitInitializationMode();
}

/* GET CURRENT RTC TIME.
 * @param rtc_timestamp:	Pointer to timestamp that will contain current RTC time.
 * @return:					None.
 */
void RTC_GetTimestamp(Timestamp* rtc_timestamp) {
	// Read registers.
	unsigned int dr_value = (RTC -> DR) & 0x00FFFF3F; // Mask reserved bits.
	unsigned int tr_value = (RTC -> TR) & 0x007F7F7F; // Mask reserved bits.
	// Parse registers into timestamp structure.
	rtc_timestamp -> year = 2000 + ((dr_value & (0b1111 << 20)) >> 20) * 10 + ((dr_value & (0b1111 << 16)) >> 16);
	rtc_timestamp -> month = ((dr_value & (0b1 << 12)) >> 12) * 10 + ((dr_value & (0b1111 << 8)) >> 8);
	rtc_timestamp -> date = ((dr_value & (0b11 << 4)) >> 4) * 10 + (dr_value & 0b1111);
	rtc_timestamp -> hours = ((tr_value & (0b11 << 20)) >> 20) * 10 + ((tr_value & (0b1111 << 16)) >> 16);
	rtc_timestamp -> minutes = ((tr_value & (0b111 << 12)) >> 12) * 10 + ((tr_value & (0b1111 << 8)) >> 8);
	rtc_timestamp -> seconds = ((tr_value & (0b111 << 4)) >> 4) * 10 + (tr_value & 0b1111);
}

/* ENABLE RTC ALARM A INTERRUPT.
 * @param:	None.
 * @return:	None.
 */
void RTC_EnableAlarmAInterrupt(void) {
	// Enable interrupt.
	RTC -> CR |= (0b1 << 12); // ALRAIE='1'.
}

/* DISABLE RTC ALARM A INTERRUPT.
 * @param:	None.
 * @return:	None.
 */
void RTC_DisableAlarmAInterrupt(void) {
	// Disable interrupt.
	RTC -> CR &= ~(0b1 << 12); // ALRAIE='0'.
}

/* RETURN THE CURRENT ALARM INTERRUPT STATUS.
 * @param:	None.
 * @return:	1 if the RTC interrupt occured, 0 otherwise.
 */
volatile unsigned char RTC_GetAlarmAFlag(void) {
	return rtc_alarm_a_flag;
}

/* CLEAR ALARM A INTERRUPT FLAG.
 * @param:	None.
 * @return:	None.
 */
void RTC_ClearAlarmAFlag(void) {
	// Clear all flags.
	RTC -> ISR &= ~(0b1 << 8); // ALRAF='0'.
	EXTI -> PR |= (0b1 << EXTI_LINE_RTC_ALARM);
	rtc_alarm_a_flag = 0;
}

/* ENABLE RTC ALARM B INTERRUPT.
 * @param:	None.
 * @return:	None.
 */
void RTC_EnableAlarmBInterrupt(void) {
	// Enable interrupt.
	RTC -> CR |= (0b1 << 13); // ALRBIE='1'.
}

/* DISABLE RTC ALARM A INTERRUPT.
 * @param:	None.
 * @return:	None.
 */
void RTC_DisableAlarmBInterrupt(void) {
	// Disable interrupt.
	RTC -> CR &= ~(0b1 << 13); // ALRBIE='0'.
}

/* RETURN THE CURRENT ALARM INTERRUPT STATUS.
 * @param:	None.
 * @return:	1 if the RTC interrupt occured, 0 otherwise.
 */
volatile unsigned char RTC_GetAlarmBFlag(void) {
	return rtc_alarm_b_flag;
}

/* CLEAR ALARM A INTERRUPT FLAG.
 * @param:	None.
 * @return:	None.
 */
void RTC_ClearAlarmBFlag(void) {
	// Clear all flags.
	RTC -> ISR &= ~(0b1 << 9); // ALRBF='0'.
	EXTI -> PR |= (0b1 << EXTI_LINE_RTC_ALARM);
	rtc_alarm_b_flag = 0;
}

/* START RTC WAKE-UP TIMER.
 * @param delay_seconds:	Delay in seconds.
 * @return:					None.
 */
void RTC_StartWakeUpTimer(unsigned int delay_seconds) {
	// Clamp parameter.
	unsigned int local_delay_seconds = delay_seconds;
	if (local_delay_seconds > RTC_WAKEUP_TIMER_DELAY_MAX) {
		local_delay_seconds = RTC_WAKEUP_TIMER_DELAY_MAX;
	}
	// Check if timer si not allready running.
	if (((RTC -> CR) & (0b1 << 10)) == 0) {
		// Enable RTC and register access.
		RTC_EnterInitializationMode();
		// Configure wake-up timer.
		RTC -> CR &= ~(0b1 << 10); // Disable wake-up timer.
		RTC -> WUTR = (local_delay_seconds - 1);
		// Clear flags.
		RTC -> ISR &= ~(0b1 << 10); // WUTF='0'.
		EXTI -> PR |= (0b1 << EXTI_LINE_RTC_WAKEUP_TIMER);
		// Enable interrupt.
		RTC -> CR |= (0b1 << 14); // WUTE='1'.
		// Start timer.
		RTC -> CR |= (0b1 << 10); // Enable wake-up timer.
		RTC_ExitInitializationMode();
	}
}

/* STOP RTC WAKE-UP TIMER.
 * @param:	None.
 * @return:	None.
 */
void RTC_StopWakeUpTimer(void) {
	// Enable RTC and register access.
	RTC_EnterInitializationMode();
	RTC -> CR &= ~(0b1 << 10); // Disable wake-up timer.
	RTC_ExitInitializationMode();
	// Disable interrupt.
	RTC -> CR &= ~(0b1 << 14); // WUTE='0'.
}

/* RETURN THE CURRENT ALARM INTERRUPT STATUS.
 * @param:	None.
 * @return:	1 if the RTC interrupt occured, 0 otherwise.
 */
volatile unsigned char RTC_GetWakeUpTimerFlag(void) {
	return rtc_wakeup_timer_flag;
}

/* CLEAR ALARM A INTERRUPT FLAG.
 * @param:	None.
 * @return:	None.
 */
void RTC_ClearWakeUpTimerFlag(void) {
	// Clear all flags.
	RTC -> ISR &= ~(0b1 << 10); // WUTF='0'.
	EXTI -> PR |= (0b1 << EXTI_LINE_RTC_WAKEUP_TIMER);
	rtc_wakeup_timer_flag = 0;
}
