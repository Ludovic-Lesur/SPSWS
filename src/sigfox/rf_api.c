/*
 * rf_api.c
 *
 *  Created on: 18 juin 2018
 *      Author: Ludovic
 */

#include "rf_api.h"

#include "nvic.h"
#include "sigfox_api.h"
#include "sigfox_types.h"
#include "sky13317.h"
#include "spi.h"
#include "sx1232.h"
#include "tim.h"
#include "tim_reg.h"

// DEBUG
#include "gpio.h"
#include "mapping.h"

/*** RF API local structures ***/

// Sigfox uplink modulation parameters.
typedef struct {
	// Uplink message frequency.
	unsigned int uplink_frequency_hz;
	// Modulation parameters.
	unsigned short symbol_duration_us;
	volatile unsigned char tim2_event_mask; // Read as [x x x CCR4 CCR3 CCR2 CCR1 ARR].
	unsigned short ramp_duration_us;
	volatile unsigned char phase_shift_required;
	unsigned int frequency_shift_hz;
	volatile unsigned char frequency_shift_direction;
	// Output power range.
	signed char output_power_min;
	signed char output_power_max;
} RF_API_Context;

/*** RF API local global variables ***/

RF_API_Context rf_api_ctx;

/*** RF API local functions ***/

/* TIM2 INTERRUPT HANDLER.
 * @param:	None.
 * @return:	None.
 */
 void TIM2_IRQHandler(void) {

	/* ARR = symbol rate */
	if (((TIM2 -> SR) & (0b1 << TIM2_TIMINGS_ARRAY_ARR_IDX)) != 0) {
		// Update event status (set current and clear previous).
		rf_api_ctx.tim2_event_mask |= (0b1 << TIM2_TIMINGS_ARRAY_ARR_IDX);
		rf_api_ctx.tim2_event_mask &= ~(0b1 << TIM2_TIMINGS_ARRAY_CCR4_IDX);
		// Toggle GPIO (debug).
		//GPIO_Toggle(GPIO_LED);
		// Clear flag.
		TIM2 -> SR &= ~(0b1 << TIM2_TIMINGS_ARRAY_ARR_IDX);
	}

	/* CCR1 = ramp down start */
	else if (((TIM2 -> SR) & (0b1 << TIM2_TIMINGS_ARRAY_CCR1_IDX)) != 0) {
		// Update event status (set current and clear previous).
		rf_api_ctx.tim2_event_mask |= (0b1 << TIM2_TIMINGS_ARRAY_CCR1_IDX);
		rf_api_ctx.tim2_event_mask &= ~(0b1 << TIM2_TIMINGS_ARRAY_ARR_IDX);
		// Toggle GPIO (debug).
		//GPIO_Toggle(GPIO_LED);
		// Clear flag.
		TIM2 -> SR &= ~(0b1 << TIM2_TIMINGS_ARRAY_CCR1_IDX);
	}

	/* CCR2 = ramp down end + frequency shift start */
	else if (((TIM2 -> SR) & (0b1 << TIM2_TIMINGS_ARRAY_CCR2_IDX)) != 0) {
		if (rf_api_ctx.phase_shift_required != 0) {
			if (rf_api_ctx.frequency_shift_direction == 0) {
				// Decrease frequency.
				SX1232_SetRfFrequency(rf_api_ctx.uplink_frequency_hz - rf_api_ctx.frequency_shift_hz);
				rf_api_ctx.frequency_shift_direction = 1;
			}
			else {
				// Increase frequency.
				SX1232_SetRfFrequency(rf_api_ctx.uplink_frequency_hz + rf_api_ctx.frequency_shift_hz);
				rf_api_ctx.frequency_shift_direction = 0;
			}
		}
		// Update event status (set current and clear previous).
		rf_api_ctx.tim2_event_mask |= (0b1 << TIM2_TIMINGS_ARRAY_CCR2_IDX);
		rf_api_ctx.tim2_event_mask &= ~(0b1 << TIM2_TIMINGS_ARRAY_CCR1_IDX);
		// Toggle GPIO (debug).
		//GPIO_Toggle(GPIO_LED);
		// Clear flag.
		TIM2 -> SR &= ~(0b1 << TIM2_TIMINGS_ARRAY_CCR2_IDX);
	}

	/* CCR3 = frequency shift end + ramp-up start */
	else if (((TIM2 -> SR) & (0b1 << TIM2_TIMINGS_ARRAY_CCR3_IDX)) != 0) {
		if (rf_api_ctx.phase_shift_required != 0){
			// Come back to uplink frequency.
			SX1232_SetRfFrequency(rf_api_ctx.uplink_frequency_hz);
		}
		// Update event status (set current and clear previous).
		rf_api_ctx.tim2_event_mask |= (0b1 << TIM2_TIMINGS_ARRAY_CCR3_IDX);
		rf_api_ctx.tim2_event_mask &= ~(0b1 << TIM2_TIMINGS_ARRAY_CCR2_IDX);
		// Toggle GPIO (debug).
		//GPIO_Toggle(GPIO_LED);
		// Clear flag.
		TIM2 -> SR &= ~(0b1 << TIM2_TIMINGS_ARRAY_CCR3_IDX);
	}

	/* CCR4 = ramp-up end */
	else if (((TIM2 -> SR) & (0b1 << TIM2_TIMINGS_ARRAY_CCR4_IDX)) != 0) {
		// Update event status.
		rf_api_ctx.tim2_event_mask |= (0b1 << TIM2_TIMINGS_ARRAY_CCR4_IDX);
		rf_api_ctx.tim2_event_mask &= ~(0b1 << TIM2_TIMINGS_ARRAY_CCR3_IDX);
		// Toggle GPIO (debug).
		//GPIO_Toggle(GPIO_LED);
		// Clear flag.
		TIM2 -> SR &= ~(0b1 << TIM2_TIMINGS_ARRAY_CCR4_IDX);
	}
}

