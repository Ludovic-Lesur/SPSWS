/*
 * neom8n.c
 *
 *  Created on: 11 aug 2018
 *      Author: Ludovic
 */

#include "neom8n.h"

#include "dma.h"
#include "gpio_reg.h"
#include "iwdg.h"
#include "lpuart.h"
#include "rcc_reg.h"
#include "tim.h"

/*** NEO-M8N local macros ***/

#define UBX_MSG_OVERHEAD_LENGTH				8 // 6 bytes header + 2 bytes checksum.
#define UBX_CHECKSUM_OVERHEAD_LENGTH		4
#define UBX_CHECKSUM_OFFSET					2
#define UBX_CFG_MSG_PAYLOAD_LENGTH			8

#define NMEA_RX_BUFFER_SIZE					128

#define NMEA_MESSAGE_START_CHAR				'$'

#define NMEA_SEP							','
#define NMEA_DOT							'.'

#define NMEA_ZDA_MASK						0x00020000 // Provided to NEOM8N_SelectNmeaMessages() function.
#define NMEA_ZDA_ADDRESS_FIELD_LENGTH		6
#define NMEA_ZDA_TIME_FIELD_LENGTH			9
#define NMEA_ZDA_DAY_FIELD_LENGTH			2
#define NMEA_ZDA_MONTH_FIELD_LENGTH			2
#define NMEA_ZDA_YEAR_FIELD_LENGTH			4

#define NMEA_GGA_MASK						0x00000008 // Provided to NEOM8N_SelectNmeaMessages() function.
#define NMEA_GGA_ADDRESS_FIELD_LENGTH		6
#define NMEA_GGA_LAT_FIELD_LENGTH			10
#define NMEA_GGA_NS_FIELD_LENGTH			1
#define NMEA_GGA_NORTH						'N'
#define NMEA_GGA_SOUTH						'S'
#define NMEA_GGA_LONG_FIELD_LENGTH			11
#define NMEA_GGA_EO_FIELD_LENGTH			1
#define NMEA_GGA_EAST						'E'
#define NMEA_GGA_WEST						'O'
#define NMEA_GGA_ALT_UNIT_FIELD_LENGTH		1
#define NMEA_GGA_METERS						'M'

#define NMEA_CHECKSUM_START_CHAR			'*' // To skip '$'.

/*** NEO-M8N local structures ***/

typedef struct {
	// Buffers.
	unsigned char nmea_rx_buf1[NMEA_RX_BUFFER_SIZE]; 	// NMEA input messages buffer 1.
	unsigned char nmea_rx_buf2[NMEA_RX_BUFFER_SIZE]; 	// NMEA input messages buffer 2.
	unsigned char nmea_rx_fill_buf1;					// 0/1 = buffer 2/1 is currently filled by DMA, buffer 1/2 is ready to be parsed.
	unsigned char nmea_rx_lf_flag;						// Set to '1' as soon as a complete NMEA message is received.
	// Parsing.
	unsigned char nmea_zda_parsing_success;				// Set to '1' as soon an NMEA ZDA message was successfully parsed.
	unsigned char nmea_zda_data_valid;					// set to '1' if retrieved NMEA ZDA data is valid.
	unsigned char nmea_gga_parsing_success;				// Set to '1' as soon an NMEA GGA message was successfully parsed.
	unsigned char nmea_gga_data_valid;					// set to '1' if retrieved NMEA GGA data is valid.
} NEOM8N_Context;

/*** NEO-M8N local global variables ***/

static NEOM8N_Context neom8n_ctx;

/*** NEO-M8N local functions ***/

/* CONVERTS THE ASCII CODE OF A DECIMAL CHARACTER TO THE CORRESPONDING VALUE.
 * @param c:			Hexadecimal character to convert.
 * @return hexa_value:	Result of conversion.
 */
unsigned char AsciiToDecimal(unsigned char c) {
	unsigned char value = 0;
	if ((c >= '0') && (c <= '9')) {
		value = c - '0';
	}
	else {
		if ((c >= 'A') && (c <= 'F')) {
			value = c - 'A' + 10;
		}
	}
	return value;
}

/* COMPUTE A POWER A 10.
 * @param power:	The desired power.
 * @return result:	Result of computation.
 */
