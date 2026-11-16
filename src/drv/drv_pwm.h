/*
*************************************************************************************************************************
*                                                       DRV_PWM
*                            Driver module for the 500W Super-Capacitor Digital Power Supply.
* Filename      : drv_pwm.h
* Version       : V1.02.00
* Programmer(s) : RuiYuan Lin
*************************************************************************************************************************
* Note(s)       : HPM5151 advanced PWM (PWM2), 4 channels in 2 complementary pairs.
*                 - PA12..PA15 -> PWM2_P_0..P_3 (IOC ALT select 16)
*                 - P0/P1 and P2/P3 are hardware complementary pairs
*                 - Dead time unit is 0.5 PWM clock cycle, minimum 1 cycle
*                 - Duty compare values are loaded by hardware at the period end, never mid period
*                 - SYNT gives the time base, CH00 synchronizes PWM2 through TRGM
*************************************************************************************************************************
*                                                   MODULE INFORMATION
*************************************************************************************************************************
* Layer         : DRV / Device driver
* Role          : Public API and types for complementary PWM with dead time
* Counterpart   : User/Driver/drv_hrtim.h (STM32 HRTIM, retargeted to HPM PWM2)
* Status        : Ported, HPM PWM2 implementation
*/

#ifndef DRV_PWM_H
#define DRV_PWM_H

/*
*************************************************************************************************************************
*                                                     INCLUDE FILES
*************************************************************************************************************************
*/
#include <stdint.h>
#include "platform_config.h"

/*
*************************************************************************************************************************
*                                                     EXTERN DEFINES
*************************************************************************************************************************
*/
#ifdef DEF_DRV_PWM
#define EXT_DRV_PWM
#else
#define EXT_DRV_PWM extern
#endif

/*
*************************************************************************************************************************
*                                                     PUBLIC DEFINES
*************************************************************************************************************************
*/
/* DRV_PWM_DEAD_TIME_NS / DRV_PWM_FREQ_HZ / DRV_PWM_SYNT_FREQ_HZ / DRV_PWM_DUTY_FULL
 * are declared in platform_config.h so that all tuning lives in one file. */

/* Complementary pair index */
#define DRV_PWM_PAIR_0              0U      /* P0 / P1 */
#define DRV_PWM_PAIR_1              1U      /* P2 / P3 */
#define DRV_PWM_PAIR_NUM            2U

/* Return codes */
#define DRV_PWM_OK                  0U
#define DRV_PWM_ERR_PARAM           0x80000001U
#define DRV_PWM_ERR_NOT_READY       0x80000002U

/*
*************************************************************************************************************************
*                                                      PUBLIC TYPES
*************************************************************************************************************************
*/

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
 * @brief  Initialize PWM2 on PA12..PA15 as two complementary pairs, then start SYNT and the TRGM link.
 * @param  pwm_freq_hz  Switching frequency in Hz, also fixes the counter reload value.
 * @param  synt_freq_hz SYNT time base frequency in Hz, usually equal to pwm_freq_hz.
 * @return DRV_PWM_OK on success, otherwise DRV_PWM_ERR_PARAM.
 * @note   Outputs start at 0 % duty, which means high side off and low side on. Call Drv_Pwm_Stop()
 *         for a real all-off state, 0 % duty alone still keeps the low side conducting.
 * @note   Rejected when the dead time does not fit in half a period, or when the reload value
 *         overflows the 24 bit RLD field. Nothing is left running on a rejected call.
 * @note   The counter reset through TRGM is only armed when the two periods are equal, because a
 *         SYNT driven reset would otherwise truncate or stretch the PWM period. The duty update
 *         latch is wired in both cases.
 */
EXT_DRV_PWM uint32_t Drv_Pwm_Init(uint32_t pwm_freq_hz, uint32_t synt_freq_hz);

/**
 * @brief  Disable outputs, stop the counter and release the PWM2 configuration.
 * @note   Output enables are cleared first. A stopped counter never reloads, so the output
 *         latches would otherwise hold whatever level they had at that moment.
 * @note   SYNT is left running so that other consumers of the time base are not disturbed,
 *         but the TRGM connection to PWM2_SYNCI is removed.
 */
EXT_DRV_PWM void     Drv_Pwm_DeInit(void);

/**
 * @brief  Re-enable the four outputs and start the counter.
 */
EXT_DRV_PWM void     Drv_Pwm_Start(void);

/**
 * @brief  Disable the four outputs, then stop the counter. Use this to stop the bridge safely.
 */
EXT_DRV_PWM void     Drv_Pwm_Stop(void);

/**
 * @brief  Update the duty cycle of one complementary pair.
 * @param  pair          DRV_PWM_PAIR_0 (P0/P1) or DRV_PWM_PAIR_1 (P2/P3).
 * @param  duty_permille Duty in per mille, 0 .. DRV_PWM_DUTY_FULL.
 * @return DRV_PWM_OK, or DRV_PWM_ERR_NOT_READY when used before Drv_Pwm_Init().
 * @note   Both compare values are shadow values, hardware latches the pair together on the next
 *         SYNT load pulse, so a running period is normally left untouched.
 * @note   0 % is exact. Duty below one dead time delay is widened to the shortest pulse the bridge
 *         can actually produce, so the usable range is about DT/period .. 1 - DT/period.
 * @note   Above roughly 99 % the dead time eats into the high side pulse, a 100 % request lands
 *         near 95 % and leaves no usable low side pulse. Keep the loop away from that corner.
 */
EXT_DRV_PWM uint32_t Drv_Pwm_SetDuty(uint8_t pair, uint16_t duty_permille);

/**
 * @brief  Get the switching frequency currently programmed.
 * @return Frequency in Hz, 0 before initialization.
 */
EXT_DRV_PWM uint32_t Drv_Pwm_GetFreq(void);

/**
 * @brief  Get the dead time currently programmed.
 * @return Dead time in nanoseconds, 0 before initialization.
 */
EXT_DRV_PWM uint32_t Drv_Pwm_GetDeadTimeNs(void);

#endif /* DRV_PWM_H */