/* UPDATE PARAMETERS ACCORDING TO MODULATION TYPE.
 * @param modulation:	Modulation type asked by Sigfox library.
 * @return:				None.
 */
void RF_API_SetTxModulationParameters(sfx_modulation_type_t modulation) {

	/* Init common parameters */
	rf_api_ctx.phase_shift_required = 0;
	rf_api_ctx.frequency_shift_direction = 0;

	/* Init timings */
	switch (modulation) {
	case SFX_DBPSK_100BPS:
		// 100 bps timings.
		rf_api_ctx.symbol_duration_us = 10000;
		rf_api_ctx.ramp_duration_us = 2000;
		rf_api_ctx.frequency_shift_hz = 400;
		break;
	case SFX_DBPSK_600BPS:
		// 600 bps timings.
		rf_api_ctx.symbol_duration_us = 1667;
		rf_api_ctx.ramp_duration_us = 0; // TBD.
		rf_api_ctx.frequency_shift_hz = 0; // TBD.
		break;
	default:
		break;
	}
}

/* SELECT TX RF PATH ACCORDING TO MODULATION TYPE.
 * @param modulation:	Modulation type asked by Sigfox library.
 * @return:				None.
 */
void RF_API_SetTxPath(sfx_modulation_type_t modulation) {
	switch (modulation) {

	case SFX_DBPSK_100BPS:
		// Assume 100bps corresponds to ETSI configuration (14dBm on PABOOST pin).
		SX1232_SelectRfOutputPin(SX1232_RF_OUTPUT_PIN_PABOOST);
		SKY13317_SetChannel(SKY13317_CHANNEL_RF1);
		rf_api_ctx.output_power_min = SX1232_OUTPUT_POWER_PABOOST_MIN;
		rf_api_ctx.output_power_max = SX1232_OUTPUT_POWER_PABOOST_MAX;
		break;

	case SFX_DBPSK_600BPS:
		// Assume 600bps corresponds to FCC configuration (22dBm with PA on RFO pin).
		SX1232_SelectRfOutputPin(SX1232_RF_OUTPUT_PIN_RFO);
		SKY13317_SetChannel(SKY13317_CHANNEL_RF3);
		rf_api_ctx.output_power_min = SX1232_OUTPUT_POWER_RFO_MIN;
		rf_api_ctx.output_power_max = SX1232_OUTPUT_POWER_RFO_MAX;
		break;

	default:
		break;
	}
}

/* SELECT TX RF PATH ACCORDING TO MODULATION TYPE.
 * @param modulation:	Modulation type asked by Sigfox library.
 * @return:				None.
 */
void RF_API_SetRxPath(void) {
	// Activate LNA.
	SKY13317_SetChannel(SKY13317_CHANNEL_RF2);
}