unsigned int Pow10(unsigned char n) {
	unsigned int result = 0;
	unsigned int pow10_buf[9] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};
	if (n <= 9) {
		result = pow10_buf[n];
	}
	return result;
}

/* COMPUTE AND APPEND CHECKSUM TO AN UBX MESSAGE.
 * @param ubx_command:		Complete UBX message for which checksum must be computed.
 * @param payload_length:	Length of the payload (in bytes) for this message.
 * @return:					None.
 */
void NEOM8N_ComputeUbxChecksum(unsigned char* ubx_command, unsigned char payload_length) {
	// See algorithme on p.136 of NEO-M8 programming manual.
	unsigned char ck_a = 0;
	unsigned char ck_b = 0;
	unsigned int checksum_idx = 0;
	for (checksum_idx=UBX_CHECKSUM_OFFSET ; checksum_idx<(UBX_CHECKSUM_OFFSET+UBX_CHECKSUM_OVERHEAD_LENGTH+payload_length) ; checksum_idx++) {
		ck_a = ck_a + ubx_command[checksum_idx];
		ck_b = ck_b + ck_a;
	}
	// Fill two last bytes of the UBX message with CK_A and CK_B.
	ubx_command[checksum_idx] = ck_a;
	ubx_command[checksum_idx+1] = ck_b;
}

/* GET THE CHECKSUM OF A GIVEN NMEA MESSAGE.
 * @param:		None;
 * @return ck:	Computed checksum.
 */
unsigned char NEOM8N_GetNmeaChecksum(unsigned char* nmea_rx_buf) {
	// See NMEA messages format on p.105 of NEO-M8 programming manual.
	unsigned char ck = 0;
	// Get checksum start index.
	unsigned char checksum_start_char_idx = 0;
	while ((nmea_rx_buf[checksum_start_char_idx] != NMEA_CHECKSUM_START_CHAR) && (checksum_start_char_idx < NMEA_RX_BUFFER_SIZE)) {
		checksum_start_char_idx++;
	}
	if (checksum_start_char_idx < NMEA_RX_BUFFER_SIZE) {
		ck = (AsciiToDecimal(nmea_rx_buf[checksum_start_char_idx+1]) << 4) + AsciiToDecimal(nmea_rx_buf[checksum_start_char_idx+2]);
	}
	return ck;
}

/* COMPUTE THE CHECKSUM OF A GIVEN NMEA MESSAGE.
 * @param:		None;
 * @return ck:	Computed checksum.
 */
unsigned char NEOM8N_ComputeNmeaChecksum(unsigned char* nmea_rx_buf) {
	// See algorithme on p.105 of NEO-M8 programming manual.
	unsigned char ck = 0;
	// Get message start index.
	unsigned char message_start_char_idx = 0;
	while ((nmea_rx_buf[message_start_char_idx] != NMEA_MESSAGE_START_CHAR) && (message_start_char_idx < NMEA_RX_BUFFER_SIZE)) {
		message_start_char_idx++;
	}
	// Get checksum start index.
	unsigned char checksum_start_char_idx = message_start_char_idx;
	while ((nmea_rx_buf[checksum_start_char_idx] != NMEA_CHECKSUM_START_CHAR) && (checksum_start_char_idx < NMEA_RX_BUFFER_SIZE)) {
		checksum_start_char_idx++;
	}
	if (checksum_start_char_idx < NMEA_RX_BUFFER_SIZE) {
		unsigned char checksum_idx;
		for (checksum_idx=(message_start_char_idx+1); checksum_idx<checksum_start_char_idx ; checksum_idx++) {
			ck ^= nmea_rx_buf[checksum_idx]; // Exclusive OR of all characters between '$' and '*'.
		}
	}
	return ck;
}

/* DECODE AN NMEA ZDA MESSAGE.
 * @param nmea_rx_buf:	NMEA message to decode.
 * @return:				None.
 */
