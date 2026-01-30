#include "swap.h"

// Simulate 1MB of swap space (256 pages)
#define SWAP_MAX_PAGES 256
#define SWAP_INVALID 0xFFFFFFFF

typedef struct {
    uint32_t virt_addr; // The virtual address this page belongs to
    uint8_t data[PAGE_SIZE];
    int used;
} swap_page_t;

static swap_page_t swap_storage[SWAP_MAX_PAGES];

void swap_init(void)
{
    for (int i = 0; i < SWAP_MAX_PAGES; i++) {
        swap_storage[i].used = 0;
        swap_storage[i].virt_addr = 0;
    }
}

int swap_find_index(uint32_t virt_addr)
{
    // Align address
    uint32_t aligned = virt_addr & ~0xFFF;
    for (int i = 0; i < SWAP_MAX_PAGES; i++) {
        if (swap_storage[i].used && swap_storage[i].virt_addr == aligned) {
            return i;
        }
    }
    return -1;
}

int swap_exists(uint32_t virt_addr)
{
    return swap_find_index(virt_addr) != -1;
}

void swap_read(uint32_t virt_addr, uint32_t phys_addr)
{
    int idx = swap_find_index(virt_addr);
    if (idx != -1) {
        // Copy data from swap to physical RAM
        uint8_t *dest = (uint8_t *)phys_addr;
        for (int i = 0; i < PAGE_SIZE; i++) {
            dest[i] = swap_storage[idx].data[i];
        }
    }
}

void swap_write(uint32_t virt_addr, uint32_t phys_addr) {
     int idx = swap_find_index(virt_addr);
     // If not found, find free slot
     if (idx == -1) {
         for (int i=0; i < SWAP_MAX_PAGES; i++) {
             if (!swap_storage[i].used) {
                 idx = i;
                 swap_storage[i].used = 1;
                 swap_storage[i].virt_addr = virt_addr & ~0xFFF;
                 break;
             }
         }
     }
     
     if (idx != -1) {
        uint8_t *src = (uint8_t *)phys_addr;
        for (int i = 0; i < PAGE_SIZE; i++) {
            swap_storage[idx].data[i] = src[i];
        }
     }
}

void swap_test_store(uint32_t virt_addr, const char* data)
{
    // Helper to setup a test case: Creates a swapped-out page with known data
    uint32_t aligned = virt_addr & ~0xFFF;
    int idx = -1;
    
    // Find free
    for (int i=0; i < SWAP_MAX_PAGES; i++) {
         if (!swap_storage[i].used) {
             idx = i;
             break;
         }
    }
    
    if (idx != -1) {
        swap_storage[idx].used = 1;
        swap_storage[idx].virt_addr = aligned;
        
        // Copy string
        int c = 0;
        while(data[c] && c < PAGE_SIZE) {
            swap_storage[idx].data[c] = (uint8_t)data[c];
            c++;
        }
    }
}