/*** RF API functions ***/

/*!******************************************************************
 * \fn sfx_u8 RF_API_init(sfx_rf_mode_t rf_mode)
 * \brief Init and configure Radio link in RX/TX
 *
 * [RX Configuration]
 * To receive Sigfox Frame on your device, program the following:
 *  - Preamble  : 0xAAAAAAAAA
 *  - Sync Word : 0xB227
 *  - Packet of the Sigfox frame is 15 bytes length.
 *
 * \param[in] sfx_rf_mode_t rf_mode         Init Radio link in Tx or RX
 * \param[out] none
 *
 * \retval SFX_ERR_NONE:             No error
 * \retval RF_ERR_API_INIT:          Init Radio link error
 *******************************************************************/
sfx_u8 RF_API_init(sfx_rf_mode_t rf_mode) {

	/* Switch RF on and init transceiver */
	SPI_PowerOn();
	SX1232_Init();

	/* Configure transceiver */
	switch (rf_mode) {

	case SFX_RF_MODE_TX:
		// Prepare transceiver for TX operation.
		break;

	case SFX_RF_MODE_RX:
		// Prepare transceiver for RX operation.
		break;

	default:
		break;
	}
	return SFX_ERR_NONE;
}

/*!******************************************************************
 * \fn sfx_u8 RF_API_stop(void)
 * \brief Close Radio link
 *
 * \param[in] none
 * \param[out] none
 *
 * \retval SFX_ERR_NONE:              No error
 * \retval RF_ERR_API_STOP:           Close Radio link error
 *******************************************************************/
sfx_u8 RF_API_stop(void) {

	/* Disable all switch channels */
	SKY13317_SetChannel(SKY13317_CHANNEL_NONE);

	/* Power transceiver down */
	SX1232_SetMode(SX1232_MODE_STANDBY);
	SPI_PowerOff();

	return SFX_ERR_NONE;
}

/*!******************************************************************
 * \fn sfx_u8 RF_API_send(sfx_u8 *stream, sfx_modulation_type_t type, sfx_u8 size)
 * \brief BPSK Modulation of data stream
 * (from synchro bit field to CRC)
 *
 * NOTE : during this function, the voltage_tx needs to be retrieved and stored in
 *        a variable to be returned into the MCU_API_get_voltage_and_temperature or
 *        MCU_API_get_voltage functions.
 *
 * \param[in] sfx_u8 *stream                Complete stream to modulate
 * \param[in]sfx_modulation_type_t          Type of the modulation ( enum with baudrate and modulation information)
 * \param[in] sfx_u8 size                   Length of stream
 * \param[out] none
 *
 * \retval SFX_ERR_NONE:                    No error
 * \retval RF_ERR_API_SEND:                 Send data stream error
 *******************************************************************/
