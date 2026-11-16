/*
*************************************************************************************************************************
*                                                       DRV_ADC
*                            Driver module for the 500W Super-Capacitor Digital Power Supply.
* Filename      : drv_adc.h
* Version       : V1.01.00
* Programmer(s) : RuiYuan Lin
*************************************************************************************************************************
* Note(s)       : HPM5151 ADC16, four instances sample 12 single-ended channels.
*                 - 16 bit resolution, so one conversion always takes 21 ADC clocks
*                 - Sample phase is programmable, DRV_ADC_SAMPLE_CYCLE in platform_config.h
*                 - Oneshot (bus) mode for now, the hardware trigger source is still open
*************************************************************************************************************************
*                                                   MODULE INFORMATION
*************************************************************************************************************************
* Layer         : DRV / Device driver
* Role          : ADC sampling for capacitor voltage and current
* Counterpart   : User/Driver/drv_adc.h
* Status        : Ported, HPM ADC16 implementation
*/

#ifndef DRV_ADC_H
#define DRV_ADC_H

/*
*************************************************************************************************************************
*                                                     INCLUDE FILES
*************************************************************************************************************************
*/
#include <stdint.h>
#include "platform_config.h"
#include "hpm_adc16_drv.h"

/*
*************************************************************************************************************************
*                                                     EXTERN DEFINES
*************************************************************************************************************************
*/
#ifdef DEF_DRV_ADC
#define EXT_DRV_ADC
#else
#define EXT_DRV_ADC extern
#endif

/*
*************************************************************************************************************************
*                                                     PUBLIC DEFINES
*************************************************************************************************************************
*/
/* Return codes */
#define DRV_ADC_OK                  0U
#define DRV_ADC_ERR_PARAM           0x80000001U
#define DRV_ADC_ERR_NOT_READY       0x80000002U
#define DRV_ADC_ERR_READ            0x80000004U

/*
*************************************************************************************************************************
*                                                      PUBLIC TYPES
*************************************************************************************************************************
*/
/**
 * @brief Channel index, one entry per schematic net.
 *
 * | idx | instance | channel | pin  |
 * |-----|----------|---------|------|
 * |  0  | ADC0     |   13    | PA00 |
 * |  1  | ADC1     |   12    | PA24 |
 * |  2  | ADC2     |    1    | PC14 |
 * |  3  | ADC2     |    2    | PC13 |
 * |  4  | ADC2     |    3    | PC12 |
 * |  5  | ADC2     |    4    | PC11 |
 * |  6  | ADC2     |    6    | PC09 |
 * |  7  | ADC3     |    0    | PB02 |
 * |  8  | ADC3     |    5    | PC10 |
 * |  9  | ADC3     |    7    | PC08 |
 * | 10  | ADC3     |   12    | PB00 |
 * | 11  | ADC3     |   13    | PB03 |
 */
typedef enum drv_adc_ch {
    DRV_ADC_ADC0_IN13 = 0,      /* PA00 */
    DRV_ADC_ADC1_IN12,          /* PA24 */
    DRV_ADC_ADC2_IN01,          /* PC14 */
    DRV_ADC_ADC2_IN02,          /* PC13 */
    DRV_ADC_ADC2_IN03,          /* PC12 */
    DRV_ADC_ADC2_IN04,          /* PC11 */
    DRV_ADC_ADC2_IN06,          /* PC09 */
    DRV_ADC_ADC3_IN00,          /* PB02 */
    DRV_ADC_ADC3_IN05,          /* PC10 */
    DRV_ADC_ADC3_IN07,          /* PC08 */
    DRV_ADC_ADC3_IN12,          /* PB00 */
    DRV_ADC_ADC3_IN13,          /* PB03 */
    DRV_ADC_CH_COUNT
} DRV_ADC_CH_T;

/*
*************************************************************************************************************************
*                                                    PUBLIC VARIABLES
*************************************************************************************************************************
*/

/*
*************************************************************************************************************************
*                                               GLOBAL FUNCTION PROTOTYPES
*************************************************************************************************************************
*/
/**
 * @brief  Initialize ADC0..ADC3 in oneshot mode and configure the 12 channels.
 * @return DRV_ADC_OK on success, otherwise DRV_ADC_ERR_PARAM.
 * @note   The ADC clock is taken from AHB so it stays in the motor system clock domain, which the
 *         manual requires once the hardware trigger path is added.
 * @note   The conversion clock is DRV_ADC_CONV_CLK_HZ, sampling takes DRV_ADC_SAMPLE_CYCLE clocks
 *         and the conversion itself is fixed at 21 clocks by the 16 bit resolution.
 */
EXT_DRV_ADC uint32_t Drv_Adc_Init(void);

/**
 * @brief  Read one channel in blocking oneshot mode.
 * @param  ch    Channel index, see @ref DRV_ADC_CH_T.
 * @param  value Pointer that receives the raw 16 bit result.
 * @return DRV_ADC_OK, DRV_ADC_ERR_NOT_READY, DRV_ADC_ERR_PARAM or DRV_ADC_ERR_READ.
 * @note   The read starts the conversion and waits for the result, roughly
 *         (DRV_ADC_SAMPLE_CYCLE + 21) ADC clocks.
 */
EXT_DRV_ADC uint32_t Drv_Adc_Read(DRV_ADC_CH_T ch, uint16_t *value);

/**
 * @brief  Read any channel of any instance, bypassing the index table.
 * @param  instance 0..3 for ADC0..ADC3.
 * @param  channel  Channel number 0..15.
 * @param  value    Pointer that receives the raw 16 bit result.
 * @return DRV_ADC_OK, DRV_ADC_ERR_NOT_READY, DRV_ADC_ERR_PARAM or DRV_ADC_ERR_READ.
 */
EXT_DRV_ADC uint32_t Drv_Adc_ReadRaw(uint8_t instance, uint8_t channel, uint16_t *value);

/**
 * @brief  Get the conversion clock actually programmed.
 * @return Conversion clock in Hz, 0 before initialization.
 */
EXT_DRV_ADC uint32_t Drv_Adc_GetConvClock(void);

/**
 * @brief  Get the number of ADC clocks one conversion takes, sample phase plus 21 conversion clocks.
 * @return Clock count, 0 before initialization.
 */
EXT_DRV_ADC uint32_t Drv_Adc_GetConvCycles(void);

#endif /* DRV_ADC_H */
