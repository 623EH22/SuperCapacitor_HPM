/*
*************************************************************************************************************************
*                                                       DRV_FLASH
*                            Driver module for the 500W Super-Capacitor Digital Power Supply.
* Filename      : drv_flash.h
* Version       : V1.01.00
* Programmer(s) : RuiYuan Lin
*************************************************************************************************************************
* Note(s)       : HPM5151 on-package serial NOR, accessed through XPI0.
*************************************************************************************************************************
*                                                   MODULE INFORMATION
*************************************************************************************************************************
* Layer         : DRV / Device driver
* Role          : Public API and types for on-chip flash read / write
* Counterpart   : User/Driver/drv_flash.h
* Status        : Ported, HPM XPI NOR implementation
*/

#ifndef DRV_FLASH_H
#define DRV_FLASH_H

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
#ifdef DEF_DRV_FLASH
#define EXT_DRV_FLASH
#else
#define EXT_DRV_FLASH extern
#endif

/*
*************************************************************************************************************************
*                                                     PUBLIC DEFINES
*************************************************************************************************************************
*/
#define STORAGE_SIZE               (STORAGE_END_ADDR - STORAGE_START_ADDR)
#define STORAGE_USE_PAGE_NUM       (STORAGE_SIZE / FLASH_PAGE_SIZE)

#define STORAGE_PAGE_ADDR(x)       (STORAGE_START_ADDR + ((x) * FLASH_PAGE_SIZE))
#define STORAGE_ERASE_PAGE_MASK(x) (1U << (x))
#define IS_STORAGE_PAGE_MASK(mask) (((mask) >> STORAGE_USE_PAGE_NUM) == 0U)

#define DRV_FLASH_OK               0U
#define DRV_FLASH_ERR_VERIFY       0x80000000U
#define DRV_FLASH_ERR_INIT         0x80000001U
#define DRV_FLASH_ERR_RANGE        0x80000002U
#define DRV_FLASH_ERR_WRITE        0x80000004U
#define DRV_FLASH_ERR_ERASE        0x80000008U

/*
*************************************************************************************************************************
*                                                      PUBLIC TYPES
*************************************************************************************************************************
*/
typedef uint32_t (*DEF_FLASH_WRITE)(uint32_t address, void *data, uint32_t length);
typedef void (*DEF_FLASH_READ)(uint32_t address, void *data, uint32_t length);
typedef uint32_t (*DEF_FLASH_ERASE_PAGE)(uint32_t mask);

typedef struct drv_flash_func_handle_t {
    DEF_FLASH_WRITE      flash_write;
    DEF_FLASH_READ       flash_read;
    DEF_FLASH_ERASE_PAGE flash_page_erase;
} DRV_FLASH_FUNC_HANDLE_T;

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
EXT_DRV_FLASH uint32_t                 Drv_Flash_Write(uint32_t address, void *data, uint32_t length);
EXT_DRV_FLASH void                     Drv_Flash_Read(uint32_t address, void *data, uint32_t length);
EXT_DRV_FLASH uint32_t                 Drv_Flash_Page_Erase(uint32_t mask);
EXT_DRV_FLASH DRV_FLASH_FUNC_HANDLE_T *DRV_Flash_GetFuncHandle(void);

#endif /* DRV_FLASH_H */