sfx_u8 RF_API_send(sfx_u8 *stream, sfx_modulation_type_t type, sfx_u8 size) {

	/* Disable all interrupts */
	NVIC_DisableInterrupt(IT_RTC);
	NVIC_DisableInterrupt(IT_EXTI4_15);
	NVIC_DisableInterrupt(IT_TIM21);
	NVIC_DisableInterrupt(IT_USART2);
	NVIC_DisableInterrupt(IT_LPUART1);

	/* Set modulation parameters and RF path according to modulation */
	RF_API_SetTxModulationParameters(type);
	RF_API_SetTxPath(type);

	/* Init common variables */
	volatile unsigned int start_time = 0;
	unsigned char output_power_idx = 0;
	unsigned char stream_byte_idx = 0;
	unsigned char stream_bit_idx = 0;

	/* Compute ramp steps */
	unsigned char output_power_dynamic = (rf_api_ctx.output_power_max - rf_api_ctx.output_power_min) + 1;
	unsigned int first_last_ramp_step_duration_us = (rf_api_ctx.symbol_duration_us - 1000) / output_power_dynamic;
	unsigned int ramp_step_duration_us = (rf_api_ctx.ramp_duration_us) / output_power_dynamic;

	/* Compute frequency shift duration required to invert signal phase */
	// Compensate transceiver synthetizer step by programming and reading effective frequencies.
	SX1232_SetRfFrequency(rf_api_ctx.uplink_frequency_hz);
	unsigned int effective_uplink_frequency_hz = SX1232_GetRfFrequency();
	SX1232_SetRfFrequency(rf_api_ctx.uplink_frequency_hz + rf_api_ctx.frequency_shift_hz);
	unsigned int effective_high_shifted_frequency_hz = SX1232_GetRfFrequency();
	SX1232_SetRfFrequency(rf_api_ctx.uplink_frequency_hz - rf_api_ctx.frequency_shift_hz);
	unsigned int effective_low_shifted_frequency_hz = SX1232_GetRfFrequency();
	// Compute average durations = 1 / (2 * delta_f).
	unsigned short high_shifted_frequency_duration_us = (1000000) / (2 * (effective_high_shifted_frequency_hz - effective_uplink_frequency_hz));
	unsigned short low_shifted_frequency_duration_us = (1000000) / (2 * (effective_uplink_frequency_hz - effective_low_shifted_frequency_hz));
	unsigned short frequency_shift_duration_us = (high_shifted_frequency_duration_us + low_shifted_frequency_duration_us) / (2);
	// Compute average idle duration (before and after signal phase inversion).
	unsigned short high_shifted_idle_duration_us = (rf_api_ctx.symbol_duration_us - (2 * rf_api_ctx.ramp_duration_us) - (high_shifted_frequency_duration_us)) / (2);
	unsigned short low_shifted_idle_duration_us = (rf_api_ctx.symbol_duration_us - (2 * rf_api_ctx.ramp_duration_us) - (low_shifted_frequency_duration_us)) / (2);
	unsigned short idle_duration_us = (high_shifted_idle_duration_us + low_shifted_idle_duration_us) / (2);

	/* Configure timer */
	unsigned short dbpsk_timings[TIM2_TIMINGS_ARRAY_LENGTH] = {0};
	dbpsk_timings[TIM2_TIMINGS_ARRAY_ARR_IDX] = rf_api_ctx.symbol_duration_us;
	dbpsk_timings[TIM2_TIMINGS_ARRAY_CCR1_IDX] = idle_duration_us;
	dbpsk_timings[TIM2_TIMINGS_ARRAY_CCR2_IDX] = dbpsk_timings[1] + rf_api_ctx.ramp_duration_us;
	dbpsk_timings[TIM2_TIMINGS_ARRAY_CCR3_IDX] = dbpsk_timings[2] + frequency_shift_duration_us;
	dbpsk_timings[TIM2_TIMINGS_ARRAY_CCR4_IDX] = dbpsk_timings[3] + rf_api_ctx.ramp_duration_us;
	TIM2_Init(TIM2_MODE_SIGFOX, dbpsk_timings);
	rf_api_ctx.tim2_event_mask = 0;

	// DEBUG
	unsigned char sfx_frame[50] = {0x00};

	/* Start CW */
	SX1232_StartCw(rf_api_ctx.uplink_frequency_hz, rf_api_ctx.output_power_min);
	TIM2_Start();

	/* First ramp-up */
	start_time = TIM2_GetCounter();
	for (output_power_idx=0 ; output_power_idx<output_power_dynamic ; output_power_idx++) {
		SX1232_SetRfOutputPower(rf_api_ctx.output_power_min + output_power_idx); // Update output power.
		while (TIM2_GetCounter() < (start_time + (first_last_ramp_step_duration_us * (output_power_idx + 1)))); // Wait until step duration is reached.
	}
	// Wait the end of symbol period.
	while ((rf_api_ctx.tim2_event_mask & (0b1 << TIM2_TIMINGS_ARRAY_ARR_IDX)) == 0);

	/* Data transmission */
	// Byte loop.
	for (stream_byte_idx=0 ; stream_byte_idx<size ; stream_byte_idx++) {
		// Bit loop.
		sfx_frame[stream_byte_idx] = stream[stream_byte_idx];
		for (stream_bit_idx=0 ; stream_bit_idx<8 ; stream_bit_idx++) {
			// Clear ARR flag.
			rf_api_ctx.tim2_event_mask &= ~(0b1 << TIM2_TIMINGS_ARRAY_ARR_IDX);
			// Phase shift required is bit is '0'.
			if ((stream[stream_byte_idx] & (0b1 << (7-stream_bit_idx))) == 0) {
				// Update flag.
				rf_api_ctx.phase_shift_required = 1;
				// First idle period.
				while ((rf_api_ctx.tim2_event_mask & (0b1 << TIM2_TIMINGS_ARRAY_CCR1_IDX)) == 0);
				// Ramp down.
				start_time = TIM2_GetCounter();
				for (output_power_idx=0 ; output_power_idx<output_power_dynamic ; output_power_idx++) {
					SX1232_SetRfOutputPower(rf_api_ctx.output_power_max - output_power_idx);
					while (TIM2_GetCounter() < (start_time + (ramp_step_duration_us * (output_power_idx + 1)))); // Wait until step duration is reached.
				}
				// Frequency shift is made in timer interrupt.
				while ((rf_api_ctx.tim2_event_mask & (0b1 << TIM2_TIMINGS_ARRAY_CCR3_IDX)) == 0);
				// Ramp-up.
				start_time = TIM2_GetCounter();
				for (output_power_idx=0 ; output_power_idx<output_power_dynamic ; output_power_idx++) {
					SX1232_SetRfOutputPower(rf_api_ctx.output_power_min + output_power_idx);
					while (TIM2_GetCounter() < (start_time + (ramp_step_duration_us * (output_power_idx + 1)))); // Wait until step duration is reached.
				}
			}
			else {
				rf_api_ctx.phase_shift_required = 0;
			}
			// Wait the end of symbol period.
			while ((rf_api_ctx.tim2_event_mask & (0b1 << TIM2_TIMINGS_ARRAY_ARR_IDX)) == 0);
		}
	}

	/* Last ramp down */
	start_time = TIM2_GetCounter();
	for (output_power_idx=0 ; output_power_idx<output_power_dynamic ; output_power_idx++) {
		SX1232_SetRfOutputPower(rf_api_ctx.output_power_max - output_power_idx);
		while (TIM2_GetCounter() < (start_time + (first_last_ramp_step_duration_us * (output_power_idx + 1)))); // Wait until step duration is reached.
	}
	// Wait the end of symbol period.
	while ((rf_api_ctx.tim2_event_mask & (0b1 << TIM2_TIMINGS_ARRAY_ARR_IDX)) == 0);

	/* Stop CW */
	TIM2_Stop();
	SX1232_StopCw();

	/* Re-enable all interrupts */
#if (defined IM_RTC || defined CM_RTC)
	NVIC_EnableInterrupt(IT_RTC);
#endif
#ifdef CM_RTC
	NVIC_EnableInterrupt(IT_EXTI4_15);
	NVIC_EnableInterrupt(IT_TIM21);
#endif
#ifdef ATM
	NVIC_EnableInterrupt(IT_USART2);
#endif

	if (sfx_frame[0] == 0) {
		NVIC_EnableInterrupt(IT_USART2);
	}

	return SFX_ERR_NONE;
}

