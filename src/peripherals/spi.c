/*
 * spi.c
 *
 *  Created on: 19 june 2018
 *      Author: Ludo
 */

#include "spi.h"

#include "gpio.h"
#include "lptim.h"
#include "mapping.h"
#include "rcc_reg.h"
#include "spi_reg.h"

/*** SPI local macros ***/

#define SPI_ACCESS_TIMEOUT_COUNT	1000000

/*** SPI functions ***/

/* CONFIGURE SPI1.
 * @param:	None.
 * @return:	None.
 */
void SPI1_Init(void) {
	// Enable peripheral clock.
	RCC -> APB2ENR |= (0b1 << 12); // SPI1EN='1'.
	// Configure power enable pins.
	GPIO_Configure(&GPIO_RF_POWER_ENABLE, GPIO_MODE_OUTPUT, GPIO_TYPE_PUSH_PULL, GPIO_SPEED_LOW, GPIO_PULL_NONE);
	GPIO_Write(&GPIO_RF_POWER_ENABLE, 0);
#ifdef HW1_0
	GPIO_Configure(&GPIO_SENSORS_POWER_ENABLE, GPIO_MODE_OUTPUT, GPIO_TYPE_PUSH_PULL, GPIO_SPEED_LOW, GPIO_PULL_NONE);
	GPIO_Write(&GPIO_SENSORS_POWER_ENABLE, 0);
#endif
	// Configure NSS, SCK, MISO and MOSI (first as high impedance).
	GPIO_Configure(&GPIO_SPI1_SCK, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
	GPIO_Configure(&GPIO_SPI1_MOSI, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
	GPIO_Configure(&GPIO_SPI1_MISO, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
	GPIO_Configure(&GPIO_SX1232_CS, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
#ifdef HW1_0
	GPIO_Configure(&GPIO_MAX11136_CS, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
#endif
	// Configure peripheral.
	SPI1 -> CR1 &= 0xFFFF0000; // Disable peripheral before configuration (SPE='0').
	SPI1 -> CR1 |= (0b1 << 2); // Master mode (MSTR='1').
	SPI1 -> CR1 |= (0b001 << 3); // Baud rate = PCLK2/4 = SYSCLK/4 = 4MHz.
	SPI1 -> CR1 &= ~(0b1 << 11); // 8-bits format (DFF='0') by default.
#ifdef HW2_0
	SPI1 -> CR1 &= ~(0b11 << 0); // CPOL='0' and CPHA='0'.
#endif
	SPI1 -> CR2 &= 0xFFFFFF08;
	SPI1 -> CR2 |= (0b1 << 2); // Enable output (SSOE='1').
	// Enable peripheral.
	SPI1 -> CR1 |= (0b1 << 6); // SPE='1'.
}

#ifdef HW1_0
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
#endif

/* ENABLE SPI1 PERIPHERAL.
 * @param:	None.
 * @return:	None.
 */
void SPI1_Enable(void) {
	// Enable SPI1 peripheral.
	RCC -> APB2ENR |= (0b1 << 12); // SPI1EN='1'.
	SPI1 -> CR1 |= (0b1 << 6);
	// Configure power enable pins.
	GPIO_Configure(&GPIO_RF_POWER_ENABLE, GPIO_MODE_OUTPUT, GPIO_TYPE_PUSH_PULL, GPIO_SPEED_LOW, GPIO_PULL_NONE);
	GPIO_Write(&GPIO_RF_POWER_ENABLE, 0);
#ifdef HW1_0
	GPIO_Configure(&GPIO_SENSORS_POWER_ENABLE, GPIO_MODE_OUTPUT, GPIO_TYPE_PUSH_PULL, GPIO_SPEED_LOW, GPIO_PULL_NONE);
	GPIO_Write(&GPIO_SENSORS_POWER_ENABLE, 0);
#endif
}

/* DISABLE SPI1 PERIPHERAL.
 * @param:	None.
 * @return:	None.
 */
void SPI1_Disable(void) {
	// Disable power control pin.
	GPIO_Configure(&GPIO_RF_POWER_ENABLE, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_NONE);
#ifdef HW1_0
	GPIO_Configure(&GPIO_SENSORS_POWER_ENABLE, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_NONE);
#endif
	// Disable SPI1 peripheral.
	SPI1 -> CR1 &= ~(0b1 << 6);
	// Clear all flags.
	SPI1 -> SR &= 0xFFFFFEEF;
	// Disable peripheral clock.
	RCC -> APB2ENR &= ~(0b1 << 12); // SPI1EN='0'.
}

/* SWITCH ALL SPI1 SLAVES ON.
 * @param:	None.
 * @return:	None.
 */
void SPI1_PowerOn(void) {
	// Turn SPI1 slaves on.
	GPIO_Write(&GPIO_RF_POWER_ENABLE, 1);
	LPTIM1_DelayMilliseconds(50, 1);
#ifdef HW1_0
	GPIO_Write(&GPIO_SENSORS_POWER_ENABLE, 1);
	LPTIM1_DelayMilliseconds(50, 1);
#endif
	// Enable GPIOs.
	GPIO_Configure(&GPIO_SPI1_SCK, GPIO_MODE_ALTERNATE_FUNCTION, GPIO_TYPE_PUSH_PULL, GPIO_SPEED_HIGH, GPIO_PULL_NONE);
	GPIO_Configure(&GPIO_SPI1_MOSI, GPIO_MODE_ALTERNATE_FUNCTION, GPIO_TYPE_PUSH_PULL, GPIO_SPEED_HIGH, GPIO_PULL_NONE);
	GPIO_Configure(&GPIO_SPI1_MISO, GPIO_MODE_ALTERNATE_FUNCTION, GPIO_TYPE_PUSH_PULL, GPIO_SPEED_HIGH, GPIO_PULL_NONE);
	GPIO_Write(&GPIO_SX1232_CS, 1); // CS high (idle state).
	GPIO_Configure(&GPIO_SX1232_CS, GPIO_MODE_OUTPUT, GPIO_TYPE_PUSH_PULL, GPIO_SPEED_HIGH, GPIO_PULL_NONE);
#ifdef HW1_0
	GPIO_Write(&GPIO_MAX11136_CS, 1); // CS high (idle state).
	GPIO_Configure(&GPIO_MAX11136_CS, GPIO_MODE_OUTPUT, GPIO_TYPE_PUSH_PULL, GPIO_SPEED_HIGH, GPIO_PULL_NONE);
	// Add pull-up to EOC.
	GPIO_Configure(&GPIO_MAX11136_EOC, GPIO_MODE_INPUT, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_UP);
#endif
	// Wait for power-on.
	LPTIM1_DelayMilliseconds(50, 1);
}

/* SWITCH ALL SPI1 SLAVES OFF.
 * @param:	None.
 * @return:	None.
 */
void SPI1_PowerOff(void) {
	// Turn SPI1 slaves off.
	GPIO_Write(&GPIO_RF_POWER_ENABLE, 0);
	GPIO_Write(&GPIO_SX1232_CS, 0); // CS low (to avoid powering slaves via SPI bus).
#ifdef HW1_0
	GPIO_Write(&GPIO_SENSORS_POWER_ENABLE, 0);
	GPIO_Write(&GPIO_MAX11136_CS, 0); // CS low (to avoid powering slaves via SPI bus).
#endif
	// Disable SPI alternate function.
	GPIO_Configure(&GPIO_SPI1_SCK, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
	GPIO_Configure(&GPIO_SPI1_MOSI, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
	GPIO_Configure(&GPIO_SPI1_MISO, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
	GPIO_Configure(&GPIO_SX1232_CS, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
#ifdef HW1_0
	GPIO_Configure(&GPIO_MAX11136_CS, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
	// Remove pull-up to EOC.
	GPIO_Configure(&GPIO_MAX11136_EOC, GPIO_MODE_INPUT, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_NONE);
#endif
	// Wait for power-off.
	LPTIM1_DelayMilliseconds(100, 1);
}

/* SEND A BYTE THROUGH SPI1.
 * @param tx_data:	Data to send (8-bits).
 * @return:			1 in case of success, 0 in case of failure.
 */
unsigned char SPI1_WriteByte(unsigned char tx_data) {
#ifdef HW1_0
	// Set data length to 8-bits.
	SPI1 -> CR1 &= ~(0b1 << 11); // DFF='0'.
#endif
	// Wait for TXE flag.
	unsigned int loop_count = 0;
	while (((SPI1 -> SR) & (0b1 << 1)) == 0) {
		// Wait for TXE='1' or timeout.
		loop_count++;
		if (loop_count > SPI_ACCESS_TIMEOUT_COUNT) return 0;
	}
	// Send data.
	*((volatile unsigned char*) &(SPI1 -> DR)) = tx_data;
	return 1;
}

/* READ A BYTE FROM SPI1.
 * @param rx_data:	Pointer to byte that will contain the data to read (8-bits).
 * @return:			1 in case of success, 0 in case of failure.
 */
unsigned char SPI1_ReadByte(unsigned char tx_data, unsigned char* rx_data) {
#ifdef HW1_0
	// Set data length to 8-bits.
	SPI1 -> CR1 &= ~(0b1 << 11); // DFF='0'.
#endif
	// Dummy read to DR to clear RXNE flag.
	(*rx_data) = *((volatile unsigned char*) &(SPI1 -> DR));
	// Wait for TXE flag.
	unsigned int loop_count = 0;
	while (((SPI1 -> SR) & (0b1 << 1)) == 0) {
		// Wait for TXE='1' or timeout.
		loop_count++;
		if (loop_count > SPI_ACCESS_TIMEOUT_COUNT) return 0;
	}
	// Send dummy data on MOSI to generate clock.
	*((volatile unsigned char*) &(SPI1 -> DR)) = tx_data;
	// Wait for incoming data.
	loop_count = 0;
	while (((SPI1 -> SR) & (0b1 << 0)) == 0) {
		// Wait for RXNE='1' or timeout.
		loop_count++;
		if (loop_count > SPI_ACCESS_TIMEOUT_COUNT) return 0;
	}
	(*rx_data) = *((volatile unsigned char*) &(SPI1 -> DR));
	return 1;
}

#ifdef HW1_0
/* SEND A SHORT THROUGH SPI1.
 * @param tx_data:	Data to send (16-bits).
 * @return:			1 in case of success, 0 in case of failure.
 */
unsigned char SPI1_WriteShort(unsigned short tx_data) {
	// Set data length to 16-bits.
	SPI1 -> CR1 |= (0b1 << 11); // DFF='1'.
	// Wait for TXE flag.
	unsigned int loop_count = 0;
	while (((SPI1 -> SR) & (0b1 << 1)) == 0) {
		// Wait for TXE='1' or timeout.
		loop_count++;
		if (loop_count > SPI_ACCESS_TIMEOUT_COUNT) return 0;
	}
	// Send data.
	*((volatile unsigned short*) &(SPI1 -> DR)) = tx_data;
	return 1;
}

/* READ A SHORT FROM SPI1.
 * @param rx_data:	Pointer to short that will contain the data to read (16-bits).
 * @return:			1 in case of success, 0 in case of failure.
 */
unsigned char SPI1_ReadShort(unsigned short tx_data, unsigned short* rx_data) {
	// Set data length to 16-bits.
	SPI1 -> CR1 |= (0b1 << 11); // DFF='1'.
	// Dummy read to DR to clear RXNE flag.
	(*rx_data) = *((volatile unsigned short*) &(SPI1 -> DR));
	// Wait for TXE flag.
	unsigned int loop_count = 0;
	while (((SPI1 -> SR) & (0b1 << 1)) == 0) {
		// Wait for TXE='1' or timeout.
		loop_count++;
		if (loop_count > SPI_ACCESS_TIMEOUT_COUNT) return 0;
	}
	// Send dummy data on MOSI to generate clock.
	*((volatile unsigned short*) &(SPI1 -> DR)) = tx_data;
	// Wait for incoming data.
	loop_count = 0;
	while (((SPI1 -> SR) & (0b1 << 0)) == 0) {
		// Wait for RXNE='1' or timeout.
		loop_count++;
		if (loop_count > SPI_ACCESS_TIMEOUT_COUNT) return 0;
	}
	(*rx_data) = *((volatile unsigned short*) &(SPI1 -> DR));
	return 1;
}
#endif

#ifdef HW2_0
/* CONFIGURE SPI2.
 * @param:	None.
 * @return:	None.
 */
void SPI2_Init(void) {
	// Enable peripheral clock.
	RCC -> APB1ENR |= (0b1 << 14); // SPI2EN='1'.
	// Configure power enable pins.
	GPIO_Configure(&GPIO_ADC_POWER_ENABLE, GPIO_MODE_OUTPUT, GPIO_TYPE_PUSH_PULL, GPIO_SPEED_LOW, GPIO_PULL_NONE);
	GPIO_Write(&GPIO_ADC_POWER_ENABLE, 0);
	// Configure NSS, SCK, MISO and MOSI (first as high impedance).
	GPIO_Configure(&GPIO_SPI2_SCK, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
	GPIO_Configure(&GPIO_SPI2_MOSI, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
	GPIO_Configure(&GPIO_SPI2_MISO, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
	GPIO_Configure(&GPIO_MAX11136_CS, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
	// Configure peripheral.
	SPI2 -> CR1 &= 0xFFFF0000; // Disable peripheral before configuration (SPE='0').
	SPI2 -> CR1 |= (0b1 << 2); // Master mode (MSTR='1').
	SPI2 -> CR1 |= (0b001 << 3); // Baud rate = PCLK2/4 = SYSCLK/4 = 4MHz.
	SPI2 -> CR1 |= (0b1 << 11); // 16-bits format (DFF='1').
	SPI2 -> CR1 |= (0b11 << 0); // CPOL='1' and CPHA='1'.
	SPI2 -> CR2 &= 0xFFFFFF08;
	SPI2 -> CR2 |= (0b1 << 2); // Enable output (SSOE='1').
	// Enable peripheral.
	SPI2 -> CR1 |= (0b1 << 6); // SPE='1'.
}

/* ENABLE SPI2 PERIPHERAL.
 * @param:	None.
 * @return:	None.
 */
void SPI2_Enable(void) {
	// Enable SPI2 peripheral.
	RCC -> APB1ENR |= (0b1 << 14); // SPI2EN='1'.
	SPI2 -> CR1 |= (0b1 << 6);
	// Configure power enable pins.
	GPIO_Configure(&GPIO_ADC_POWER_ENABLE, GPIO_MODE_OUTPUT, GPIO_TYPE_PUSH_PULL, GPIO_SPEED_LOW, GPIO_PULL_NONE);
	GPIO_Write(&GPIO_ADC_POWER_ENABLE, 0);
}

/* DISABLE SPI2 PERIPHERAL.
 * @param:	None.
 * @return:	None.
 */
void SPI2_Disable(void) {
	// Disable power control pin.
	GPIO_Configure(&GPIO_ADC_POWER_ENABLE, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_NONE);
	// Disable SPI2 peripheral.
	SPI2 -> CR1 &= ~(0b1 << 6);
	// Clear all flags.
	SPI2 -> SR &= 0xFFFFFEEF;
	// Disable peripheral clock.
	RCC -> APB1ENR &= ~(0b1 << 14); // SPI2EN='0'.
}

/* SWITCH ALL SPI2 SLAVES ON.
 * @param:	None.
 * @return:	None.
 */
void SPI2_PowerOn(void) {
	// Turn MAX11136 on.
	GPIO_Write(&GPIO_ADC_POWER_ENABLE, 1);
	// Wait for power-on.
	LPTIM1_DelayMilliseconds(50, 1);
	// Enable GPIOs.
	GPIO_Configure(&GPIO_SPI2_SCK, GPIO_MODE_ALTERNATE_FUNCTION, GPIO_TYPE_PUSH_PULL, GPIO_SPEED_LOW, GPIO_PULL_NONE);
	GPIO_Configure(&GPIO_SPI2_MOSI, GPIO_MODE_ALTERNATE_FUNCTION, GPIO_TYPE_PUSH_PULL, GPIO_SPEED_LOW, GPIO_PULL_NONE);
	GPIO_Configure(&GPIO_SPI2_MISO, GPIO_MODE_ALTERNATE_FUNCTION, GPIO_TYPE_PUSH_PULL, GPIO_SPEED_LOW, GPIO_PULL_NONE);
	GPIO_Write(&GPIO_MAX11136_CS, 1); // CS high (idle state).
	GPIO_Configure(&GPIO_MAX11136_CS, GPIO_MODE_OUTPUT, GPIO_TYPE_PUSH_PULL, GPIO_SPEED_HIGH, GPIO_PULL_NONE);
	// Wait for power-on.
	LPTIM1_DelayMilliseconds(100, 1);
}

/* SWITCH ALL SPI2 SLAVES OFF.
 * @param:	None.
 * @return:	None.
 */
void SPI2_PowerOff(void) {
	// Turn MAX11136 off.
	GPIO_Write(&GPIO_ADC_POWER_ENABLE, 0);
	GPIO_Write(&GPIO_MAX11136_CS, 0); // CS low (to avoid powering slaves via SPI bus).
	// Disable SPI alternate function.
	GPIO_Configure(&GPIO_SPI2_SCK, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
	GPIO_Configure(&GPIO_SPI2_MOSI, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
	GPIO_Configure(&GPIO_SPI2_MISO, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
	GPIO_Configure(&GPIO_MAX11136_CS, GPIO_MODE_ANALOG, GPIO_TYPE_OPEN_DRAIN, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
	// Wait for power-off.
	LPTIM1_DelayMilliseconds(100, 1);
}

/* SEND A SHORT THROUGH SPI2.
 * @param tx_data:	Data to send (16-bits).
 * @return:			1 in case of success, 0 in case of failure.
 */
unsigned char SPI2_WriteShort(unsigned short tx_data) {
	// Wait for TXE flag.
	unsigned int loop_count = 0;
	while (((SPI2 -> SR) & (0b1 << 1)) == 0) {
		// Wait for TXE='1' or timeout.
		loop_count++;
		if (loop_count > SPI_ACCESS_TIMEOUT_COUNT) return 0;
	}
	// Send data.
	*((volatile unsigned short*) &(SPI2 -> DR)) = tx_data;
	return 1;
}

/* READ A SHORT FROM SPI2.
 * @param rx_data:	Pointer to short that will contain the data to read (16-bits).
 * @return:			1 in case of success, 0 in case of failure.
 */
unsigned char SPI2_ReadShort(unsigned short tx_data, unsigned short* rx_data) {
	// Dummy read to DR to clear RXNE flag.
	(*rx_data) = *((volatile unsigned short*) &(SPI2 -> DR));
	// Wait for TXE flag.
	unsigned int loop_count = 0;
	while (((SPI2 -> SR) & (0b1 << 1)) == 0) {
		// Wait for TXE='1' or timeout.
		loop_count++;
		if (loop_count > SPI_ACCESS_TIMEOUT_COUNT) return 0;
	}
	// Send dummy data on MOSI to generate clock.
	*((volatile unsigned short*) &(SPI2 -> DR)) = tx_data;
	// Wait for incoming data.
	loop_count = 0;
	while (((SPI2 -> SR) & (0b1 << 0)) == 0) {
		// Wait for RXNE='1' or timeout.
		loop_count++;
		if (loop_count > SPI_ACCESS_TIMEOUT_COUNT) return 0;
	}
	(*rx_data) = *((volatile unsigned short*) &(SPI2 -> DR));
	return 1;
}
#endif
