/*
*************************************************************************************************************************
*                                                         HAL_STORAGE
*                             HAL module for the 500W Super-Capacitor Digital Power Supply.
* Filename      : hal_storage.c
* Version       : V1.00.00
* Programmer(s) : RuiYuan Lin
*************************************************************************************************************************
* Note(s)       : TBD
*************************************************************************************************************************
*                                                  MODIFICATION HISTORY
*************************************************************************************************************************
*   Version   Date          Author        Description
*   V1.00.00  2026-09-16    RuiYuan Lin   function realization
*************************************************************************************************************************
*/

/*
*************************************************************************************************************************
*                                                     INCLUDE FILES
*************************************************************************************************************************
*/
#define DEF_HAL_STORAGE
#include "hal_storage.h"
#include "drv_flash.h"
#include <stddef.h>
#include "string.h"

/*
*************************************************************************************************************************
*                                                    PRIVATE DEFINES
*************************************************************************************************************************
*/
#define DEF_STORAGE_PAGE_MAGIC  0x53544F52

#define DEF_STORAGE_PAGE_ERASED 0xFFFFFFFFU
#define DEF_STORAGE_PAGE_ACTIVE 0xB0B0B0B0U
#define DEF_STORAGE_PAGE_FULL   0x00000000U

#define DEF_STORAGE_ID_LENTH    (sizeof(uint16_t))
#define DEF_STORAGE_INVALID_ID  0x0000U
#define DEF_STORAGE_ERASED_ID   0xFFFFU
#define DEF_STORAGE_EXTENDED_ID 0xFF00U

#define DEF_STORAGE_RECORD_BUF_SIZE 256U

/*
*************************************************************************************************************************
*                                                     PRIVATE TYPES
*************************************************************************************************************************
*/
typedef enum {
    STORAGE_PAGE_INVALID = 0,
    STORAGE_PAGE_FIRST,
    STORAGE_PAGE_ACTIVE,
    STORAGE_PAGE_RECEIVE,
    STORAGE_PAGE_FULL,
} STORAGE_PAGE_STATUS_E;

typedef struct {
    uint32_t magic;
    uint32_t status;
    uint32_t seq;
} STORAGE_PAGE_HEADER_T;

typedef struct {
    uint16_t id;
    uint32_t length;
    void    *data;
} STORAGE_ITEM_T;

typedef struct {
    STORAGE_ITEM_T       storage_item_table[STORAGE_ITEM_MAX_COUNT];
    HAL_STORAGE_STATUS_E status;
    uint8_t              page;
    uint8_t              item_count;
    uint32_t             current;
    uint32_t             seq;
} HAL_STORAGE_INFO_HANDLE_T;

/*
*************************************************************************************************************************
*                                                   PRIVATE VARIABLES
*************************************************************************************************************************
*/
HAL_STORAGE_INFO_HANDLE_T g_storage_page_info;

/*
*************************************************************************************************************************
*                                                    PUBLIC VARIABLES
*************************************************************************************************************************
*/
HAL_STORAGE_FUNC_INFO_HANDLE_T g_hal_storage_func_info = {
    .register_func  = Hal_Storage_Register,
    .init_func      = Hal_Storage_Init,
    .para_updt_func = Hal_Storage_ParaUpdt,
};
/*
*************************************************************************************************************************
*                                               PRIVATE (HELPER) FUNCTIONS
*************************************************************************************************************************
*/
static uint32_t Hal_Storage_RecordSize(uint32_t length)
{
    /* layout: [id:2][data:length], record start aligned to FLASH_MIN_WRITE_SIZE */
    return (DEF_STORAGE_ID_LENTH + length + FLASH_MIN_WRITE_SIZE - 1U) & ~(FLASH_MIN_WRITE_SIZE - 1U);
}

static STORAGE_ITEM_T *Hal_Storage_FindItem(uint16_t id)
{
    HAL_STORAGE_INFO_HANDLE_T *ptr = &g_storage_page_info;

    for (uint8_t i = 0; i < ptr->item_count; i++) {
        if (ptr->storage_item_table[i].id == id) {
            return &ptr->storage_item_table[i];
        }
    }

    return NULL;
}

