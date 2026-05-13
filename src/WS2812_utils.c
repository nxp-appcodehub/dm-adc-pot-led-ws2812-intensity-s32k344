/*==================================================================================================
 * Project : RTD AUTOSAR 4.9
 * Platform : CORTEXM
 * Peripheral : S32K3XX
 * Dependencies : none
 *
 * Autosar Version : 4.9.0
 * Autosar Revision : ASR_REL_4_9_REV_0000
 * Autosar Conf.Variant :
 * SW Version : 7.0.0
 * Build Version : S32K3_RTD_7_0_0_QLP03_D2512_ASR_REL_4_9_REV_0000_20251210
 *
 * Copyright 2020 - 2026 NXP
 *
 *   NXP Proprietary. This software is owned or controlled by NXP and may only be
 *   used strictly in accordance with the applicable license terms. By expressly
 *   accepting such terms or by downloading, installing, activating and/or otherwise
 *   using the software, you are agreeing that you have read, and that you agree to
 *   comply with and are bound by, such license terms. If you do not agree to be
 *   bound by the applicable license terms, then you may not retain, install,
 *   activate or otherwise use the software.
 ==================================================================================================*/

#include "WS2812_utils.h"

#include "Flexio_Mcl_Ip.h"
#include "S32K344_FLEXIO.h"
#include "OsIf.h"

/**
 * @brief Expands an 8-bit color value into a 32-bit FlexIO shifter word
 *
 * Converts a single byte (0-255) into WS2812 timing patterns.
 * Each bit becomes a 4-bit pattern: '1' = 0011 (high-high-low-low), '0' = 0001 (high-low-low-low)
 *
 * @param color - 8-bit color value (0-255)
 * @return 32-bit word containing WS2812-compatible bit patterns
 */
static uint32_t WS2812_ExpandColorByte(uint8_t color) {
	uint32_t expandedWord = 0;

	// FlexIO shifts bit 0 (LSB) out to the physical wire first.
	for (int i = 0; i < 8; i++) {
		// Extract the MSB of the color byte first (WS2812 expects MSB first)
		bool isOne = (color >> (7 - i)) & 0x01;

		// WS2812 "1": High, High, Low, Low -> LSB first = 0011 (0x3)
		// WS2812 "0": High, Low, Low, Low  -> LSB first = 0001 (0x1)
		uint32_t pattern = isOne ? 0x3 : 0x1;

		// Pack the 4-bit pattern into the 32-bit word
		expandedWord |= (pattern << (i * 4));
	}
	return expandedWord;
}

/**
 * @brief Initializes the WS2812 LED strip hardware using FlexIO
 *
 * Configures FlexIO shifter and timer for WS2812 communication at 3.2 MHz baud rate.
 * Sets up hardware routing and clears the transmit buffer.
 *
 * @param strip - Pointer to WS2812_Strip_t configuration structure
 * @param pin - FlexIO pin number for data output
 * @param shifter - FlexIO shifter ID to use (0-7)
 * @param timer - FlexIO timer ID to use (0-7)
 */
void WS2812_Init(WS2812_Strip_t *strip, uint8_t pin, uint8_t shifter,
		uint8_t timer) {
	strip->fxioPin = pin;
	strip->shifterId = shifter;
	strip->timerId = timer;
	strip->brightness = 50;

	// Clear the buffer to zeroes initially
	for (uint32_t i = 0; i < WS2812_BUFFER_WORDS; i++) {
		strip->txBuffer[i] = 0;
	}

	// 1. Shifter Config: Transmit Mode, Linked to Timer
	IP_FLEXIO->SHIFTCTL[strip->shifterId] =
	FLEXIO_SHIFTCTL_TIMSEL(strip->timerId) |   // Link to our Timer
			FLEXIO_SHIFTCTL_TIMPOL(0) |               // Shift on positive edge
			FLEXIO_SHIFTCTL_PINCFG(3) |        // Shifter drives the pin output
			FLEXIO_SHIFTCTL_PINSEL(strip->fxioPin) | // The physical FlexIO pin
			FLEXIO_SHIFTCTL_SMOD(2);                   // Mode 2: Transmit

	// 2. Timer Config: 8-bit Baud Mode
	IP_FLEXIO->TIMCFG[strip->timerId] =
	FLEXIO_TIMCFG_TIMOUT(0) |
	FLEXIO_TIMCFG_TIMDEC(0) |
	FLEXIO_TIMCFG_TIMRST(0) |
	FLEXIO_TIMCFG_TIMDIS(2) |                  // Disable on timer compare
			FLEXIO_TIMCFG_TIMENA(2) |// Enable on trigger high
			FLEXIO_TIMCFG_TSTOP(0) |// Stop bit disabled
			FLEXIO_TIMCFG_TSTART(0);// Start bit enabled

	IP_FLEXIO->TIMCTL[strip->timerId] =
	FLEXIO_TIMCTL_TRGSEL((strip->shifterId << 2) | 1) | // Triggered by Shifter Flag
			FLEXIO_TIMCTL_TRGPOL(1) |// Trigger active low
			FLEXIO_TIMCTL_TRGSRC(1) |// Internal trigger
			FLEXIO_TIMCTL_PINCFG(0) |// Timer pin output disabled
			FLEXIO_TIMCTL_PINSEL(0) |
			FLEXIO_TIMCTL_PINPOL(0) |
			FLEXIO_TIMCTL_TIMOD(1);// Mode 1: 8-bit baud mode

	// 3. Set the 3.2 MHz Baud Rate
	// Formula: Divider = (FlexIO_Clock / (Target_Baud * 2)) - 1
	// FlexIO Clock: 160 MHz -> (160 / (3.2 * 2)) - 1 = 24
	uint32_t cmpDivider = 24;
	uint32_t bitsPerFrame = 63;              // Send exactly 64 bits per trigger

	IP_FLEXIO->TIMCMP[strip->timerId] = (bitsPerFrame << 8) | cmpDivider;
}

