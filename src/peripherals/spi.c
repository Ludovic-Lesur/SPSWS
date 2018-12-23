/*
 * spi.c
 *
 *  Created on: 19 june 2018
 *      Author: Ludo
 */

#include "spi.h"

#include "gpio.h"
#include "mapping.h"
#include "rcc_reg.h"
#include "spi_reg.h"
#include "tim.h"

/*** SPI functions ***/

/* CONFIGURE SPI1.
 * @param:	None.
 * @return:	None.
 */
void SPI1_Init(void) {

	/* Enable peripheral clock */
	RCC -> APB2ENR |= (0b1 << 12); // SPI1EN='1'.

	/* Configure power enable pins */
	GPIO_Configure(GPIO_RF_POWER_ENABLE, Output, PushPull, LowSpeed, NoPullUpNoPullDown);
	GPIO_Write(GPIO_RF_POWER_ENABLE, 0);
	GPIO_Configure(GPIO_SENSORS_POWER_ENABLE, Output, PushPull, LowSpeed, NoPullUpNoPullDown);
	GPIO_Write(GPIO_SENSORS_POWER_ENABLE, 0);

	/* Configure SCK, MISO and MOSI (first as high impedance) */
	GPIO_Configure(GPIO_SPI1_SCK, Input, PushPull, LowSpeed, NoPullUpNoPullDown);
	GPIO_Configure(GPIO_SPI1_MOSI, Input, PushPull, LowSpeed, NoPullUpNoPullDown);
	GPIO_Configure(GPIO_SPI1_MISO, Input, PushPull, LowSpeed, NoPullUpNoPullDown);

	/* Configure CS pins (first as output low) */
	GPIO_Configure(GPIO_SX1232_CS, Output, PushPull, LowSpeed, NoPullUpNoPullDown);
	GPIO_Write(GPIO_SX1232_CS, 0);
	GPIO_Configure(GPIO_MAX11136_CS, Output, PushPull, LowSpeed, NoPullUpNoPullDown);
	GPIO_Write(GPIO_MAX11136_CS, 0);
#ifdef IM_HWT
	GPIO_Configure(GPIO_MAX5495_CS, Output, PushPull, LowSpeed, NoPullUpNoPullDown);
	GPIO_Write(GPIO_MAX5495_CS, 0);
#endif

	/* Configure peripheral */
	SPI1 -> CR1 = 0; // Disable peripheral before configuration (SPE='0').
	SPI1 -> CR1 |= (0b1 << 2); // Master mode (MSTR='1').
	SPI1 -> CR1 |= (0b011 << 3); // Baud rate = PCLK2/16 = SYSCLK/16 = 1MHz.
	SPI1 -> CR1 &= ~(0b1 << 11); // 8-bits format (DFF='0') by default.
	//SPI1 -> CR1 |= (0b1 << 9); // Software slave management enabled (SSM='1').
	SPI1 -> CR2 = 0;
	SPI1 -> CR2 |= (0b1 << 2); // Enable output (SSOE='1').

	/* Disable peripheral by default */
	RCC -> APB2ENR &= ~(0b1 << 12); // SPI1EN='0'.
}

/* ENABLE SPI1 PERIPHERAL.
 * @param:	None.
 * @return:	None.
 */
void SPI1_Enable(void) {

	/* Enable SPI1 peripheral */
	RCC -> APB2ENR |= (0b1 << 12); // SPI1EN='1'.
	SPI1 -> CR1 |= (0b1 << 6);
}

/* DISABLE SPI1 PERIPHERAL.
 * @param:	None.
 * @return:	None.
 */
void SPI1_Disable(void) {

	/* Disable SPI1 peripheral */
	SPI1 -> CR1 &= ~(0b1 << 6);
	RCC -> APB2ENR &= ~(0b1 << 12); // SPI1EN='0'.
}

/* SET SPI1 SCLK POLARITY.
 * @param polarity:	Clock polarity (0 = SCLK idle high, otherwise SCLK idle low).
 * @return:			None.
 */
void SPI1_SetClockPolarity(unsigned char polarity) {
	if (polarity == 0) {
		SPI1 -> CR1 &= ~(0b11 << 0); // CPOL='0' and CPHA='0'.
	}
	else {
		SPI1 -> CR1 |= (0b11 << 0); // CPOL='1' and CPHA='1'.
	}
}

/* SWITCH ALL SPI1 SLAVES ON.
 * @param:	None.
 * @return:	None.
 */
void SPI1_PowerOn(void) {

	/* Switch SX1232 on */
	GPIO_Write(GPIO_RF_POWER_ENABLE, 1);
	GPIO_Write(GPIO_SX1232_CS, 1); // CS high (idle state).
	TIM22_WaitMilliseconds(100);

	/* Switch MAX11136 on */
	GPIO_Write(GPIO_SENSORS_POWER_ENABLE, 1);
	GPIO_Write(GPIO_MAX11136_CS, 1); // CS high (idle state).
	TIM22_WaitMilliseconds(100);

#ifdef IM_HWT
	/* MAX5495 */
	GPIO_Write(GPIO_MAX5495_CS, 1); // CS high (idle state).
#endif

	/* Enable SPI alternate function */
	GPIO_Configure(GPIO_SPI1_SCK, AlternateFunction, PushPull, HighSpeed, NoPullUpNoPullDown);
	GPIO_Configure(GPIO_SPI1_MOSI, AlternateFunction, PushPull, HighSpeed, NoPullUpNoPullDown);
	GPIO_Configure(GPIO_SPI1_MISO, AlternateFunction, PushPull, HighSpeed, NoPullUpNoPullDown);
}