void NEOM8N_ParseNmeaZdaMessage(unsigned char* nmea_rx_buf, GPS_TimestampData* gps_timestamp) {
	unsigned char error_found = 0;
	unsigned char idx = 0;

	/* Verify checksum */
	unsigned char received_checksum = NEOM8N_GetNmeaChecksum(nmea_rx_buf);
	unsigned char computed_checksum = NEOM8N_ComputeNmeaChecksum(nmea_rx_buf);

	if (computed_checksum == received_checksum) {

		/* Extract NMEA data (see ZDA message format on p.127 of NEO-M8 programming manual) */
		unsigned char sep_idx = 0;
		while ((nmea_rx_buf[sep_idx] != NMEA_MESSAGE_START_CHAR) && (sep_idx < NMEA_RX_BUFFER_SIZE)) {
			sep_idx++;
		}
		unsigned char field = 0;
		while ((nmea_rx_buf[idx] != NMEA_LF) && (idx < NMEA_RX_BUFFER_SIZE)) {
			if (nmea_rx_buf[idx] == NMEA_SEP) {
				field++;
				unsigned int k = 0; // Generic index used in local for loops.
				switch (field) {

				/* Field 1 = address = <ID><message> */
				case 1:

					if (idx == NMEA_ZDA_ADDRESS_FIELD_LENGTH) {
						/* Check if message = 'ZDA' */
						if ((nmea_rx_buf[sep_idx+3] != 'Z') || (nmea_rx_buf[sep_idx+4] != 'D') || (nmea_rx_buf[sep_idx+5] != 'A')) {
							error_found = 1;
						}
					}
					else {
						error_found = 1;
					}
					break;

				/* Field 2 = time = <hhmmss.ss> */
				case 2:
					if ((idx-sep_idx) == (NMEA_ZDA_TIME_FIELD_LENGTH+1)) {
						(*gps_timestamp).time_hours = AsciiToDecimal(nmea_rx_buf[sep_idx+1])*10 + AsciiToDecimal(nmea_rx_buf[sep_idx+2]);
						(*gps_timestamp).time_minutes = AsciiToDecimal(nmea_rx_buf[sep_idx+3])*10 + AsciiToDecimal(nmea_rx_buf[sep_idx+4]);
						(*gps_timestamp).time_seconds = AsciiToDecimal(nmea_rx_buf[sep_idx+5])*10 + AsciiToDecimal(nmea_rx_buf[sep_idx+6]);
					}
					else {
						error_found = 1;
					}
					break;

				/* Field 3 = day = <dd> */
				case 3:
					if ((idx-sep_idx) == (NMEA_ZDA_DAY_FIELD_LENGTH+1)) {
						(*gps_timestamp).date_day = AsciiToDecimal(nmea_rx_buf[sep_idx+1])*10 + AsciiToDecimal(nmea_rx_buf[sep_idx+2]);
					}
					else {
						error_found = 1;
					}
					break;

				case 4:
					if ((idx-sep_idx) == (NMEA_ZDA_MONTH_FIELD_LENGTH+1)) {
						(*gps_timestamp).date_month = AsciiToDecimal(nmea_rx_buf[sep_idx+1])*10 + AsciiToDecimal(nmea_rx_buf[sep_idx+2]);
					}
					else {
						error_found = 1;
					}
					break;

				case 5:
					if ((idx-sep_idx) == (NMEA_ZDA_YEAR_FIELD_LENGTH+1)) {
						(*gps_timestamp).date_year = 0;
						for (k=0 ; k<NMEA_ZDA_YEAR_FIELD_LENGTH ; k++) {
							(*gps_timestamp).date_year += Pow10(NMEA_ZDA_YEAR_FIELD_LENGTH-1-k)*AsciiToDecimal(nmea_rx_buf[sep_idx+1+k]);
						}
						// Last field retrieved, parsing process succeeded.
						neom8n_ctx.nmea_zda_parsing_success = 1;
					}
					else {
						error_found = 1;
					}
					break;

				/* Unused fields */
				default:
					break;
				}
				sep_idx = idx; // Update separator index.
			}

			/* Check syntax error flag */
			if (error_found == 1) {
				// Reset buffer and exit decoding loop as soon as an error occured.
				for (idx=0 ; idx<NMEA_RX_BUFFER_SIZE ; idx++) nmea_rx_buf[idx] = 0;
				break;
			}

			/* Increment index */
			idx++;
		}
	}
	else {
		// Reset buffer.
		for (idx=0 ; idx<NMEA_RX_BUFFER_SIZE ; idx++) nmea_rx_buf[idx] = 0;
	}
}

/* DECODE AN NMEA GGA MESSAGE.
 * @param nmea_rx_buf:	NMEA message to decode.
 * @return:						None.
 */
