#include "pmm.h"

/* 
   For this simple kernel, we will use a very basic "Watermark Allocator" 
   for the free heap space. 
   
   We assume the kernel and initial paging setup utilize the first few MBs.
   We will start allocating from 4MB onwards.
*/

// Start allocation after 4MB to avoid kernel code
static uint32_t free_mem_start = 0x00400000; 
static uint32_t system_max_mem = 0;

void pmm_init(uint32_t mem_size)
{
    system_max_mem = mem_size;
}

uint32_t pmm_alloc_page(void)
{
    if (free_mem_start >= system_max_mem) {
        return 0; // Out of memory
    }

    uint32_t alloc = free_mem_start;
    free_mem_start += PAGE_SIZE;
    return alloc;
}

void pmm_free_page(uint32_t phys_addr)
{
    /* 
       A watermark allocator cannot easily free individual pages 
       without fragmentation or a bitmap. 
       For this specific "Demand Paging Demo" task, we can ignore freeing 
       since we just want to demonstrate loading on fault.
    */
    (void)phys_addr;
}
