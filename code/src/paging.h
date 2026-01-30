#ifndef PAGING_H
#define PAGING_H

/* =======================
   Basic integer types
   ======================= */
typedef unsigned int   uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char  uint8_t;

/* =======================
   Paging constants
   ======================= */
#define PAGE_SIZE        4096
#define PAGE_ENTRIES     1024

/* Page entry flags */
#define PAGE_PRESENT     0x001
#define PAGE_RW          0x002
#define PAGE_USER        0x004

/* =======================
   Structures
   ======================= */
/* 
   Ideally we just use arrays of uint32_t, but 
   we can also use structs if we want to get fancy later.
   For now, we keep it simple as per original implementation.
*/

/* =======================
   Function Prototypes
   ======================= */
/* Initialize paging */
void paging_init(void);

/* Map a virtual address to a physical address */
/* phys_addr and virt_addr must be 4KB aligned */
void map_page(uint32_t phys_addr, uint32_t virt_addr, uint32_t flags);

/* Unmap a virtual page */
void unmap_page(uint32_t virt_addr);

/* Page fault handler - to be called from ISR */
/* regs argument type depends on your interrupt frame struct, 
   but for now we'll just take error code if passed */
void page_fault_handler(void);

#endif