/* SWITCH ALL SPI1 SLAVES OFF.
 * @param:	None.
 * @return:	None.
 */
void SPI1_PowerOff(void) {

	/* Disable SPI alternate function */
	GPIO_Configure(GPIO_SPI1_SCK, Input, PushPull, LowSpeed, NoPullUpNoPullDown);
	GPIO_Configure(GPIO_SPI1_MOSI, Input, PushPull, LowSpeed, NoPullUpNoPullDown);
	GPIO_Configure(GPIO_SPI1_MISO, Input, PushPull, LowSpeed, NoPullUpNoPullDown);

	/* Switch SX1232 off */
	GPIO_Write(GPIO_RF_POWER_ENABLE, 0);
	GPIO_Write(GPIO_SX1232_CS, 0); // CS low (to avoid powering slaves via SPI bus).

	/* Switch MAX11136 off */
	GPIO_Write(GPIO_SENSORS_POWER_ENABLE, 0);
	GPIO_Write(GPIO_MAX11136_CS, 0); // CS low (to avoid powering slaves via SPI bus).

#ifdef IM_HWT
	/* MAX5495 */
	GPIO_Write(GPIO_MAX5495_CS, 0; // CS low (to avoid powering slaves via SPI bus).
#endif
}

/* SEND A BYTE THROUGH SPI1.
 * @param tx_data:	Data to send (8-bits).
 * @return:			None.
 */
void SPI1_WriteByte(unsigned char tx_data) {
	// Set data length to 8-bits.
	SPI1 -> CR1 &= ~(0b1 << 11); // DFF='0'.
	// Send data.
	*((volatile unsigned char*) &(SPI1 -> DR)) = tx_data;
	// Wait for transmission to complete.
	while ((((SPI1 -> SR) & (0b1 << 1)) == 0) || (((SPI1 -> SR) & (0b1 << 7)) != 0)); // Wait for TXE='1' and BSY='0'.
}

/* SEND A SHORT THROUGH SPI1.
 * @param tx_data:	Data to send (16-bits).
 * @return:			None.
 */
void SPI1_WriteShort(unsigned short tx_data) {
	// Set data length to 16-bits.
	SPI1 -> CR1 |= (0b1 << 11); // DFF='1'.
	// Send data.
	*((volatile unsigned short*) &(SPI1 -> DR)) = tx_data;
	// Wait for transmission to complete.
	while ((((SPI1 -> SR) & (0b1 << 1)) == 0) || (((SPI1 -> SR) & (0b1 << 7)) != 0)); // Wait for TXE='1' and BSY='0'.
}

/* READ A BYTE FROM SPI1.
 * @param rx_data:	Pointer to byte that will contain the data to read (8-bits).
 * @return:			None.
 */
void SPI1_ReadByte(unsigned char tx_data, unsigned char* rx_data) {
	// Set data length to 8-bits.
	SPI1 -> CR1 &= ~(0b1 << 11); // DFF='0'.
	// Dummy read to DR to clear RXNE flag.
	(*rx_data) = *((volatile unsigned char*) &(SPI1 -> DR));
	// Send dummy data on MOSI to generate clock.
	*((volatile unsigned char*) &(SPI1 -> DR)) = tx_data;
	// Wait for incoming data.
	while (((SPI1 -> SR) & (0b1 << 0)) == 0); // Wait for RXNE='1'.
	(*rx_data) = *((volatile unsigned char*) &(SPI1 -> DR));
	// Wait for reception to complete.
	while ((((SPI1 -> SR) & (0b1 << 0)) != 0) || (((SPI1 -> SR) & (0b1 << 7)) != 0)); // Wait for RXNE='0' and BSY='0'.
}

/* READ A SHORT FROM SPI1.
 * @param rx_data:	Pointer to short that will contain the data to read (16-bits).
 * @return:			None.
 */
void SPI1_ReadShort(unsigned short tx_data, unsigned short* rx_data) {
	// Set data length to 16-bits.
	SPI1 -> CR1 |= (0b1 << 11); // DFF='1'.
	// Dummy read to DR to clear RXNE flag.
	(*rx_data) = *((volatile unsigned char*) &(SPI1 -> DR));
	// Send dummy data on MOSI to generate clock.
	*((volatile unsigned short*) &(SPI1 -> DR)) = tx_data;
	// Wait for incoming data.
	while (((SPI1 -> SR) & (0b1 << 0)) == 0); // Wait for RXNE='1'.
	(*rx_data) = *((volatile unsigned short*) &(SPI1 -> DR));
	// Wait for reception to complete.
	while ((((SPI1 -> SR) & (0b1 << 0)) != 0) || (((SPI1 -> SR) & (0b1 << 7)) != 0)); // Wait for RXNE='0' and BSY='0'.
}
