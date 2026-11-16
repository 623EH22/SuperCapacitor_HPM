/*
*************************************************************************************************************************
*                                                       DRV_ADC
*                            Driver module for the 500W Super-Capacitor Digital Power Supply.
* Filename      : drv_adc.c
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
* Role          : ADC sampling (capacitor voltage / current)
* Counterpart   : User/Driver/drv_adc.c
* Status        : Ported, HPM ADC16 implementation
*/

/*
*************************************************************************************************************************
*                                                     INCLUDE FILES
*************************************************************************************************************************
*/
#define DEF_DRV_ADC
#include "drv_adc.h"
#include "board.h"
#include "hpm_common.h"
#include "hpm_ioc_regs.h"
#include "hpm_iomux.h"
#include "hpm_clock_drv.h"

/*
*************************************************************************************************************************
*                                                    PRIVATE DEFINES
*************************************************************************************************************************
*/
#define DRV_ADC_INST_COUNT          4U
#define DRV_ADC_CHANNEL_MAX         16U

/*
*************************************************************************************************************************
*                                                     PRIVATE TYPES
*************************************************************************************************************************
*/
typedef struct {
    uint8_t inst;   /* 0..3, index into s_adc_inst */
    uint8_t ch;     /* ADC channel number */
    uint8_t pad;    /* IOC PAD index */
} DRV_ADC_MAP_T;

/*
*************************************************************************************************************************
*                                                   PRIVATE VARIABLES
*************************************************************************************************************************
*/
static ADC16_Type *const s_adc_inst[DRV_ADC_INST_COUNT] = {
    HPM_ADC0, HPM_ADC1, HPM_ADC2, HPM_ADC3,
};

static const DRV_ADC_MAP_T s_adc_map[DRV_ADC_CH_COUNT] = {
    { 0U, 13U, IOC_PAD_PA00 },  /* DRV_ADC_ADC0_IN13 */
    { 1U, 12U, IOC_PAD_PA24 },  /* DRV_ADC_ADC1_IN12 */
    { 2U,  1U, IOC_PAD_PC14 },  /* DRV_ADC_ADC2_IN01 */
    { 2U,  2U, IOC_PAD_PC13 },  /* DRV_ADC_ADC2_IN02 */
    { 2U,  3U, IOC_PAD_PC12 },  /* DRV_ADC_ADC2_IN03 */
    { 2U,  4U, IOC_PAD_PC11 },  /* DRV_ADC_ADC2_IN04 */
    { 2U,  6U, IOC_PAD_PC09 },  /* DRV_ADC_ADC2_IN06 */
    { 3U,  0U, IOC_PAD_PB02 },  /* DRV_ADC_ADC3_IN00 */
    { 3U,  5U, IOC_PAD_PC10 },  /* DRV_ADC_ADC3_IN05 */
    { 3U,  7U, IOC_PAD_PC08 },  /* DRV_ADC_ADC3_IN07 */
    { 3U, 12U, IOC_PAD_PB00 },  /* DRV_ADC_ADC3_IN12 */
    { 3U, 13U, IOC_PAD_PB03 },  /* DRV_ADC_ADC3_IN13 */
};

static bool     s_adc_ready;
static uint32_t s_adc_conv_hz;
static uint32_t s_adc_conv_cycles;

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
static void Drv_Adc_InitPins(void)
{
    uint8_t i;

    for (i = 0U; i < DRV_ADC_CH_COUNT; i++) {
        HPM_IOC->PAD[s_adc_map[i].pad].FUNC_CTL = IOC_PAD_FUNC_CTL_ANALOG_MASK;
    }
}

/*
*************************************************************************************************************************
*                                               GLOBAL FUNCTION PROTOTYPES
*************************************************************************************************************************
*/
uint32_t Drv_Adc_Init(void)
{
    adc16_config_t         cfg;
    adc16_channel_config_t ch_cfg;
    uint32_t               input_hz;
    uint32_t               div;
    uint8_t                i;
    uint8_t                inst;

    Drv_Adc_InitPins();

    for (inst = 0U; inst < DRV_ADC_INST_COUNT; inst++) {
        /* AHB source keeps the ADC in the motor system clock domain, needed for the trigger path later. */
        input_hz = board_init_adc_clock(s_adc_inst[inst], true);
        if (input_hz == 0U) {
            return DRV_ADC_ERR_PARAM;
        }
        if (adc16_calc_clock_divider(input_hz, DRV_ADC_CONV_CLK_HZ, &div) != status_success) {
            return DRV_ADC_ERR_PARAM;
        }
        s_adc_conv_hz = input_hz / div;

        adc16_get_default_config(&cfg);
        cfg.res         = adc16_res_16_bits;             /* conversion fixed at 21 ADC clocks */
        cfg.conv_mode   = adc16_conv_mode_oneshot;
        cfg.adc_clk_div = div;
        cfg.wait_dis    = false;                         /* blocking, the read waits for a valid result */
        if (adc16_init(s_adc_inst[inst], &cfg) != status_success) {
            return DRV_ADC_ERR_PARAM;
        }

        /* Every channel of this instance shares the same sample timing. */
        for (i = 0U; i < DRV_ADC_CH_COUNT; i++) {
            if (s_adc_map[i].inst != inst) {
                continue;
            }
            adc16_get_channel_default_config(&ch_cfg);
            ch_cfg.ch           = s_adc_map[i].ch;
            ch_cfg.sample_cycle = DRV_ADC_SAMPLE_CYCLE;
            if (adc16_init_channel(s_adc_inst[inst], &ch_cfg) != status_success) {
                return DRV_ADC_ERR_PARAM;
            }
        }

        adc16_set_blocking_read(s_adc_inst[inst]);
        adc16_enable_oneshot_mode(s_adc_inst[inst]);
    }

    s_adc_conv_cycles = DRV_ADC_SAMPLE_CYCLE + (uint32_t)adc16_res_16_bits;
    s_adc_ready       = true;

    return DRV_ADC_OK;
}

uint32_t Drv_Adc_ReadRaw(uint8_t instance, uint8_t channel, uint16_t *value)
{
    if (!s_adc_ready) {
        return DRV_ADC_ERR_NOT_READY;
    }
    if ((instance >= DRV_ADC_INST_COUNT) || (channel >= DRV_ADC_CHANNEL_MAX) || (value == NULL)) {
        return DRV_ADC_ERR_PARAM;
    }
    if (adc16_get_oneshot_result(s_adc_inst[instance], channel, value) != status_success) {
        return DRV_ADC_ERR_READ;
    }
    return DRV_ADC_OK;
}

uint32_t Drv_Adc_Read(DRV_ADC_CH_T ch, uint16_t *value)
{
    if (ch >= DRV_ADC_CH_COUNT) {
        return DRV_ADC_ERR_PARAM;
    }
    return Drv_Adc_ReadRaw(s_adc_map[ch].inst, s_adc_map[ch].ch, value);
}

uint32_t Drv_Adc_GetConvClock(void)
{
    return s_adc_conv_hz;
}

uint32_t Drv_Adc_GetConvCycles(void)
{
    return s_adc_conv_cycles;
}
