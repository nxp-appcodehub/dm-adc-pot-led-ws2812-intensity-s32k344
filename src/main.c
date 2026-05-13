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

/*==================================================================================================
 *                                        INCLUDE FILES
 ==================================================================================================*/
#include "Clock_Ip.h"
#include "IntCtrl_Ip.h"
#include "Adc_Sar_Ip.h"
#include "Siul2_Port_Ip.h"
#include "OsIf.h"
#include <stdlib.h>
#include "Flexio_Mcl_Ip.h"
#include "WS2812_utils.h"
#include "Siul2_Dio_Ip.h"

/*==================================================================================================
 *                                      DEFINES AND MACROS
 ==================================================================================================*/
/* ADC Configuration */
#define USE_POT_CLICK           1U

/** @brief ADC channel selection based on potentiometer source */
#if (USE_POT_CLICK == 1U)
	#define ADC_SAR_USED_CH         0U      /*!< POT Click board channel */
#else
    #define ADC_SAR_USED_CH         7U      /*!< Onboard K344 potentiometer channel */
#endif
#define ADC_VREFH_mV                    3300U
#define ADC_RESOLUTION_BITS             12U

/* Filtering Configuration */
#define ADC_FILTER_ALPHA                0.15f    /* Exponential smoothing factor (0.0-1.0) */

/*==================================================================================================
 *                          LOCAL TYPEDEFS (STRUCTURES, UNIONS, ENUMS)
 *==================================================================================================*/
typedef struct {
    boolean btInc;
    boolean btDec;
} App_UsrControl_t;

/*==================================================================================================
 *                                       LOCAL MACROS
 *==================================================================================================*/

/*==================================================================================================
 *                                      LOCAL CONSTANTS
 *==================================================================================================*/

/*==================================================================================================
 *                                      LOCAL VARIABLES
 *==================================================================================================*/
App_UsrControl_t usrControl = {0};
uint8_t activeColorIndex = 0;           /* 0=Red, 1=Green, 2=Blue, 3=Yellow, 4=Cyan, 5=Magenta, 6=White, 7=Random */

/*==================================================================================================
 *                                      GLOBAL CONSTANTS
 *==================================================================================================*/

/*==================================================================================================
 *                                      GLOBAL VARIABLES
 *==================================================================================================*/
/* WS2812 LED Strip State */
WS2812_Strip_t ledStrip;

/* ADC Variables */
volatile boolean notif_triggered = FALSE;
volatile uint16 data;
uint16_t ui16AdcRawValue = 0u;
uint16_t ui16AdcMax = (1 << ADC_RESOLUTION_BITS);
float fAdcValue;
float fAdcValueFiltered = 0.0f;

/*==================================================================================================
 *                                   LOCAL FUNCTION PROTOTYPES
 *==================================================================================================*/

/*==================================================================================================
 *                                   EXTERNAL FUNCTION PROTOTYPES
 ==================================================================================================*/
extern void Adc_Sar_0_Isr(void);

/*==================================================================================================
 *                                       LOCAL FUNCTIONS
 ==================================================================================================*/

/**
 * @brief Board buttons to control the LED color with edge-detection debouncing
 * @details Detects button press transitions and cycles through color modes
 */
void BoardButtons(void)
{
    static boolean prevBtInc = FALSE;
    static boolean prevBtDec = FALSE;
    
    /* Detect rising edge (button press) for increment */
    if (usrControl.btInc && !prevBtInc)
    {
        activeColorIndex = (activeColorIndex + 1) % 8;
    }
    
    /* Detect rising edge (button press) for decrement */
    if (usrControl.btDec && !prevBtDec)
    {
        if (activeColorIndex == 0) {
            activeColorIndex = 7;
        } else {
            activeColorIndex--;
        }
    }
    
    /* Store previous button states */
    prevBtInc = usrControl.btInc;
    prevBtDec = usrControl.btDec;
}