void NEOM8N_ParseNmeaGgaMessage(unsigned char* nmea_rx_buf, GPS_PositionData* gps_position) {
	unsigned char error_found = 0;
	unsigned char idx = 0;

	/* Verify checksum */
	unsigned char received_checksum = NEOM8N_GetNmeaChecksum(nmea_rx_buf);
	unsigned char computed_checksum = NEOM8N_ComputeNmeaChecksum(nmea_rx_buf);

	if (computed_checksum == received_checksum) {

		/* Extract NMEA data (see GGA message format on p.114 of NEO-M8 programming manual) */
		unsigned char sep_idx = 0;
		while ((nmea_rx_buf[sep_idx] != NMEA_MESSAGE_START_CHAR) && (sep_idx < NMEA_RX_BUFFER_SIZE)) {
			sep_idx++;
		}
		unsigned char field = 0;
		unsigned char alt_field_length = 0;
		unsigned char alt_number_of_digits = 0;
		while ((nmea_rx_buf[idx] != NMEA_LF) && (idx < NMEA_RX_BUFFER_SIZE)) {
			if (nmea_rx_buf[idx] == NMEA_SEP) {
				field++;
				unsigned int k = 0; // Generic index used in local for loops.
				switch (field) {

				/* Field 1 = address = <ID><message> */
				case 1:

					if (idx == NMEA_GGA_ADDRESS_FIELD_LENGTH) {
						/* Check if message = 'GGA' */
						if ((nmea_rx_buf[sep_idx+3] != 'G') || (nmea_rx_buf[sep_idx+4] != 'G') || (nmea_rx_buf[sep_idx+5] != 'A')) {
							error_found = 1;
						}
					}
					else {
						error_found = 1;
					}
					break;

				/* Field 3 = latitude = <ddmm.mmmmm> */
				case 3:
					if ((idx-sep_idx) == (NMEA_GGA_LAT_FIELD_LENGTH+1)) {
						(*gps_position).lat_degrees = AsciiToDecimal(nmea_rx_buf[sep_idx+1])*10+AsciiToDecimal(nmea_rx_buf[sep_idx+2]);
						(*gps_position).lat_minutes = AsciiToDecimal(nmea_rx_buf[sep_idx+3])*10+AsciiToDecimal(nmea_rx_buf[sep_idx+4]);
						(*gps_position).lat_seconds = 0;
						for (k=0 ; k<5 ; k++) {
							(*gps_position).lat_seconds += Pow10(4-k)*AsciiToDecimal(nmea_rx_buf[sep_idx+6+k]);
						}
					}
					else {
						error_found = 1;
					}
					break;

				/* Field 4 = <N> or <S> */
				case 4:
					if ((idx-sep_idx) == (NMEA_GGA_NS_FIELD_LENGTH+1)) {
						switch (nmea_rx_buf[sep_idx+1]) {
						case NMEA_GGA_NORTH:
							(*gps_position).lat_north = 1;
							break;
						case NMEA_GGA_SOUTH:
							(*gps_position).lat_north = 0;
							break;
						default:
							(*gps_position).lat_north = 0;
							error_found = 1;
							break;
						}
					}
					else {
						error_found = 1;
					}
					break;

				/* Field 5 = longitude = <dddmm.mmmmm> */
				case 5:
					if ((idx-sep_idx) == (NMEA_GGA_LONG_FIELD_LENGTH+1)) {
						(*gps_position).long_degrees = 0;
						for (k=0 ; k<3 ; k++) {
							(*gps_position).long_degrees += Pow10(2-k)*AsciiToDecimal(nmea_rx_buf[sep_idx+1+k]);
						}
						(*gps_position).long_minutes = AsciiToDecimal(nmea_rx_buf[sep_idx+4])*10+AsciiToDecimal(nmea_rx_buf[sep_idx+5]);
						(*gps_position).long_seconds = 0;
						for (k=0 ; k<5 ; k++) {
							(*gps_position).long_seconds += Pow10(4-k)*AsciiToDecimal(nmea_rx_buf[sep_idx+7+k]);
						}
					}
					else {
						error_found = 1;
					}
					break;

				/* Field 6 = <E> or <O> */
				case 6:
					if ((idx-sep_idx) == (NMEA_GGA_EO_FIELD_LENGTH+1)) {
						switch (nmea_rx_buf[sep_idx+1]) {
						case NMEA_GGA_EAST:
							(*gps_position).long_east = 1;
							break;
						case NMEA_GGA_WEST:
							(*gps_position).long_east = 0;
							break;
						default:
							(*gps_position).long_east = 0;
							error_found = 1;
							break;
						}
					}
					else {
						error_found = 1;
					}
					break;

				/* Field 10 = altitude */
				case 10:
					alt_field_length = (idx-sep_idx)-1;
					if (alt_field_length >= 1) {

						/* Get number of digits of integer part (search dot) */
						for (alt_number_of_digits=0 ; alt_number_of_digits<alt_field_length ; alt_number_of_digits++) {
							if (nmea_rx_buf[sep_idx+1+alt_number_of_digits] == NMEA_DOT) {
								break; // Dot found, stop counting integer part length.
							}
						}

						/* Compute integer part */
						if (alt_number_of_digits > 0) {
							(*gps_position).altitude = 0;
							for (k=0 ; k<alt_number_of_digits ; k++) {
								(*gps_position).altitude += Pow10(alt_number_of_digits-1-k)*AsciiToDecimal(nmea_rx_buf[sep_idx+1+k]);
							}

							/* Rounding operation if fractionnal part exists (not required for success) */
							if ((idx-(sep_idx+alt_number_of_digits)-1) >= 2) {
								if (AsciiToDecimal(nmea_rx_buf[sep_idx+alt_number_of_digits+2]) >= 5) {
									(*gps_position).altitude++; // Add '1' to altitude.
								}
							}
						}
						else {
							error_found = 1;
						}
					}
					else {
						error_found = 1;
					}
					break;

				/* Field 11 = altitude unit */
				case 11:
					if ((idx-sep_idx) == (NMEA_GGA_ALT_UNIT_FIELD_LENGTH+1)) {
						if (nmea_rx_buf[sep_idx+1] == NMEA_GGA_METERS) {
							// Last field retrieved, parsing process succeeded.
							neom8n_ctx.nmea_gga_parsing_success = 1;
						}
						else {
							error_found = 1;
						}
					}
					else {
						error_found = 1;
					}
					break;

				/* Unused fields */
				default:
					break;
				}
				sep_idx = idx; // Update separator index.
			}

			/* Check syntax error flag */
			if (error_found == 1) {
				// Reset buffer and exit decoding loop as soon as an error occured.
				for (idx=0 ; idx<NMEA_RX_BUFFER_SIZE ; idx++) nmea_rx_buf[idx] = 0;
				break;
			}

			/* Increment index */
			idx++;
		}
	}
	else {
		// Reset buffer.
		for (idx=0 ; idx<NMEA_RX_BUFFER_SIZE ; idx++) nmea_rx_buf[idx] = 0;
	}
}

