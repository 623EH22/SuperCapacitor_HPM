/*
*************************************************************************************************************************
*                                                       DRV_PWM
*                            Driver module for the 500W Super-Capacitor Digital Power Supply.
* Filename      : drv_pwm.c
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
* Role          : Complementary PWM generation with dead time
* Counterpart   : User/Driver/drv_hrtim.c (STM32 HRTIM, retargeted to HPM PWM2)
* Status        : Ported, HPM PWM2 implementation
*/

/*
*************************************************************************************************************************
*                                                     INCLUDE FILES
*************************************************************************************************************************
*/
#define DEF_DRV_PWM
#include "drv_pwm.h"
#include "board.h"
#include "hpm_common.h"
#include "hpm_interrupt.h"
#include "hpm_iomux.h"
#include "hpm_clock_drv.h"
#include "hpm_pwm_drv.h"
#include "hpm_synt_drv.h"
#include "hpm_trgm_drv.h"

/*
*************************************************************************************************************************
*                                                    PRIVATE DEFINES
*************************************************************************************************************************
*/
#define DRV_PWM_BASE                HPM_PWM2
#define DRV_PWM_CLK_NAME            clock_pwm2
#define DRV_PWM_TRGM_BASE           HPM_TRGM0
#define DRV_PWM_TRGM_SYNC_TGT       TRGM_TRGOCFG_PWM2_SYNCI
#define DRV_PWM_TRGM_LOAD_TGT       TRGM_TRGOCFG_PWM2_SHRLDSYNCI
#define DRV_PWM_TRGM_SYNT_SRC       HPM_TRGM0_INPUT_SRC_SYNT_CH00
#define DRV_PWM_SYNT_BASE           HPM_SYNT
#define DRV_PWM_SYNT_CMP_SYNC       SYNT_CMP_0
#define DRV_PWM_SYNT_CMP_HALF       SYNT_CMP_1

/* Four outputs take part in the two complementary pairs. */
#define DRV_PWM_OUT_COUNT           4U

/* Reload below this leaves no usable duty resolution next to the dead time. */
#define DRV_PWM_MIN_RELOAD          100U

/* PWM_RLD_RLD_MASK covers the field at its shifted position, the raw limit is that mask shifted back. */
#define DRV_PWM_RLD_MAX             (PWM_RLD_RLD_MASK >> PWM_RLD_RLD_SHIFT)

/*
 * SYNT has no clock_synt enumerator and no sysctl gate on this SoC. The manual only says that the block
 * runs on the motor-system clock, which on HPM5100 is derived from the AHB domain, and PWM/SYNT clock
 * ratio is 1:1 either way. Override DRV_PWM_SYNT_CLK_HZ if a board feeds SYNT from another source.
 */
#ifndef DRV_PWM_SYNT_CLK_HZ
#define DRV_PWM_SYNT_CLK_HZ         (clock_get_frequency(clock_ahb))
#endif

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
static bool     s_pwm_ready;
static uint32_t s_pwm_clk_hz;
static uint32_t s_pwm_reload;
static uint32_t s_pwm_dead_half;
static uint32_t s_pwm_freq_hz;
static uint32_t s_pwm_dead_ns;

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
static void Drv_Pwm_InitPins(void)
{
    HPM_IOC->PAD[IOC_PAD_PA12].FUNC_CTL = IOC_PA12_FUNC_CTL_PWM2_P_0;
    HPM_IOC->PAD[IOC_PAD_PA13].FUNC_CTL = IOC_PA13_FUNC_CTL_PWM2_P_1;
    HPM_IOC->PAD[IOC_PAD_PA14].FUNC_CTL = IOC_PA14_FUNC_CTL_PWM2_P_2;
    HPM_IOC->PAD[IOC_PAD_PA15].FUNC_CTL = IOC_PA15_FUNC_CTL_PWM2_P_3;
}

static void Drv_Pwm_DisableOutputs(void)
{
    uint8_t i;

    for (i = 0U; i < DRV_PWM_OUT_COUNT; i++) {
        pwm_disable_output(DRV_PWM_BASE, i);
    }
}

/*
 * DEADAREA counts half PWM clock cycles, so one full clock is 2 and the hardware minimum is 2.
 * Manual example: DEADAREA = 0xA0 gives a dead time of 80 clock cycles, i.e. value * 0.5 cycle.
 */
static uint32_t Drv_Pwm_DeadTimeToHalfCycle(uint32_t dead_ns, uint32_t clk_hz)
{
    uint32_t half_cycle = (uint32_t)(((uint64_t)dead_ns * (uint64_t)clk_hz) / 500000000ULL);

    if (half_cycle < 2U) {
        half_cycle = 2U;
    }
    if (half_cycle > PWM_PWMCFG_DEADAREA_MASK) {
        half_cycle = PWM_PWMCFG_DEADAREA_MASK;
    }
    return half_cycle;
}