/*!******************************************************************
 * \fn sfx_u8 RF_API_start_continuous_transmission (sfx_modulation_type_t type)
 * \brief Generate a signal with modulation type. All the configuration ( Init of the RF and Frequency have already been executed
 *        when this function is called.
 *
 * \param[in] sfx_modulation_type_t         Type of the modulation ( enum with baudrate and modulation information is contained in sigfox_api.h)
 *
 * \retval SFX_ERR_NONE:                                 No error
 * \retval RF_ERR_API_START_CONTINUOUS_TRANSMISSION:     Continuous Transmission Start error
 *******************************************************************/
sfx_u8 RF_API_start_continuous_transmission (sfx_modulation_type_t type) {

	/* Select RF TX path according to modulation */
	RF_API_SetTxPath(type);

	/* Start CW */
	SX1232_StartCw(rf_api_ctx.uplink_frequency_hz, rf_api_ctx.output_power_max);

	return SFX_ERR_NONE;
}

/*!******************************************************************
 * \fn sfx_u8 RF_API_stop_continuous_transmission (void)
 * \brief Stop the current continuous transmisssion
 *
 * \retval SFX_ERR_NONE:                                 No error
 * \retval RF_ERR_API_STOP_CONTINUOUS_TRANSMISSION:      Continuous Transmission Stop error
 *******************************************************************/
sfx_u8 RF_API_stop_continuous_transmission (void) {

	/* Stop CW */
	SX1232_StopCw();

	return SFX_ERR_NONE;
}