/* INDICATE IF A GPS POSITION IS VALID.
 * @param local_gps_position:	GPS position structure to analyse.
 * @return gps_position_valid:	1 if GPS position is valid, 0 otherwise.
 */
unsigned char NEOM8N_PositionIsValid(GPS_PositionData local_gps_position) {
	unsigned char gps_position_valid = 0;
	if ((local_gps_position.lat_degrees >= 0) && (local_gps_position.lat_degrees <= 89) &&
		(local_gps_position.lat_minutes >= 0) && (local_gps_position.lat_minutes <= 59) &&
		(local_gps_position.lat_seconds >= 0) && (local_gps_position.lat_seconds <= 99999) &&
		(local_gps_position.long_degrees >= 0) && (local_gps_position.long_degrees <= 179) &&
		(local_gps_position.long_minutes >= 0) && (local_gps_position.long_minutes <= 59) &&
		(local_gps_position.long_seconds >= 0) && (local_gps_position.long_seconds <= 99999)) {
		gps_position_valid = 1;
	}
	return gps_position_valid;
}

/* SEND UBX COMMANDS TO SELECT NMEA MESSAGES TO OUTPUT.
 * @param nmea_message_id_mask:	Binary mask to enable or disable each NMEA standard message, coded as follow:
 * 								0b <ZDA> <VTG> <VLW> <TXT> <RMC> <GSV> <GST> <GSA> <GRS> <GPQ> <GND> <GNQ> <GLQ> <GLL> <GGA> <GBS> <GBQ> <DTM>.
 * @return:						None.
 */
