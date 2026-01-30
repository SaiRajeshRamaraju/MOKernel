#ifndef SWAP_H
#define SWAP_H

#include "paging.h" // For types

/* 
   Swap Manager (Simulated)
   Manages a fake disk store for demand paging.
*/

/* Initialize mocked disk storage */
void swap_init(void);

/* Check if a virtual address exists in swap storage (i.e., is it a valid page compliant for loading?) */
int swap_exists(uint32_t virt_addr);

/* Read page data from swap into a buffer (phys_addr) */
void swap_read(uint32_t virt_addr, uint32_t phys_addr);

/* Write page data from buffer (phys_addr) to swap */
void swap_write(uint32_t virt_addr, uint32_t phys_addr);

/* Register a virtual address as "swapped out" containing specific data 
   (For testing purposes, we pre-fill some data)
*/
void swap_test_store(uint32_t virt_addr, const char* data);

#endif
