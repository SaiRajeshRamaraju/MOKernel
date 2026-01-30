#ifndef PMM_H
#define PMM_H

#include "paging.h" // For types

/* 
   Simple Physical Memory Manager 
   Manages allocation of physical page frames.
*/

/* Initialize PMM with total system memory size (in bytes) */
void pmm_init(uint32_t mem_size);

/* Allocate a physical page frame. Returns physical address or 0 if out of memory. */
uint32_t pmm_alloc_page(void);

/* Free a physical page frame. */
void pmm_free_page(uint32_t phys_addr);

#endif