/*
 * Map a duty cycle to the two compare points of a center aligned channel.
 * Both points above RLD never match, which is the only way to get a real 0 %.
 */
static void Drv_Pwm_DutyToCmp(uint32_t reload, uint16_t duty_permille, uint32_t *cmp_low, uint32_t *cmp_high)
{
    uint32_t duty_cnt;
    uint32_t lo;
    uint32_t hi;
    uint32_t min_pulse;

    if (duty_permille == 0U) {
        *cmp_low  = reload + 1U;
        *cmp_high = reload + 1U;
        return;
    }
    if (duty_permille >= DRV_PWM_DUTY_FULL) {
        /* Single edge: the high point never matches, the output stays on until the reload. */
        *cmp_low  = 1U;
        *cmp_high = reload + 1U;
        return;
    }

    duty_cnt = (uint32_t)(((uint64_t)(reload + 1U) * (uint64_t)duty_permille) / DRV_PWM_DUTY_FULL);

    lo = (reload - duty_cnt) >> 1;
    hi = (reload + duty_cnt) >> 1;

    /* Center aligned generation requires STA < CMP0 < CMP1 < RLD. */
    if (lo == 0U) {
        lo = 1U;
    }
    if (hi > reload) {
        hi = reload;
    }
    if (hi <= lo) {
        hi = lo + 1U;
    }

    /*
     * Each edge is delayed by one dead time delay, so a pulse only has to be longer than a single
     * delay to survive; the other delay fills the opposite side. dead_half counts half cycles while
     * hi/lo count full cycles, hence one delay is dead_half / 2 full cycles.
     */
    min_pulse = (s_pwm_dead_half / 2U) + 1U;
    if (min_pulse < 2U) {
        min_pulse = 2U;
    }
    if ((hi - lo) < min_pulse) {
        lo = (reload - min_pulse) >> 1;
        hi = lo + min_pulse;
    }

    *cmp_low  = lo;
    *cmp_high = hi;
}

/*
 * Pair mode rules from the user manual: both PWMCFGx[PAIR] bits must be set or the complement is ignored,
 * the even channel configuration wins and the odd channel simply outputs its inverse.
 * The compare pair is shared, so P0/P1 use CMP0/CMP1 and P2/P3 use CMP2/CMP3.
 */
static hpm_stat_t Drv_Pwm_ConfigOnePair(pwm_pair_config_t *pair_cfg, uint8_t pwm_index,
                                        pwm_cmp_config_t *cmp)
{
    return pwm_setup_waveform_in_pair(DRV_PWM_BASE, pwm_index, pair_cfg, pwm_index, cmp, 2U);
}

