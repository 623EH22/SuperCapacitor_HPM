/*
*************************************************************************************************************************
*                                                       APP_SUPERCAP
*                             Application module for the 500W Super-Capacitor Digital Power Supply.
* Filename      : app_supercap.c
* Version       : V1.00.00
* Programmer(s) : RuiYuan Lin
*************************************************************************************************************************
* Note(s)       : TBD
*************************************************************************************************************************
*                                                   MODULE INFORMATION
*************************************************************************************************************************
* Layer         : APP / Application logic
* Role          : Super-capacitor charge / discharge control state machine
* Counterpart   : User/App/app_supercap.c
* Status        : Ported, implementation kept
*/

/*
*************************************************************************************************************************
*                                                     INCLUDE FILES
*************************************************************************************************************************
*/
#define DEF_APP_SUPERCAP

#include "app_supercap.h"
#include "board.h"
#include <stdio.h>

#define CAP_UNDERVOLTAGE_MV   (800U)
#define CAP_READY_MV          (2200U)
#define CAP_OVERVOLTAGE_MV    (2700U)
#define CAP_TASK_PERIOD_MS    (500U)
#define CAP_LOG_PERIOD_MS     (2000U)

static cap_state_t s_state;
static uint16_t    s_voltage_mv;
static uint32_t    s_last_task_ms;
static uint32_t    s_last_log_ms;
static volatile uint32_t s_tick_ms;

static void app_tick_callback(void)
{
    s_tick_ms += 1U;
}

static const char *cap_state_name(cap_state_t state)
{
    switch (state) {
    case CAP_STATE_UNDERVOLTAGE: return "UNDERVOLTAGE";
    case CAP_STATE_CHARGING:     return "CHARGING";
    case CAP_STATE_READY:        return "READY";
    case CAP_STATE_OVERVOLTAGE:  return "OVERVOLTAGE";
    default:                     return "UNKNOWN";
    }
}

void app_supercap_init(void)
{
    s_state = CAP_STATE_UNDERVOLTAGE;
    s_voltage_mv = 0U;
    s_last_task_ms = 0U;
    s_last_log_ms = 0U;
    s_tick_ms = 0U;
    board_timer_create(1U, app_tick_callback);
}

void app_supercap_task(void)
{
    uint32_t now = s_tick_ms;
    cap_state_t next;

    if ((uint32_t)(now - s_last_task_ms) < CAP_TASK_PERIOD_MS) {
        return;
    }
    s_last_task_ms = now;

    /* 1. 采集 */

    /* 2. 决策：在这里换成你的充放电 / 均衡 / 保护策略 */
    if (s_voltage_mv >= CAP_OVERVOLTAGE_MV) {
        next = CAP_STATE_OVERVOLTAGE;
    } else if (s_voltage_mv >= CAP_READY_MV) {
        next = CAP_STATE_READY;
    } else if (s_voltage_mv >= CAP_UNDERVOLTAGE_MV) {
        next = CAP_STATE_CHARGING;
    } else {
        next = CAP_STATE_UNDERVOLTAGE;
    }

    /* 3. 执行 */
    if (next != s_state) {
        s_state = next;
#if SUPERCAP_LOG
        printf("[supercap] state -> %s (%u mV)\r\n", cap_state_name(s_state), s_voltage_mv);
#endif
    } else if ((uint32_t)(now - s_last_log_ms) >= CAP_LOG_PERIOD_MS) {
        s_last_log_ms = now;
#if SUPERCAP_LOG
        printf("[supercap] %s, %u mV\r\n", cap_state_name(s_state), s_voltage_mv);
#endif
    }
}

cap_state_t app_supercap_get_state(void)
{
    return s_state;
}

uint16_t app_supercap_get_voltage_mv(void)
{
    return s_voltage_mv;
}

/*
*************************************************************************************************************************
*                                                    PRIVATE DEFINES
*************************************************************************************************************************
*/

/*
*************************************************************************************************************************
*                                                     PRIVATE TYPES
*************************************************************************************************************************
*/

/*
*************************************************************************************************************************
*                                                   PRIVATE VARIABLES
*************************************************************************************************************************
*/

/*
*************************************************************************************************************************
*                                                    PUBLIC VARIABLES
*************************************************************************************************************************
*/

/*
*************************************************************************************************************************
*                                               PRIVATE (HELPER) FUNCTIONS
*************************************************************************************************************************
*/

/*
*************************************************************************************************************************
*                                               GLOBAL FUNCTION PROTOTYPES
*************************************************************************************************************************
*/
