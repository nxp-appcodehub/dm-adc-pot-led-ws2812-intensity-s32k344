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

#ifndef WS2812_H
#define WS2812_H

#include <stdint.h>
#include <stdbool.h>

// Define the number of LEDs on your strip here
#define NUM_LEDS 16
#define WS2812_BUFFER_WORDS (NUM_LEDS * 3)

// The struct that holds the configuration and color buffer
typedef struct {
	uint32_t txBuffer[WS2812_BUFFER_WORDS];
	uint8_t fxioPin;
	uint8_t shifterId;
	uint8_t timerId;
	uint8_t brightness;  // Brightness: 0-255 (0 = off, 255 = max)
} WS2812_Strip_t;

// Function Prototypes
void WS2812_Init(WS2812_Strip_t *strip, uint8_t pin, uint8_t shifter,
		uint8_t timer);
void WS2812_SetPixel(WS2812_Strip_t *strip, uint16_t index, uint8_t r,
		uint8_t g, uint8_t b);
void WS2812_Clear(WS2812_Strip_t *strip);
void WS2812_Show(WS2812_Strip_t *strip);
void WS2812_Reset(WS2812_Strip_t *strip);
void delay_cycles(uint32_t wait);

#endif // WS2812_H