void NEOM8N_SelectNmeaMessages(unsigned int nmea_message_id_mask) {
	// See p.110 for NMEA messages ID.
	unsigned char nmea_message_id[18] = {0x0A, 0x44, 0x09, 0x00, 0x01, 0x43, 0x42, 0x0D, 0x40, 0x06, 0x02, 0x07, 0x03, 0x04, 0x41, 0x0F, 0x05, 0x08};
	unsigned char nmea_message_id_idx = 0;
	// See p.174 for UBX message format.
	unsigned char ubx_cfg_msg[UBX_MSG_OVERHEAD_LENGTH+UBX_CFG_MSG_PAYLOAD_LENGTH] = {0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	unsigned char ubx_cfg_msg_idx = 0;
	// Send commands.
	for (nmea_message_id_idx=0 ; nmea_message_id_idx<18 ; nmea_message_id_idx++) {
		// Byte 7 = is the ID of the message to enable or disable.
		ubx_cfg_msg[7] = nmea_message_id[nmea_message_id_idx];
		// Bytes 8-13 = message rate.
		unsigned char rate_value = 0;
		if ((nmea_message_id_mask & (0b1 << nmea_message_id_idx)) != 0) {
			rate_value = 1;
		}
		for (ubx_cfg_msg_idx=8 ; ubx_cfg_msg_idx<14 ; ubx_cfg_msg_idx++) {
			ubx_cfg_msg[ubx_cfg_msg_idx] = rate_value;
		}
		// Bytes 14-15 = UBX checksum (CK_A and CK_B).
		NEOM8N_ComputeUbxChecksum(ubx_cfg_msg, UBX_CFG_MSG_PAYLOAD_LENGTH);
		for (ubx_cfg_msg_idx=0 ; ubx_cfg_msg_idx<(UBX_MSG_OVERHEAD_LENGTH+UBX_CFG_MSG_PAYLOAD_LENGTH) ; ubx_cfg_msg_idx++) {
			LPUART_SendByte(ubx_cfg_msg[ubx_cfg_msg_idx]); // Send command.
		}
		TIM_TimeWaitMilliseconds(100);
	}
}

/*** NEO-M8N functions ***/

/* INIT NEO-M8N MODULE.
 * @param:	None.
 * @return:	None.
 */
void NEOM8N_Init(void) {

	/* Init context */
	unsigned int byte_idx = 0;
	for (byte_idx=0 ; byte_idx<NMEA_RX_BUFFER_SIZE ; byte_idx++) neom8n_ctx.nmea_rx_buf1[byte_idx] = 0;
	for (byte_idx=0 ; byte_idx<NMEA_RX_BUFFER_SIZE ; byte_idx++) neom8n_ctx.nmea_rx_buf2[byte_idx] = 0;
	neom8n_ctx.nmea_rx_fill_buf1 = 1;
	neom8n_ctx.nmea_rx_lf_flag = 0;
	neom8n_ctx.nmea_zda_parsing_success = 0;
	neom8n_ctx.nmea_zda_data_valid = 0;
	neom8n_ctx.nmea_gga_parsing_success = 0;
	neom8n_ctx.nmea_gga_data_valid = 0;

	/* Init LPUART and DMA */
	LPUART_Init();
	DMA_LpuartRxInit();

	/* Switch GPS module on */
	RCC -> IOPENR |= (0b1 << 1); // Enable GPIOB clock.
	GPIOB -> MODER &= ~(0b11 << 10); // Reset bits 10-11.
	GPIOB -> MODER |= (0b01 << 10); // Configure PB5 as output.
	GPIOB -> ODR |= (0b1 << 5); // Switch on GPS.
}

/* SWITCH NEO-M8N MODULE OFF.
 * @param:	None.
 * @return:	None.
 */
void NEOM8N_Off(void) {
	// Switch DMA and LPUART off.
	DMA_LpuartRxOff();
	LPUART_Off();
	// Switch GPS module off.
	GPIOB -> ODR &= ~(0b1 << 5);
}

/* GET CURRENT GPS TIMESTAMP VIA ZDA NMEA  MESSAGES.
 * @param gps_timestamp:	Pointer to GPS timestamp structure that will contain the data.
 * @param timeout_seconds:	Timeout in seconds.
 * @return return_code:		See NEOM8N_ReturnCode structure in neom8n.h.
 */
NEOM8N_ReturnCode NEOM8N_GetTimestamp(GPS_TimestampData* gps_timestamp, unsigned char timeout_seconds) {
	NEOM8N_ReturnCode return_code = NEOM8N_TIMEOUT;
	// Select ZDA message to get complete timestamp.
	NEOM8N_SelectNmeaMessages(NMEA_ZDA_MASK);
	// Start DMA.
	DMA_LpuartRxStop();
	DMA_LpuartRxSetDestAddr((unsigned int) &(neom8n_ctx.nmea_rx_buf1), NMEA_RX_BUFFER_SIZE); // Start with buffer 1.
	neom8n_ctx.nmea_rx_fill_buf1 = 1;
	DMA_LpuartRxStart();
	LPUART_StartRx();
	// Save fix start time.
	unsigned int fix_start_time = TIM_TimeGetSeconds();
	// Loop until data is retrieved or timeout expired.
	while ((TIM_TimeGetSeconds() < fix_start_time+timeout_seconds) && (neom8n_ctx.nmea_zda_data_valid == 0)) {
		// Reload watchdog counter.
		IWDG_Reload();
		// Check LF flag to trigger parsing process.
		if (neom8n_ctx.nmea_rx_lf_flag == 1) {
			// Decode incoming NMEA message.
			if (neom8n_ctx.nmea_rx_fill_buf1 == 1) {
				NEOM8N_ParseNmeaZdaMessage(neom8n_ctx.nmea_rx_buf2, gps_timestamp); // Buffer 1 is currently filled by DMA, buffer 2 is available for parsing.
			}
			else {
				NEOM8N_ParseNmeaZdaMessage(neom8n_ctx.nmea_rx_buf1, gps_timestamp); // Buffer 2 is currently filled by DMA, buffer 1 is available for parsing.
			}
			if (neom8n_ctx.nmea_zda_parsing_success == 1) {
				return_code = NEOM8N_INVALID_DATA;
				// Check data.
				if (NEOM8N_TimestampIsValid(*gps_timestamp) == 1) {
					neom8n_ctx.nmea_zda_data_valid = 1;
					return_code = NEOM8N_SUCCESS;
				}
				else {
					neom8n_ctx.nmea_zda_parsing_success = 0;
				}
			}
			// Wait for next message.
			neom8n_ctx.nmea_rx_lf_flag = 0;
		}
	}
	// Stop DMA.
	LPUART_StopRx();
	DMA_LpuartRxStop();
	// Reset GPS timestamp data in case of failure.
	if (return_code != NEOM8N_SUCCESS) {
		(*gps_timestamp).date_day = 0;
		(*gps_timestamp).date_month = 0;
		(*gps_timestamp).date_year = 0;
		(*gps_timestamp).time_hours = 0;
		(*gps_timestamp).time_minutes = 0;
		(*gps_timestamp).time_seconds = 0;
	}
	return return_code;
}

/* INDICATE IF A GPS TIMESTAMP IS VALID.
 * @param gps_timestamp:		GPS timestamp structure to analyse.
 * @return gps_timestamp_valid:	1 if GPS timestamp is valid, 0 otherwise.
 */
unsigned char NEOM8N_TimestampIsValid(GPS_TimestampData local_gps_timestamp) {
	unsigned char gps_timestamp_valid = 0;
	if ((local_gps_timestamp.date_day >= 1) && (local_gps_timestamp.date_day <= 31) &&
		(local_gps_timestamp.date_month >= 1) && (local_gps_timestamp.date_month <= 12) &&
		(local_gps_timestamp.date_year >= 0) && (local_gps_timestamp.date_day <= 99) &&
		(local_gps_timestamp.time_hours >= 0) && (local_gps_timestamp.date_day <= 23) &&
		(local_gps_timestamp.time_minutes >= 0) && (local_gps_timestamp.date_day <= 59) &&
		(local_gps_timestamp.time_seconds >= 0) && (local_gps_timestamp.date_day <= 59)) {
		gps_timestamp_valid = 1;
	}
	return gps_timestamp_valid;
}

/* GET CURRENT GPS POSITION VIA NMEA GGA MESSAGES.
 * @param gps_position:		Pointer to GPS position structure that will contain the data.
 * @param timeout_seconds:	Timeout in seconds.
 * @return return_code:		See NEOM8N_ReturnCode structure in neom8n.h.
 */
NEOM8N_ReturnCode NEOM8N_GetPosition(GPS_PositionData* gps_position, unsigned char timeout_seconds) {
	NEOM8N_ReturnCode return_code = NEOM8N_TIMEOUT;
	// Select GGA message to get complete position.
	NEOM8N_SelectNmeaMessages(NMEA_GGA_MASK);
	// Start DMA.
	DMA_LpuartRxStop();
	DMA_LpuartRxSetDestAddr((unsigned int) &(neom8n_ctx.nmea_rx_buf1), NMEA_RX_BUFFER_SIZE); // Start with buffer 1.
	neom8n_ctx.nmea_rx_fill_buf1 = 1;
	DMA_LpuartRxStart();
	LPUART_StartRx();
	// Save fix start time.
	unsigned int fix_start_time = TIM_TimeGetSeconds();
	// Loop until data is retrieved or timeout expired.
	while ((TIM_TimeGetSeconds() < fix_start_time+timeout_seconds) && (neom8n_ctx.nmea_gga_data_valid == 0)) {
		// Reload watchdog counter.
		IWDG_Reload();
		// Check LF flag to trigger parsing process.
		if (neom8n_ctx.nmea_rx_lf_flag == 1) {
			// Decode incoming NMEA message.
			if (neom8n_ctx.nmea_rx_fill_buf1 == 1) {
				NEOM8N_ParseNmeaGgaMessage(neom8n_ctx.nmea_rx_buf2, gps_position); // Buffer 1 is currently filled by DMA, buffer 2 is available for parsing.
			}
			else {
				NEOM8N_ParseNmeaGgaMessage(neom8n_ctx.nmea_rx_buf1, gps_position); // Buffer 2 is currently filled by DMA, buffer 1 is available for parsing.
			}
			if (neom8n_ctx.nmea_gga_parsing_success == 1) {
				return_code = NEOM8N_INVALID_DATA;
				// Check data.
				if (NEOM8N_PositionIsValid(*gps_position) == 1) {
					neom8n_ctx.nmea_gga_data_valid = 1;
					return_code = NEOM8N_SUCCESS;
				}
				else {
					neom8n_ctx.nmea_gga_parsing_success = 0;
				}
			}
			// Wait for next message.
			neom8n_ctx.nmea_rx_lf_flag = 0;
		}
	}
	// Stop DMA.
	LPUART_StopRx();
	DMA_LpuartRxStop();
	// Reset GPS timestamp data in case of failure.
	if (return_code != NEOM8N_SUCCESS) {
		(*gps_position).lat_degrees = 0;
		(*gps_position).lat_minutes = 0;
		(*gps_position).lat_seconds = 0;
		(*gps_position).lat_north = 0;
		(*gps_position).long_degrees = 0;
		(*gps_position).long_minutes = 0;
		(*gps_position).long_seconds = 0;
		(*gps_position).long_east = 0;
		(*gps_position).altitude = 0;
	}
	return return_code;
}

/* SWITCH DMA DESTINATION BUFFER (CALLED BY LPUART CM INTERRUPT).
 * @param:	None.
 * @return:	None.
 */
void NEOM8N_SwitchDmaBuffer(void) {

	/* Stop and start DMA transfer to switch buffer */
	DMA_LpuartRxStop();

	/* Switch buffer */
	if (neom8n_ctx.nmea_rx_fill_buf1 == 1) {
		DMA_LpuartRxSetDestAddr((unsigned int) &(neom8n_ctx.nmea_rx_buf2), NMEA_RX_BUFFER_SIZE); // Switch to buffer 2.
		neom8n_ctx.nmea_rx_fill_buf1 = 0;
	}
	else {
		DMA_LpuartRxSetDestAddr((unsigned int) &(neom8n_ctx.nmea_rx_buf1), NMEA_RX_BUFFER_SIZE); // Switch to buffer 1.
		neom8n_ctx.nmea_rx_fill_buf1 = 1;
	}

	/* Restart DMA transfer */
	DMA_LpuartRxStart();

	/* Set LF flag to start decoding current buffer */
	neom8n_ctx.nmea_rx_lf_flag = 1;
}