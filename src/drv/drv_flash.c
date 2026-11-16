/*
*************************************************************************************************************************
*                                                       DRV_FLASH
*                            Driver module for the 500W Super-Capacitor Digital Power Supply.
* Filename      : drv_flash.c
* Version       : V1.01.00
* Programmer(s) : RuiYuan Lin
*************************************************************************************************************************
* Note(s)       : HPM5151 on-package serial NOR, accessed through XPI0.
*                 - 4KB sector erase, 256B page program
*                 - 1~256B per program, no ECC, 1 -> 0 only
*                 - ROM API uses a flash device offset, not the CPU address
*************************************************************************************************************************
*                                                   MODULE INFORMATION
*************************************************************************************************************************
* Layer         : DRV / Device driver
* Role          : On-chip flash read / write
* Counterpart   : User/Driver/drv_flash.c
* Status        : Ported, HPM XPI NOR implementation
*/

/*
*************************************************************************************************************************
*                                                     INCLUDE FILES
*************************************************************************************************************************
*/
#define DEF_DRV_FLASH
#include "drv_flash.h"
#include "board.h"
#include "hpm_common.h"
#include "hpm_interrupt.h"
#include "hpm_l1c_drv.h"
#include "hpm_romapi.h"
#include <string.h>

/*
*************************************************************************************************************************
*                                                    PRIVATE DEFINES
*************************************************************************************************************************
*/
#define FLASH_PAGE_PROGRAM_SIZE    256U
#define FLASH_PROGRAM_BOUNCE_WORDS (FLASH_PAGE_PROGRAM_SIZE / sizeof(uint32_t))

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
static xpi_nor_config_t        s_nor_config;
static bool                    s_nor_ready;

static DRV_FLASH_FUNC_HANDLE_T s_drv_flash_func = {
    .flash_write      = Drv_Flash_Write,
    .flash_read       = Drv_Flash_Read,
    .flash_page_erase = Drv_Flash_Page_Erase,
};

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
ATTR_RAMFUNC static hpm_stat_t Drv_Flash_InitOnce(void)
{
    xpi_nor_config_option_t option;

    if (s_nor_ready) {
        return status_success;
    }

    option.header.U   = BOARD_APP_XPI_NOR_CFG_OPT_HDR;
    option.option0.U  = BOARD_APP_XPI_NOR_CFG_OPT_OPT0;
    option.option1.U  = BOARD_APP_XPI_NOR_CFG_OPT_OPT1;
    option.option2.U  = 0U;

    hpm_stat_t status = rom_xpi_nor_auto_config(BOARD_APP_XPI_NOR_XPI_BASE, &s_nor_config, &option);
    if (status == status_success) {
        s_nor_ready = true;
    }
    return status;
}

ATTR_RAMFUNC static uint32_t Drv_Flash_ToOffset(uint32_t address)
{
    return address - BOARD_FLASH_BASE_ADDRESS;
}

ATTR_RAMFUNC static void Drv_Flash_Invalidate(uint32_t address, uint32_t length)
{
    uint32_t aligned_start = HPM_L1C_CACHELINE_ALIGN_DOWN(address);
    uint32_t aligned_end   = HPM_L1C_CACHELINE_ALIGN_UP(address + length);

    l1c_dc_invalidate(aligned_start, aligned_end - aligned_start);
}

/*
*************************************************************************************************************************
*                                               GLOBAL FUNCTION PROTOTYPES
*************************************************************************************************************************
*/
ATTR_RAMFUNC void Drv_Flash_Read(uint32_t address, void *data, uint32_t length)
{
    if (data == NULL || length == 0U) {
        return;
    }

    if (address < STORAGE_START_ADDR || address > STORAGE_END_ADDR || length > (STORAGE_END_ADDR - address)) {
        return;
    }

    Drv_Flash_Invalidate(address, length);
    memcpy(data, (const void *)address, length);
}