/**
 * @brief Performs initialization and test sequence for WS2812 LED strip
 * @details Runs a visual test sequence:
 *          1. Lights up each LED sequentially in white
 *          2. Cycles all LEDs through RED, GREEN, and BLUE colors
 *          3. Clears all LEDs to prepare for normal operation
 * @param strip  Pointer to the WS2812 strip structure
 */
void WS2812_InitSequence(WS2812_Strip_t *strip)
{
	/* Step 1: Light up all LEDs one by one in WHITE */
	for (uint16_t i = 0; i < NUM_LEDS; i++) {
		WS2812_SetPixel(strip, i, 255, 255, 255);
		WS2812_Show(strip);
		delay_cycles(5000000);
	}

	delay_cycles(5000000);
	WS2812_Clear(strip);
	WS2812_Show(strip);

	/* Step 2: Cycle through RGB colors on all LEDs */
	/* RED */
	for (uint16_t i = 0; i < NUM_LEDS; i++) {
		WS2812_SetPixel(strip, i, 255, 0, 0);
	}
	WS2812_Show(strip);
	delay_cycles(10000000);

	/* GREEN */
	for (uint16_t i = 0; i < NUM_LEDS; i++) {
		WS2812_SetPixel(strip, i, 0, 255, 0);
	}
	WS2812_Show(strip);
	delay_cycles(10000000);

	/* BLUE */
	for (uint16_t i = 0; i < NUM_LEDS; i++) {
		WS2812_SetPixel(strip, i, 0, 0, 255);
	}
	WS2812_Show(strip);
	delay_cycles(10000000);

	/* Step 3: Clear all LEDs */
	WS2812_Clear(strip);
	WS2812_Show(strip);
	delay_cycles(5000000);
}

/*==================================================================================================
 *                                       GLOBAL FUNCTIONS
 ==================================================================================================*/
/**
 * @brief ADC End of Chain Notification Callback
 * @details Called when ADC conversion chain completes
 */
void AdcEndOfChainNotif(void)
{
	data = Adc_Sar_Ip_GetConvData(ADCHWUNIT_0_INSTANCE, ADC_SAR_USED_CH);
	notif_triggered = TRUE;
}

/*==================================================================================================
 *                                           MAIN FUNCTION
 ==================================================================================================*/