/**
 * @brief Sets the color of a specific LED in the strip
 *
 * Applies brightness scaling and converts RGB values to WS2812 format (GRB order).
 * Data is stored in the transmit buffer but not sent until WS2812_Show() is called.
 *
 * @param strip - Pointer to WS2812_Strip_t structure
 * @param index - LED index (0 to NUM_LEDS-1)
 * @param r - Red value (0-255)
 * @param g - Green value (0-255)
 * @param b - Blue value (0-255)
 */
void WS2812_SetPixel(WS2812_Strip_t *strip, uint16_t index, uint8_t r,
		uint8_t g, uint8_t b) {
	if (index >= NUM_LEDS)
		return;

	// Apply brightness scaling (0-255 range)
	r = (r * strip->brightness) / 255;
	g = (g * strip->brightness) / 255;
	b = (b * strip->brightness) / 255;

	// WS2812 expects data in Green -> Red -> Blue order
	uint16_t bufferIndex = index * 3;
	strip->txBuffer[bufferIndex + 0] = WS2812_ExpandColorByte(g);
	strip->txBuffer[bufferIndex + 1] = WS2812_ExpandColorByte(r);
	strip->txBuffer[bufferIndex + 2] = WS2812_ExpandColorByte(b);
}

/**
 * @brief Clears all LEDs in the strip (sets all to black/off)
 *
 * Sets all LEDs to RGB(0,0,0). Changes take effect after WS2812_Show() is called.
 *
 * @param strip - Pointer to WS2812_Strip_t structure
 */
void WS2812_Clear(WS2812_Strip_t *strip) {
	for (uint16_t i = 0; i < NUM_LEDS; i++) {
		WS2812_SetPixel(strip, i, 0, 0, 0);
	}
}

/**
 * @brief Resets the FlexIO shifter and sends a reset pulse to the WS2812 strip
 *
 * Temporarily disables the shifter, clears status flags, re-enables it, and sends
 * an 80µs+ reset pulse required by WS2812 protocol to latch data.
 *
 * @param strip - Pointer to WS2812_Strip_t structure
 */
void WS2812_Reset(WS2812_Strip_t *strip) {
	// Disable the shifter temporarily
	IP_FLEXIO->SHIFTCTL[strip->shifterId] &= ~FLEXIO_SHIFTCTL_SMOD_MASK;

	// Clear status flags
	IP_FLEXIO->SHIFTSTAT = (1 << strip->shifterId);
	IP_FLEXIO->SHIFTERR = (1 << strip->shifterId);

	// Re-enable the shifter
	IP_FLEXIO->SHIFTCTL[strip->shifterId] |= FLEXIO_SHIFTCTL_SMOD(2);

	// Small delay for reset latch
	delay_cycles(20000); // >80us reset pulse
}

/**
 * @brief Transmits the buffered LED data to the WS2812 strip
 *
 * Sends color data to all LEDs via FlexIO. First LED data is sent twice to compensate
 * for 3.3V logic level issues. Includes reset pulses to latch data properly.
 * This function blocks until transmission is complete.
 *
 * @param strip - Pointer to WS2812_Strip_t structure
 */
void WS2812_Show(WS2812_Strip_t *strip) {
	// Reset before transmission
	WS2812_Reset(strip);

	// First transmission: Send ONLY the first LED's data (3 words: G, R, B)
	// This "primes" the first LED to properly receive the signal
	for (uint32_t i = 0; i < 3; i++) {  // Only first 3 words (1 LED)
		// Block until the Shifter Buffer is empty
		while (!(IP_FLEXIO->SHIFTSTAT & (1 << strip->shifterId))) {
		}
		// Push the first LED's data
		IP_FLEXIO->SHIFTBUF[strip->shifterId] = strip->txBuffer[i];
	}

	// Block until transmission completes
	while (!(IP_FLEXIO->SHIFTSTAT & (1 << strip->shifterId))) {
	}

	// Reset latch - latches first LED with initial data
	delay_cycles(15000);

	// Second transmission: Send ALL LED data (overwrites first LED + updates rest)
	WS2812_Reset(strip);

	for (uint32_t i = 0; i < WS2812_BUFFER_WORDS; i++) {  // All LEDs
		// Block until the Shifter Buffer is empty
		while (!(IP_FLEXIO->SHIFTSTAT & (1 << strip->shifterId))) {
		}
		// Push the pre-formatted 32-bit word
		IP_FLEXIO->SHIFTBUF[strip->shifterId] = strip->txBuffer[i];
	}

	// Block until the very last shift is totally finished
	while (!(IP_FLEXIO->SHIFTSTAT & (1 << strip->shifterId))) {
	}

	// Final Reset Latch
	delay_cycles(15000);
}

/**
 * @brief Blocking delay function using CPU cycles
 *
 * Creates a delay by executing NOP instructions in a loop. The volatile keyword
 * prevents compiler optimization. Actual time depends on CPU clock speed.
 *
 * @param cycles_to_wait - Number of loop iterations to execute
 */
void delay_cycles(uint32_t cycles_to_wait) {
	for (volatile uint32_t wait = 0; wait < cycles_to_wait; wait++) {
		__asm("nop");
		// No-operation instruction (takes 1 cycle)
	}
}