static void Hal_Storage_PageInit(uint32_t page, uint32_t seq)
{
    DRV_FLASH_FUNC_HANDLE_T *drv_func = DRV_Flash_GetFuncHandle();
    uint32_t                 address  = STORAGE_PAGE_ADDR(page);

    STORAGE_PAGE_HEADER_T    header;
    header.magic  = DEF_STORAGE_PAGE_MAGIC;
    header.status = DEF_STORAGE_PAGE_ACTIVE;
    header.seq    = seq;

    drv_func->flash_write(address, &header, sizeof(header));
}

static inline void Hal_Storage_GetPageHeader(uint8_t page, STORAGE_PAGE_HEADER_T *head)
{
    DRV_FLASH_FUNC_HANDLE_T *drv_func = DRV_Flash_GetFuncHandle();
    drv_func->flash_read(STORAGE_PAGE_ADDR(page), head, sizeof(STORAGE_PAGE_HEADER_T));
}

static inline void Hal_Storage_PageSetFull(uint32_t page)
{
    DRV_FLASH_FUNC_HANDLE_T *drv_func = DRV_Flash_GetFuncHandle();

    uint32_t                 buff[2];

    buff[0] = DEF_STORAGE_PAGE_FULL;
    buff[1] = DEF_STORAGE_PAGE_FULL;

    drv_func->flash_write(STORAGE_PAGE_ADDR(page), buff, sizeof(buff));
}

static STORAGE_PAGE_STATUS_E Hal_Storage_FindActivePage(uint32_t *page)
{
    uint8_t  i;
    uint32_t fault_page_mask = 0;

    for (i = 0; i < STORAGE_USE_PAGE_NUM; i++) {
        STORAGE_PAGE_HEADER_T head;

        Hal_Storage_GetPageHeader(i, &head);
        *page = i;
        if (head.magic == DEF_STORAGE_PAGE_MAGIC && head.status == DEF_STORAGE_PAGE_ACTIVE) {
            STORAGE_PAGE_HEADER_T last_head;

            uint8_t                j = (i - 1) & (STORAGE_USE_PAGE_NUM - 1);
            Hal_Storage_GetPageHeader(j, &last_head);

            if (last_head.magic == DEF_STORAGE_PAGE_ERASED && last_head.status == DEF_STORAGE_PAGE_ERASED) {
                *page = i;
                return STORAGE_PAGE_ACTIVE;
            } else if (last_head.magic == DEF_STORAGE_PAGE_FULL && last_head.status == DEF_STORAGE_PAGE_FULL) {
                *page = i;
                return STORAGE_PAGE_RECEIVE;
            } else {
                fault_page_mask |= 1 << i;
            }
        } else if (head.magic == DEF_STORAGE_PAGE_FULL && head.status == DEF_STORAGE_PAGE_FULL) {
            if (head.seq != 0) {
                *page = i;
                return STORAGE_PAGE_FULL;
            } else {
                fault_page_mask |= 1 << i;
            }
        } else if (head.magic != DEF_STORAGE_PAGE_ERASED || head.status != DEF_STORAGE_PAGE_ERASED) {
            fault_page_mask |= 1 << i;
        }
    }

    if (fault_page_mask == 0) {
        *page = 0;
        return STORAGE_PAGE_FIRST; /* 扫完都是擦除态 */
    } else {
        *page = fault_page_mask;
        return STORAGE_PAGE_INVALID;
    }
}

static uint32_t Hal_Storage_GetCurrentAddr(uint8_t page)
{
    DRV_FLASH_FUNC_HANDLE_T *drv_func   = DRV_Flash_GetFuncHandle();

    uint32_t                 page_addr  = STORAGE_PAGE_ADDR(page);
    uint32_t                 end_addr   = page_addr + FLASH_PAGE_SIZE;

    uint32_t                 magic_status[2];
    drv_func->flash_read(page_addr, magic_status, sizeof(magic_status));
    if (magic_status[0] == DEF_STORAGE_PAGE_ERASED && magic_status[1] == DEF_STORAGE_PAGE_ERASED) {
        return page_addr;
    }

    uint32_t addr = page_addr + sizeof(STORAGE_PAGE_HEADER_T);

    while (addr + DEF_STORAGE_ID_LENTH <= end_addr) {
        uint16_t id;
        drv_func->flash_read(addr, &id, sizeof(id));

        if (id == DEF_STORAGE_ERASED_ID) {
            break;
        }

        STORAGE_ITEM_T *item = Hal_Storage_FindItem(id);
        if (item == NULL) {
            break;
        }

        uint32_t record_size = Hal_Storage_RecordSize(item->length);
        if (addr + record_size > end_addr) {
            break;
        }

        addr += record_size;
    }

    return addr;
}