/*
*************************************************************************************************************************
*                                               GLOBAL FUNCTION PROTOTYPES
*************************************************************************************************************************
*/
uint32_t Drv_Pwm_Init(uint32_t pwm_freq_hz, uint32_t synt_freq_hz)
{
    uint32_t          reload;
    uint32_t          dead_half;
    uint32_t          synt_clk;
    uint32_t          synt_reload;
    uint32_t          cmp_low;
    uint32_t          cmp_high;
    pwm_cmp_config_t  cmp[2] = {0};
    pwm_pair_config_t pair_cfg;
    trgm_output_t     trgm_cfg;

    /* ---- Validation. Nothing is driven and nothing is left half configured when a check fails. ---- */
    if ((pwm_freq_hz == 0U) || (synt_freq_hz == 0U)) {
        return DRV_PWM_ERR_PARAM;
    }

    clock_add_to_group(DRV_PWM_CLK_NAME, 0);
    s_pwm_clk_hz = clock_get_frequency(DRV_PWM_CLK_NAME);
    if (s_pwm_clk_hz == 0U) {
        return DRV_PWM_ERR_PARAM;
    }

    reload = s_pwm_clk_hz / pwm_freq_hz;
    if (reload < DRV_PWM_MIN_RELOAD) {
        return DRV_PWM_ERR_PARAM;
    }
    reload -= 1U;
    /* Keep one count of headroom so reload + 1 still fits the 24 bit compare field. */
    if (reload > (DRV_PWM_RLD_MAX - 1U)) {
        return DRV_PWM_ERR_PARAM;
    }

    /*
     * Every period pays the dead time on both edges, so the two delays together occupy dead_half
     * full clock cycles. Reject anything that would eat half a period or more.
     */
    dead_half = Drv_Pwm_DeadTimeToHalfCycle(DRV_PWM_DEAD_TIME_NS, s_pwm_clk_hz);
    if ((dead_half * 2U) >= reload) {
        return DRV_PWM_ERR_PARAM;
    }

    /* The SYNT counter runs 0 .. RLD and reloads to 0, so one period spans RLD + 1 clock cycles. */
    synt_clk = (uint32_t)(DRV_PWM_SYNT_CLK_HZ);
    if (synt_clk == 0U) {
        return DRV_PWM_ERR_PARAM;
    }
    synt_reload = synt_clk / synt_freq_hz;
    if (synt_reload < 2U) {
        return DRV_PWM_ERR_PARAM;
    }
    synt_reload -= 1U;

    /* ---- From here on the configuration cannot fail. ---- */
    if (s_pwm_ready) {
        Drv_Pwm_DeInit();
    }
    pwm_deinit(DRV_PWM_BASE);
    /*
     * pwm_deinit leaves every comparator at the all ones mask, which never matches. Push that into the
     * work registers right away so the outputs stay low until the first real compare value is latched.
     */
    pwm_issue_shadow_register_lock_event(DRV_PWM_BASE);

    Drv_Pwm_InitPins();

    s_pwm_reload    = reload;
    s_pwm_dead_half = dead_half;
    s_pwm_freq_hz   = s_pwm_clk_hz / (reload + 1U);
    s_pwm_dead_ns   = (uint32_t)(((uint64_t)dead_half * 500000000ULL) / s_pwm_clk_hz);

    pwm_set_reload(DRV_PWM_BASE, 0U, reload);
    pwm_shadow_register_unlock(DRV_PWM_BASE);
    pwm_set_start_count(DRV_PWM_BASE, 0U, 0U);

    /*
     * Start at 0 % duty. These compare values stay in the shadow registers until a SYNT pulse latches
     * them; the work registers still hold the all ones mask from pwm_deinit, so both outputs stay low
     * (high side off, low side on) until the application sets a duty cycle.
     */
    Drv_Pwm_DutyToCmp(reload, 0U, &cmp_low, &cmp_high);
    cmp[0].cmp            = cmp_low;
    cmp[1].cmp            = cmp_high;
    cmp[0].mode           = pwm_cmp_mode_output_compare;
    cmp[1].mode           = pwm_cmp_mode_output_compare;
    cmp[0].update_trigger = pwm_shadow_register_update_on_sh_synci;
    cmp[1].update_trigger = pwm_shadow_register_update_on_sh_synci;

    pwm_get_default_pwm_pair_config(DRV_PWM_BASE, &pair_cfg);
    pair_cfg.pwm[0].enable_output           = true;
    pair_cfg.pwm[1].enable_output           = true;
    pair_cfg.pwm[0].dead_zone_in_half_cycle = dead_half;
    pair_cfg.pwm[1].dead_zone_in_half_cycle = dead_half;

    if (Drv_Pwm_ConfigOnePair(&pair_cfg, 0U, cmp) != status_success) {   /* P0 / P1 -> CMP0 / CMP1 */
        Drv_Pwm_DisableOutputs();
        return DRV_PWM_ERR_PARAM;
    }
    if (Drv_Pwm_ConfigOnePair(&pair_cfg, 2U, cmp) != status_success) {   /* P2 / P3 -> CMP2 / CMP3 */
        Drv_Pwm_DisableOutputs();
        return DRV_PWM_ERR_PARAM;
    }

    /*
     * All four duty comparators use CMPSHDWUPT = 2'b11, so a single SHRLDSYNCI pulse latches both
     * points of every pair at once. That gives the update real hardware atomicity and keeps it away
     * from the middle of a pulse, which the manual requires for on the fly duty changes.
     */
    pwm_start_counter(DRV_PWM_BASE);

    /*
     * SYNT time base. CH00 fires near the period start and drives PWM2 SYNCI, CH01 marks the half
     * period for ADC or control loop use. One clock is used for CH00 because a compare value equal
     * to the counter start is not defined by the manual.
     */
    synt_enable_counter(DRV_PWM_SYNT_BASE, false);
    synt_reset_counter(DRV_PWM_SYNT_BASE);
    synt_set_reload(DRV_PWM_SYNT_BASE, synt_reload);
    synt_set_comparator(DRV_PWM_SYNT_BASE, DRV_PWM_SYNT_CMP_SYNC, 1U);
    synt_set_comparator(DRV_PWM_SYNT_BASE, DRV_PWM_SYNT_CMP_HALF, synt_reload >> 1);
    synt_enable_counter(DRV_PWM_SYNT_BASE, true);

    /*
     * The load pulse is always wired so every duty update is latched by hardware, even when the two
     * rates differ. Only the counter reset is rate dependent: tying SYNCI to a time base with a
     * different period would truncate or stretch the PWM period.
     */
    trgm_cfg.invert = false;
    trgm_cfg.type   = trgm_output_pulse_at_input_rising_edge;
    trgm_cfg.input  = DRV_PWM_TRGM_SYNT_SRC;
    trgm_output_config(DRV_PWM_TRGM_BASE, DRV_PWM_TRGM_LOAD_TGT, &trgm_cfg);

    if ((reload + 1U) == (synt_reload + 1U)) {
        trgm_output_config(DRV_PWM_TRGM_BASE, DRV_PWM_TRGM_SYNC_TGT, &trgm_cfg);
        pwm_enable_reload_at_synci(DRV_PWM_BASE);
    } else {
        trgm_output_t idle_cfg;

        /* Different periods, the counter has to keep running freely. */
        idle_cfg.invert = false;
        idle_cfg.type   = trgm_output_same_as_input;
        idle_cfg.input  = HPM_TRGM0_INPUT_SRC_VSS;
        trgm_output_config(DRV_PWM_TRGM_BASE, DRV_PWM_TRGM_SYNC_TGT, &idle_cfg);
        DRV_PWM_BASE->GCR &= ~(PWM_GCR_RLDSYNCEN_MASK | PWM_GCR_XRLDSYNCEN_MASK);
    }

    s_pwm_ready = true;
    return DRV_PWM_OK;
}