/*!******************************************************************
 * \fn sfx_u8 RF_API_change_frequency(sfx_u32 frequency)
 * \brief Change synthesizer carrier frequency
 *
 * \param[in] sfx_u32 frequency             Frequency in Hz to program in the radio chipset
 * \param[out] none
 *
 * \retval SFX_ERR_NONE:                    No error
 * \retval RF_ERR_API_CHANGE_FREQ:          Change frequency error
 *******************************************************************/
sfx_u8 RF_API_change_frequency(sfx_u32 frequency) {

	/* Save frequency */
	rf_api_ctx.uplink_frequency_hz = frequency;

	return SFX_ERR_NONE;
}

/*!******************************************************************
 * \fn sfx_u8 RF_API_wait_frame(sfx_u8 *frame, sfx_s16 *rssi, sfx_rx_state_enum_t * state)
 * \brief Get all GFSK frames received in Rx buffer, structure of
 * frame is : Synchro bit + Synchro frame + 15 Bytes.<BR> This function must
 * be blocking state since data is received or timer of 25 s has elapsed.
 *
 * - If received buffer, function returns SFX_ERR_NONE then the
 *   library will try to decode frame. If the frame is not correct, the
 *   library will recall RF_API_wait_frame.
 *
 * - If 25 seconds timer has elapsed, function returns into the state the timeout enum code.
 *   and then library will stop receive frame phase.
 *
 * \param[in] none
 * \param[out] sfx_s8 *frame                  Receive buffer
 * \param[out] sfx_s16 *rssi                  Chipset RSSI
 * Warning: This is the 'raw' RSSI value. Do not add 100 as made
 * in Library versions 1.x.x
 * Resolution: 1 LSB = 1 dBm
 *
 * \param[out] sfx_rx_state_enum_t state      Indicate the final state of the reception. Value can be DL_TIMEOUT or DL_PASSED
 *                                            if a frame has been received, as defined in sigfox_api.h file.
 *
 * \retval SFX_ERR_NONE:                      No error
 *******************************************************************/
sfx_u8 RF_API_wait_frame(sfx_u8 *frame, sfx_s16 *rssi, sfx_rx_state_enum_t * state) {
	return SFX_ERR_NONE;
}

/*!******************************************************************
 * \fn sfx_u8 RF_API_wait_for_clear_channel (sfx_u8 cs_min, sfx_s8 cs_threshold, sfx_rx_state_enum_t * state);
 * \brief This function is used in ARIB standard for the Listen Before Talk
 *        feature. It listens on a specific frequency band initialized through the RF_API_init(), during a sliding window set
 *        in the MCU_API_timer_start_carrier_sense().
 *        If the channel is clear during the minimum carrier sense
 *        value (cs_min), under the limit of the cs_threshold,
 *        the functions returns with SFX_ERR_NONE (transmission
 *        allowed). Otherwise it continues to listen to the channel till the expiration of the
 *        carrier sense maximum window and then updates the state ( with timeout enum ).
 *
 * \param[in] none
 * \param[out] sfx_u8 cs_min                  Minimum Carrier Sense time in ms.
 * \param[out] sfx_s8 cs_threshold            Power threshold limit to declare the channel clear.
 *                                            i.e : cs_threshold value -80dBm in Japan / -65dBm in Korea
 * \param[out] sfx_rx_state_enum_t state      Indicate the final state of the carrier sense. Value can be DL_TIMEOUT or PASSED
 *                                            as per defined in sigfox_api.h file.
 *
 * \retval SFX_ERR_NONE:                      No error
 *******************************************************************/
sfx_u8 RF_API_wait_for_clear_channel(sfx_u8 cs_min, sfx_s8 cs_threshold, sfx_rx_state_enum_t * state) {
	return SFX_ERR_NONE;
}

/*!******************************************************************
 * \fn sfx_u8 RF_API_get_version(sfx_u8 **version, sfx_u8 *size)
 * \brief Returns current RF API version
 *
 * \param[out] sfx_u8 **version                 Pointer to Byte array (ASCII format) containing library version
 * \param[out] sfx_u8 *size                     Size of the byte array pointed by *version
 *
 * \retval SFX_ERR_NONE:                No error
 * \retval RF_ERR_API_GET_VERSION:      Get Version error
 *******************************************************************/
sfx_u8 RF_API_get_version(sfx_u8 **version, sfx_u8 *size) {
	return SFX_ERR_NONE;
}
