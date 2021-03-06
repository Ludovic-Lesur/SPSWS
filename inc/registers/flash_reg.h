/*
 * nvm_reg.h
 *
 *  Created on: 19 june 2018
 *      Author: Ludo
 */

#ifndef FLASH_REG_H
#define FLASH_REG_H

/*** FLASH registers ***/

typedef struct {
	volatile unsigned int ACR;		// NVM interface access control register.
	volatile unsigned int PECR;		// NVM interface program and erase control register.
	volatile unsigned int PDKEYR;	// NVM interface power down key register.
	volatile unsigned int PEKEYR;	// NVM interface PECR unlock key register.
	volatile unsigned int PRGKEYR;	// NVM interface program and erase key register.
	volatile unsigned int OPTKEYR;	// NVM interface option bytes unlock key register.
	volatile unsigned int SR;		// NVM interface status register.
	volatile unsigned int OPTR;		// NVM interface option bytes register.
	volatile unsigned int WRPROT1;	// NVM interface write protection register 1.
	unsigned int RESERVED[23];		// Reserved 0x24.
	volatile unsigned int WRPROT2;	// NVM interface write protection register 2.
} FLASH_BaseAddress;

/*** FLASH registers base address ***/

#define FLASH	((FLASH_BaseAddress*) ((unsigned int) 0x40022000))

/*** EEPROM address range ***/

#define EEPROM_START_ADDRESS	(unsigned int) 0x08080000
#ifdef HW1_0
// EEPROM size is 1kB for STM32L041xxxx (category 2 device).
#define EEPROM_SIZE				1024 // In bytes.
#endif
#ifdef HW2_0
// EEPROM size is 6kB for STM32L081xxxx (category 5 device).
#define EEPROM_SIZE				6144 // In bytes.
#endif

#endif /* FLASH_REG_H */