void Drv_Pwm_DeInit(void)
{
    trgm_output_t trgm_cfg;

    Drv_Pwm_DisableOutputs();
    pwm_stop_counter(DRV_PWM_BASE);
    pwm_deinit(DRV_PWM_BASE);

    /* Drop both TRGM connections so a later user of these outputs does not inherit the tie. */
    trgm_cfg.invert = false;
    trgm_cfg.type   = trgm_output_same_as_input;
    trgm_cfg.input  = HPM_TRGM0_INPUT_SRC_VSS;
    trgm_output_config(DRV_PWM_TRGM_BASE, DRV_PWM_TRGM_SYNC_TGT, &trgm_cfg);
    trgm_output_config(DRV_PWM_TRGM_BASE, DRV_PWM_TRGM_LOAD_TGT, &trgm_cfg);

    s_pwm_ready   = false;
    s_pwm_freq_hz = 0U;
    s_pwm_dead_ns = 0U;
}

void Drv_Pwm_Start(void)
{
    uint8_t i;

    /*
     * Refuse to run without a valid configuration: pwm_deinit leaves RLD and every comparator at the
     * all ones mask, which the hardware reads as 100 % duty with pair mode disabled.
     */
    if (!s_pwm_ready) {
        return;
    }

    /*
     * Start the counter before enabling the outputs. Enabling them while the counter is still frozen
     * would push whatever level the latches held at the stop instant straight back onto the pins.
     */
    pwm_start_counter(DRV_PWM_BASE);
    for (i = 0U; i < DRV_PWM_OUT_COUNT; i++) {
        pwm_enable_output(DRV_PWM_BASE, i);
    }
}

void Drv_Pwm_Stop(void)
{
    if (!s_pwm_ready) {
        return;
    }

    /*
     * Order matters: a stopped counter never reloads, and the output latches are only cleared at
     * reload, so the outputs have to be disabled before the counter stops.
     */
    Drv_Pwm_DisableOutputs();
    pwm_stop_counter(DRV_PWM_BASE);
}

uint32_t Drv_Pwm_SetDuty(uint8_t pair, uint16_t duty_permille)
{
    uint32_t cmp_low;
    uint32_t cmp_high;
    uint32_t irq_level;
    uint8_t  idx_low;

    if (!s_pwm_ready) {
        return DRV_PWM_ERR_NOT_READY;
    }
    if (pair >= DRV_PWM_PAIR_NUM) {
        return DRV_PWM_ERR_PARAM;
    }
    if (duty_permille > DRV_PWM_DUTY_FULL) {
        duty_permille = DRV_PWM_DUTY_FULL;
    }

    Drv_Pwm_DutyToCmp(s_pwm_reload, duty_permille, &cmp_low, &cmp_high);

    idx_low = (uint8_t)(pair * 2U);

    /*
     * Both values are shadow writes, the hardware latches them together on the next SYNT load pulse.
     * The critical section only stops an interrupt from writing the same pair halfway through; it
     * cannot hold off the load pulse, so a pulse landing between the two writes stays possible, at
     * roughly one update in a few thousand.
     */
    irq_level = disable_global_irq(CSR_MSTATUS_MIE_MASK);
    pwm_shadow_register_unlock(DRV_PWM_BASE);
    pwm_cmp_update_cmp_value(DRV_PWM_BASE, idx_low, cmp_low, 0U);
    pwm_cmp_update_cmp_value(DRV_PWM_BASE, (uint8_t)(idx_low + 1U), cmp_high, 0U);
    restore_global_irq(irq_level);

    return DRV_PWM_OK;
}

uint32_t Drv_Pwm_GetFreq(void)
{
    return s_pwm_freq_hz;
}

uint32_t Drv_Pwm_GetDeadTimeNs(void)
{
    return s_pwm_dead_ns;
}
