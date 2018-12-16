/*
 * sx1232.h
 *
 *  Created on: 20 june 2018
 *      Author: Ludovic
 */

#ifndef SX1232_H
#define SX1232_H

/*** SX1232 macros ***/

// Output power ranges.
#define SX1232_OUTPUT_POWER_RFO_MIN			-1
#define SX1232_OUTPUT_POWER_RFO_MAX			14
#define SX1232_OUTPUT_POWER_PABOOST_MIN		2
#define SX1232_OUTPUT_POWER_PABOOST_MAX		17

/*** SX1232 structures ***/

// Oscillator configuration.
typedef enum {
	SX1232_QUARTZ,
	SX1232_TCXO
} SX1232_Oscillator;

// Transceiver modes.
typedef enum {
	SX1232_MODE_SLEEP,
	SX1232_MODE_STANDBY,
	SX1232_MODE_FSTX,
	SX1232_MODE_TX,
	SX1232_MODE_FSRX,
	SX1232_MODE_RX
} SX1232_Mode;

// Modulation.
typedef enum {
	SX1232_MODULATION_FSK,
	SX1232_MODULATION_OOK
} SX1232_Modulation;

// Bit rate.
typedef enum {
	// Note: 100bps can't be programmed.
	SX1232_BITRATE_600BPS
} SX1232_BitRate;

// RF output pin.
typedef enum {
	SX1232_RF_OUTPUT_PIN_RFO,
	SX1232_RF_OUTPUT_PIN_PABOOST,
} SX1232_RfOutputPin;

/*** SX1232 functions ***/

void SX1232_Init(void);
// Common settings.
void SX1232_SetOscillator(SX1232_Oscillator oscillator);
void SX1232_SetMode(SX1232_Mode mode);
void SX1232_SetModulation(SX1232_Modulation modulation);
void SX1232_SetRfFrequency(unsigned int rf_frequency_hz);
void SX1232_SetBitRate(SX1232_BitRate bit_rate);
// TX functions.
void SX1232_SelectRfOutputPin(SX1232_RfOutputPin rf_output_pin);
void SX1232_SetRfOutputPower(signed char rf_output_power_dbm);
void SX1232_StartContinuousTransmission(void);
void SX1232_StopContinuousTransmission(void);

#endif /* SX1232_H */
