#include <stdbool.h>
#include "emulator.h"

// https://www.phatcode.net/res/218/files/limems40.txt
// https://www.phatcode.net/res/219/files/xms20.txt
// http://www.techhelpmanual.com/944-xms_functions.html
// http://www.techhelpmanual.com/651-emm_functions.html
// http://www.techhelpmanual.com/650-expanded_memory_specification__ems_.html
// http://www.techhelpmanual.com/943-extended_memory_specification__xms_.html
typedef struct umb {
    uint16_t segment;
    uint16_t size; // paragraphs
    bool allocated;
} umb_t;

typedef struct  {
    uint32_t length;
    uint16_t source_handle;
    uint32_t source_offset;
    uint16_t destination_handle;
    uint32_t destination_offset;
} __attribute__((packed)) move_data_t;

static umb_t umb_blocks[] = {
// Used by EMS driver
//        { 0xC000, 0x0800, false },
//        { 0xC800, 0x0800, false },
        { 0xD000, 0x0800, false },
        { 0xD800, 0x0800, false },
        { 0xE000, 0x0800, false },
        { 0xE800, 0x0800, false },
        { 0xF000, 0x0800, false },
        { 0xF800, 0x0400, false },
};
#define UMB_BLOCKS_COUNT (sizeof(umb_blocks) / sizeof(umb_t))

static int umb_blocks_allocated = 0;


umb_t *get_free_umb_block(uint16_t size) {
    for (int i = umb_blocks_allocated; i < UMB_BLOCKS_COUNT; i++)
        if (umb_blocks[i].allocated == false && umb_blocks[i].size <= size) {
            return &umb_blocks[i];
        }
    return NULL;
}


#define XMS_VERSION 0x00
#define REQUEST_HMA 0x01
#define RELEASE_HMA 0x02
#define GLOBAL_ENABLE_A20 0x03
#define GLOBAL_DISABLE_A20 0x04
#define LOCAL_ENABLE_A20 0x05
#define LOCAL_DISABLE_A20 0x06
#define QUERY_A20 0x07

#define QUERY_EMB 0x08
#define ALLOCATE_EMB 0x09
#define RELEASE_EMB 0x0A
#define MOVE_EMB 0x0B
#define LOCK_EMB 0x0C
#define UNLOCK_EMB 0x0D
#define EMB_HANDLE_INFO 0x0E
#define REALLOCATE_EMB 0x0F

#define REQUEST_UMB 0x10
#define RELEASE_UMB 0x11

#define XMS_SIZE (1024 << 10)
#define XMS_HANDLES
uint8_t XMS[XMS_SIZE * 4] = { 0 };
uint32_t xms_available = XMS_SIZE;

struct {
    uint8_t handle;
    uint32_t size; //
    uint8_t locks;
} XMS_HANDLE[XMS_HANDLES] = { 0 };
uint8_t xms_handles = 0;

int a20_enabled = 0;

uint8_t xms_handler() {
    if (CPU_AH > 0x7 && CPU_AH < 0x10)
    printf("[XMS] %02X\n", CPU_AH);
    switch (CPU_AH) {
        case XMS_VERSION: { // Get XMS Version
            CPU_AX = 0x0200;
            CPU_BX = 0x0001; // driver version
            CPU_DX = 0x0001; // HMA Exist
            break;
        }
        case REQUEST_HMA: { // Request HMA
            // Stub: Implement HMA request functionality
            CPU_AX = 1; // Success
            break;
        }
        case RELEASE_HMA: { // Release HMA
            // Stub: Implement HMA release functionality
            CPU_AX = 1; // Success
            break;
        }
        case GLOBAL_ENABLE_A20:
        case LOCAL_ENABLE_A20: { // Local Enable A20
            CPU_AX = 1; // Success
            CPU_BL = 0;
            a20_enabled = 1;
            break;
        }
        case GLOBAL_DISABLE_A20:
        case LOCAL_DISABLE_A20: { // Local Disable A20
            CPU_AX = 1; // Failed
            CPU_BL = 0;
            a20_enabled = 0;
            break;
        }
        case QUERY_A20: { // Query A20 (Function 07h):
            CPU_AX = 1; // Success
            break;
        }

        case QUERY_EMB : { // 08h
            printf("[XMS] Query free\r\n");
            CPU_AX = XMS_SIZE >> 10;
            CPU_DX = 64;
            CPU_BL = 0;
            break;
        }
        case ALLOCATE_EMB: { // Allocate Extended Memory Block (Function 09h):
            printf("[XMS] Allocate %dKb\n", CPU_DX);
            // Stub: Implement allocating extended memory block functionality
            CPU_DX = 0xBEEF;
            CPU_AX = 1;
            CPU_BL = 0;
            break;
        }

        case MOVE_EMB: {
                uint32_t struct_offset = ((uint32_t) CPU_DS << 4) + CPU_SI;
                move_data_t move_data = { 0 };
                uint8_t * move_data_ptr = (uint8_t *)&move_data;
                for (int i = 0; i < sizeof(move_data_t); i++) {
                    *move_data_ptr++ = readw86(struct_offset++);
                }

            printf("[XMS] Move EMB 0x%06X\r\n\t length 0x%08X \r\n\t src_handle 0x%04X \r\n\t src_offset 0x%08X \r\n\t dest_handle 0x%04X \r\n\t dest_offset 0x%08X \r\n",
                   struct_offset,
                   move_data.length,
                   move_data.source_handle,
                   move_data.source_offset,
                   move_data.destination_handle,
                   move_data.destination_offset
            );
            break;
        }
        case REQUEST_UMB: { // Request Upper Memory Block (Function 10h):
            if (CPU_DX == 0xFFFF) {
                if (umb_blocks_allocated < UMB_BLOCKS_COUNT) {
                    umb_t *umb_block = get_free_umb_block(CPU_DX);
                    if (umb_block != NULL) {
                        CPU_DX = umb_block->size;
                        CPU_BX = 0x00B0; // Success
                        CPU_AX = 0x0000; // Success
                        break;
                    }
                }
            } else {
                umb_t *umb_block = get_free_umb_block(CPU_DX);
                if (umb_block != NULL) {
                    CPU_BX = umb_block->segment;
                    CPU_DX = umb_block->size;
                    CPU_AX = 0x0001;

                    umb_block->allocated = true;
                    umb_blocks_allocated++;
                    break;
                }
            }

            CPU_AX = 0x0000;
            CPU_DX = 0x0000;
            CPU_BL = umb_blocks_allocated >= UMB_BLOCKS_COUNT ? 0xB1 : 0xB0;
            break;
        }
        case RELEASE_UMB: { // Release Upper Memory Block (Function 11h)
            // Stub: Release Upper Memory Block
            for (int i = 0; i < UMB_BLOCKS_COUNT; i++)
                if (umb_blocks[i].segment == CPU_BX && umb_blocks[i].allocated) {
                    umb_blocks[i].allocated = false;
                    umb_blocks_allocated--;
                    CPU_AX = 0x0001; // Success
                    CPU_BL = 0;
                    break;
                }

            CPU_AX = 0x0000; // Failure
            CPU_DX = 0x0000;
            CPU_BL = 0xB2; // Error code
            break;
        }
        default: {
            // Unhandled function
            CPU_AX = 0x0001; // Function not supported
            CPU_BL = 0x80; // Function not implemented
            break;
        }
    }
    return 0xCB; // RETF opcode
}