static void Hal_Storage_DataWrite(uint16_t id, const void *data, uint32_t length)
{
    HAL_STORAGE_INFO_HANDLE_T *ptr       = &g_storage_page_info;
    DRV_FLASH_FUNC_HANDLE_T   *drv_func  = DRV_Flash_GetFuncHandle();

    uint32_t                   record_size = Hal_Storage_RecordSize(length);
    uint32_t                   write_size  = DEF_STORAGE_ID_LENTH + length;

    if (write_size <= DEF_STORAGE_RECORD_BUF_SIZE) {
        uint32_t record_buf[DEF_STORAGE_RECORD_BUF_SIZE / sizeof(uint32_t)];
        uint8_t *record = (uint8_t *)record_buf;

        memcpy(record, &id, DEF_STORAGE_ID_LENTH);
        memcpy(record + DEF_STORAGE_ID_LENTH, data, length);

        drv_func->flash_write(ptr->current, record, write_size);
    } else {
        drv_func->flash_write(ptr->current, &id, DEF_STORAGE_ID_LENTH);
        drv_func->flash_write(ptr->current + DEF_STORAGE_ID_LENTH, (void *)data, length);
    }

    ptr->current += record_size;
}

static void Hal_Storage_PageTransfer(HAL_STORAGE_INFO_HANDLE_T *ptr)
{
    DRV_FLASH_FUNC_HANDLE_T *drv_func = DRV_Flash_GetFuncHandle();
    if (ptr->item_count == 0) {
        return;
    }

    uint8_t  next_page         = (ptr->page + 1) & (STORAGE_USE_PAGE_NUM - 1);
    uint32_t next_page_current = Hal_Storage_GetCurrentAddr(next_page);

    if (next_page_current != STORAGE_PAGE_ADDR(next_page)) {
        drv_func->flash_page_erase(STORAGE_ERASE_PAGE_MASK(next_page));
    }

    Hal_Storage_PageSetFull(ptr->page);

    uint32_t seq;
    drv_func->flash_read(STORAGE_PAGE_ADDR(ptr->page) + 8, &seq, sizeof(uint32_t));

    Hal_Storage_PageInit(next_page, seq + 1);

    ptr->seq     = seq + 1;

    ptr->current = STORAGE_PAGE_ADDR(next_page) + sizeof(STORAGE_PAGE_HEADER_T);

    for (uint8_t i = 0; i < ptr->item_count; i++) {
        Hal_Storage_DataWrite(ptr->storage_item_table[i].id, ptr->storage_item_table[i].data, ptr->storage_item_table[i].length);
    }

    drv_func->flash_page_erase(STORAGE_ERASE_PAGE_MASK(ptr->page));

    ptr->page = next_page;
}

static void Hal_Storage_DataRecoverFromFlash(HAL_STORAGE_INFO_HANDLE_T *ptr)
{
    DRV_FLASH_FUNC_HANDLE_T *drv_func = DRV_Flash_GetFuncHandle();

    uint32_t addr     = STORAGE_PAGE_ADDR(ptr->page) + sizeof(STORAGE_PAGE_HEADER_T);
    uint32_t end_addr = ptr->current;

    drv_func->flash_read(STORAGE_PAGE_ADDR(ptr->page) + 8, &ptr->seq, sizeof(uint32_t));

    while (addr + DEF_STORAGE_ID_LENTH <= end_addr) {
        uint16_t id;
        drv_func->flash_read(addr, &id, sizeof(id));

        if (id == DEF_STORAGE_ERASED_ID) {
            break;
        }

        STORAGE_ITEM_T *item = Hal_Storage_FindItem(id);
        if (item == NULL) {
            break;
        }

        uint32_t record_size = Hal_Storage_RecordSize(item->length);
        if (addr + record_size > end_addr) {
            break;
        }

        drv_func->flash_read(addr + DEF_STORAGE_ID_LENTH, item->data, item->length);
        addr += record_size;
    }
}