int main(void)
{
	/*==============================================================================================
	 *                                 SYSTEM INITIALIZATION
	 ==============================================================================================*/

	/* 1. Initialize System Clocks */
	Clock_Ip_Init(&Clock_Ip_aClockConfig[0]);

	/* 2. Initialize OS Interface (needed for timing functions) */
	OsIf_Init(NULL_PTR);

	/* 3. Initialize GPIO Pins */
	Siul2_Port_Ip_Init(
		NUM_OF_CONFIGURED_PINS_PortContainer_0_BOARD_InitPeripherals,
		g_pin_mux_InitConfigArr_PortContainer_0_BOARD_InitPeripherals);

	/*==============================================================================================
	 *                                 WS2812 LED STRIP INITIALIZATION
	 ==============================================================================================*/

	/* 4. Initialize FlexIO module */
	Flexio_Mcl_Ip_InitDevice(&Flexio_Ip_Sa_xFlexioInit);

	/* 5. Initialize WS2812 Hardware (Pin=8, Shifter=0, Timer=0) */
	WS2812_Init(&ledStrip, 8, 0, 0);

	/* 6. Set initial brightness (0-255): 50 = ~20% */
	ledStrip.brightness = 50;

	/* 7. Run LED initialization test sequence */
	WS2812_InitSequence(&ledStrip);

	/*==============================================================================================
	 *                                 ADC INITIALIZATION
	 ==============================================================================================*/

	/* 8. Initialize ADC Hardware Unit */
	Adc_Sar_Ip_Init(ADCHWUNIT_0_INSTANCE, &AdcHwUnit_0);

	/* 9. Install and enable ADC interrupt handler */
	IntCtrl_Ip_InstallHandler(ADC0_IRQn, Adc_Sar_0_Isr, NULL_PTR);
	IntCtrl_Ip_EnableIrq(ADC0_IRQn);

	/* 10. Perform ADC Calibration (multiple times for accuracy) */
	for (uint8 Index = 0; Index <= 5; Index++) {
		Adc_Sar_Ip_DoCalibration(ADCHWUNIT_0_INSTANCE);
	}

	/* 11. Enable ADC End of Chain notifications */
	Adc_Sar_Ip_EnableNotifications(ADCHWUNIT_0_INSTANCE,
		ADC_SAR_IP_NOTIF_FLAG_NORMAL_ENDCHAIN);

	/*==============================================================================================
	 *                                 MAIN LOOP - ADC & LED CONTROL
	 ==============================================================================================*/

	while (1)
	{
		/* Read button states (inverted because buttons pull to GND when pressed) */
		usrControl.btInc = !Siul2_Dio_Ip_ReadPin(BTN_INC_SW2_PORT, BTN_INC_SW2_PIN);
		usrControl.btDec = !Siul2_Dio_Ip_ReadPin(BTN_DEC_SW3_PORT, BTN_DEC_SW3_PIN);

		/* Process button inputs */
		BoardButtons();

		/* Start ADC Conversion */
		Adc_Sar_Ip_StartConversion(ADCHWUNIT_0_INSTANCE, ADC_SAR_IP_CONV_CHAIN_NORMAL);

		/* Wait for ADC conversion to complete */
		while (notif_triggered != TRUE);
		notif_triggered = FALSE;

		/* Read ADC raw value */
		ui16AdcRawValue = data;

		/* Convert ADC raw value to millivolts */
		fAdcValue = ((float) ui16AdcRawValue * ADC_VREFH_mV) / ui16AdcMax;

		/* Apply exponential smoothing filter to reduce LED flickering */
		/* Formula: filtered = (alpha * new_value) + ((1 - alpha) * previous_filtered) */
		/* Lower alpha (0.05-0.1) = more smoothing, slower response */
		/* Higher alpha (0.3-0.5) = less smoothing, faster response */
		fAdcValueFiltered = (ADC_FILTER_ALPHA * fAdcValue) + ((1.0f - ADC_FILTER_ALPHA) * fAdcValueFiltered);

		/* Map the filtered ADC value to LED brightness (0-255) */
		if (fAdcValueFiltered > ADC_VREFH_mV) {
			fAdcValueFiltered = ADC_VREFH_mV;
		}
		uint8_t targetBrightness = (uint8_t)((fAdcValueFiltered * 255.0f) / ADC_VREFH_mV);

		/* Set LED strip brightness */
		ledStrip.brightness = targetBrightness;

		/* Clear all LEDs */
		WS2812_Clear(&ledStrip);

		/* Set LED color based on selected mode */
		uint8_t r = 0, g = 0, b = 0;
		switch(activeColorIndex) {
			case 0: r = 255; g = 0;   b = 0;   break;  /* Red */
			case 1: r = 0;   g = 255; b = 0;   break;  /* Green */
			case 2: r = 0;   g = 0;   b = 255; break;  /* Blue */
			case 3: r = 255; g = 255; b = 0;   break;  /* Yellow */
			case 4: r = 0;   g = 255; b = 255; break;  /* Cyan */
			case 5: r = 255; g = 0;   b = 255; break;  /* Magenta */
			case 6: r = 255; g = 255; b = 255; break;  /* White */
			case 7: break;  /* Random - handled below */
		}

		/* Apply color to all LEDs */
		if (activeColorIndex == 7) {
			/* Random color mode */
			for (uint16_t i = 0; i < NUM_LEDS; i++) {
				WS2812_SetPixel(&ledStrip, i, rand() % 256, rand() % 256, rand() % 256);
				delay_cycles(50000);
			}
		} else {
			/* Solid color mode */
			for (uint16_t i = 0; i < NUM_LEDS; i++) {
				WS2812_SetPixel(&ledStrip, i, r, g, b);
			}
		}

		/* Update LED strip display */
		WS2812_Show(&ledStrip);

		/* Small delay for stability */
		delay_cycles(100000);
	}

	return 0;
}
