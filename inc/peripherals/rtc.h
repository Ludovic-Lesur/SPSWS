/*
 * rtc.h
 *
 *  Created on: 25 nov. 2018
 *      Author: Ludo
 */

#ifndef RTC_H
#define RTC_H

#include "neom8n.h"

/*** RTC functions ***/

void RTC_Init(unsigned char rtc_use_lse);
void RTC_EnableInterrupt(void);
void RTC_Calibrate(Timestamp* gps_timestamp);
unsigned char RTC_GetCalibrationStatus(void);
void RTC_GetTimestamp(Timestamp* rtc_timestamp);

unsigned char RTC_GetIrqStatus(void);
void RTC_ClearIrqStatus(void);

#endif /* RTC_H */