static void Hal_Storage_ErrorHandler(HAL_STORAGE_INFO_HANDLE_T *ptr)
{
    if (ptr->status != STORAGE_OK) {
        while (1) {
            /* Infinite loop to indicate error */
        }
    }
}
/*
*************************************************************************************************************************
*                                               GLOBAL FUNCTION PROTOTYPES
*************************************************************************************************************************
*/
void Hal_Storage_Register(uint16_t id, void *data, uint32_t length)
{
    HAL_STORAGE_INFO_HANDLE_T *ptr = &g_storage_page_info;
    uint32_t                   max_data_size = FLASH_PAGE_SIZE - sizeof(STORAGE_PAGE_HEADER_T) - DEF_STORAGE_ID_LENTH;

    if (id == DEF_STORAGE_INVALID_ID || id == DEF_STORAGE_ERASED_ID || id == DEF_STORAGE_EXTENDED_ID || data == NULL || length == 0U ||
        length > max_data_size) {
        return;
    }

    if (ptr->item_count >= STORAGE_ITEM_MAX_COUNT) {
        return;
    }

    if (Hal_Storage_FindItem(id) != NULL) {
        return;
    }

    uint32_t total_size      = sizeof(STORAGE_PAGE_HEADER_T);
    uint32_t max_record_size = 0;

    for (uint8_t i = 0; i < ptr->item_count; i++) {
        uint32_t record_size = Hal_Storage_RecordSize(ptr->storage_item_table[i].length);
        total_size += record_size;
        if (record_size > max_record_size) {
            max_record_size = record_size;
        }
    }

    uint32_t record_size = Hal_Storage_RecordSize(length);
    if (record_size > max_record_size) {
        max_record_size = record_size;
    }

    /* All registered items plus one update record must fit in a single page. */
    if ((total_size + record_size + max_record_size) > FLASH_PAGE_SIZE) {
        return;
    }

    /* Register new storage item */
    ptr->storage_item_table[ptr->item_count].id     = id;
    ptr->storage_item_table[ptr->item_count].data   = data;
    ptr->storage_item_table[ptr->item_count].length = length;

    ptr->item_count++;
}

void Hal_Storage_Init(void)
{
    HAL_STORAGE_INFO_HANDLE_T *ptr = &g_storage_page_info;
    uint32_t                   page;
    STORAGE_PAGE_STATUS_E      status;

    if (ptr->item_count != 0U) {
        status = Hal_Storage_FindActivePage(&page);
        switch (status) {
            case STORAGE_PAGE_FIRST: {
                Hal_Storage_PageInit(0, 1);
                ptr->page    = 0;
                ptr->current = STORAGE_PAGE_ADDR(ptr->page) + sizeof(STORAGE_PAGE_HEADER_T);
                break;
            }
            case STORAGE_PAGE_ACTIVE:
                ptr->page    = page;
                ptr->current = Hal_Storage_GetCurrentAddr(ptr->page);
                Hal_Storage_DataRecoverFromFlash(ptr);
                break;
            case STORAGE_PAGE_FULL:
                ptr->page    = page;
                ptr->current = Hal_Storage_GetCurrentAddr(ptr->page);
                Hal_Storage_DataRecoverFromFlash(ptr);
                Hal_Storage_PageTransfer(ptr);
                break;
            case STORAGE_PAGE_RECEIVE:
                ptr->page    = (uint8_t)((page - 1U) & (STORAGE_USE_PAGE_NUM - 1U));
                ptr->current = Hal_Storage_GetCurrentAddr(ptr->page);
                Hal_Storage_DataRecoverFromFlash(ptr);
                Hal_Storage_PageTransfer(ptr);
                break;
            case STORAGE_PAGE_INVALID:
                /* Handle invalid to check and clear a fault*/
                Hal_Storage_ErrorHandler(ptr);
                break;
            default:
                break;
        }
    } else {
        return;
    }
}

void Hal_Storage_ParaUpdt(uint16_t id, void *data, uint32_t length)
{
    HAL_STORAGE_INFO_HANDLE_T *ptr = &g_storage_page_info;

    if (data == NULL || length == 0U) {
        return;
    }

    STORAGE_ITEM_T *item = Hal_Storage_FindItem(id);
    if (item == NULL || item->length != length) {
        return;
    }

    uint32_t align_length = Hal_Storage_RecordSize(length);
    uint32_t page_end     = STORAGE_PAGE_ADDR(ptr->page) + FLASH_PAGE_SIZE;

    if ((ptr->current + align_length) > page_end) {
        Hal_Storage_PageTransfer(ptr);
    }

    if ((ptr->current + align_length) <= page_end) {
        Hal_Storage_DataWrite(id, data, length);
    }
}

HAL_STORAGE_FUNC_INFO_HANDLE_T *Hal_Storage_GetFuncHandle(void)
{
    return &g_hal_storage_func_info;
}