ATTR_RAMFUNC uint32_t Drv_Flash_Write(uint32_t address, void *data, uint32_t length)
{
    uint32_t       offset;
    uint32_t       end_addr;
    const uint8_t *src = (const uint8_t *)data;
    uint32_t       bounce[FLASH_PROGRAM_BOUNCE_WORDS];

    if (data == NULL || length == 0U) {
        return DRV_FLASH_ERR_RANGE;
    }

    if (address < STORAGE_START_ADDR || address > STORAGE_END_ADDR || length > (STORAGE_END_ADDR - address)) {
        return DRV_FLASH_ERR_RANGE;
    }

    if (Drv_Flash_InitOnce() != status_success) {
        return DRV_FLASH_ERR_INIT;
    }

    offset   = Drv_Flash_ToOffset(address);
    end_addr = address + length;

    while (length > 0U) {
        uint32_t   page_remain = FLASH_PAGE_PROGRAM_SIZE - (offset % FLASH_PAGE_PROGRAM_SIZE);
        uint32_t   chunk       = (length < page_remain) ? length : page_remain;
        uint32_t   irq_level;
        hpm_stat_t status;

        memset(bounce, 0xFF, sizeof(bounce));
        memcpy(bounce, src, chunk);

        irq_level = disable_global_irq(CSR_MSTATUS_MIE_MASK);
        status    = rom_xpi_nor_program(BOARD_APP_XPI_NOR_XPI_BASE, xpi_xfer_channel_auto,
                                        &s_nor_config, bounce, offset, chunk);
        restore_global_irq(irq_level);

        if (status != status_success) {
            return DRV_FLASH_ERR_WRITE;
        }

        src += chunk;
        offset += chunk;
        length -= chunk;
    }

    Drv_Flash_Invalidate(address, end_addr - address);

    return DRV_FLASH_OK;
}

ATTR_RAMFUNC uint32_t Drv_Flash_Page_Erase(uint32_t mask)
{
    uint32_t err = DRV_FLASH_OK;
    uint8_t  i;

    if (!IS_STORAGE_PAGE_MASK(mask)) {
        return DRV_FLASH_ERR_RANGE;
    }

    if (Drv_Flash_InitOnce() != status_success) {
        return DRV_FLASH_ERR_INIT;
    }

    for (i = 0; i < STORAGE_USE_PAGE_NUM; i++) {
        if ((mask & (1U << i)) == 0U) {
            continue;
        }

        uint32_t   address   = STORAGE_PAGE_ADDR(i);
        uint32_t   offset    = Drv_Flash_ToOffset(address);
        uint32_t   irq_level = disable_global_irq(CSR_MSTATUS_MIE_MASK);
        hpm_stat_t status    = rom_xpi_nor_erase_sector(BOARD_APP_XPI_NOR_XPI_BASE,
                                                        xpi_xfer_channel_auto,
                                                        &s_nor_config, offset);
        restore_global_irq(irq_level);

        if (status != status_success) {
            err = DRV_FLASH_ERR_ERASE;
            continue;
        }

        Drv_Flash_Invalidate(address, FLASH_PAGE_SIZE);

        for (uint32_t j = 0; j < FLASH_PAGE_SIZE; j += sizeof(uint32_t)) {
            if (*(volatile uint32_t *)(address + j) != 0xFFFFFFFFU) {
                err = DRV_FLASH_ERR_VERIFY;
                break;
            }
        }
    }

    return err;
}

ATTR_RAMFUNC DRV_FLASH_FUNC_HANDLE_T *DRV_Flash_GetFuncHandle(void)
{
    (void)Drv_Flash_InitOnce();
    return &s_drv_flash_func;
}

/*
*************************************************************************************************************************
*                                                   PRIVATE VARIABLES
*************************************************************************************************************************
*/

/*
*************************************************************************************************************************
*                                               PRIVATE (HELPER) FUNCTIONS
*************************************************************************************************************************
*/
