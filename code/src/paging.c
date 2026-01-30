#include "paging.h"
#include "pmm.h"
#include "swap.h"

/* 
   We reference external functions to print to screen or handle errors.
   For now, we assume simple availability or just define minimal helpers.
   In a real kernel, we'd include "kernel.h" or similar.
*/
extern void *vidptr;
extern unsigned int current_loc;

/* Address of the page directory (must be 4KB aligned) */
__attribute__((aligned(PAGE_SIZE)))
uint32_t page_directory[PAGE_ENTRIES];

/* First page table (to map the first 4MB of memory) */
__attribute__((aligned(PAGE_SIZE)))
uint32_t first_page_table[PAGE_ENTRIES];

/* 
   Helper to print simple strings. 
   Duplicated from kernel.c slightly or we should expose it from kernel.c 
   but for now we keep it minimal here or just don't print complex things.
*/
static void print_string(const char *str) {
    char *v = (char*)0xb8000; // default VGA
    // This is VERY hacky, relying on global current_loc external
    // Ideally we expose a proper print function.
    /* For safety, let's just do a direct write if we panic? 
       Actually, let's just write to the buffer at current_loc if possible,
       but we need access to 'current_loc'.
    */
    /* simplified assumption: we just won't print for now inside this file 
       unless we link against kernel generic print. */
}

void paging_init(void)
{
    uint32_t i;

    /* 1. Clear page directory */
    for (i = 0; i < PAGE_ENTRIES; i++) {
        /* Attribute: Supervisor, Read/Write, Not Present */
        page_directory[i] = 0x00000002; 
    }

    /* 2. Identity-map first 4 MB */
    for (i = 0; i < PAGE_ENTRIES; i++) {
        /* (i * 4096) | Present | R/W | Supervisor */
        first_page_table[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_RW;
    }

    /* 3. Register the first page table in the directory */
    /* Entry 0 maps virtual addresses 0x00000000 - 0x003FFFFF */
    page_directory[0] = ((uint32_t)first_page_table) | PAGE_PRESENT | PAGE_RW;

    /* 4. Load Page Directory Base Register (CR3) */
    asm volatile("mov %0, %%cr3" :: "r"(page_directory));

    /* 5. Enable Paging (Set PG bit in CR0) */
    uint32_t cr0;
    asm volatile("mov %%cr0, %0": "=r"(cr0));
    cr0 |= 0x80000000; // Set PG bit
    asm volatile("mov %0, %%cr0":: "r"(cr0));

    /* Initialize PMM and Swap */
    pmm_init(0x1000000); // Assume 16MB RAM
    swap_init();
}

void map_page(uint32_t phys_addr, uint32_t virt_addr, uint32_t flags)
{
    /* Calculate indexes */
    uint32_t pd_index = virt_addr >> 22;
    uint32_t pt_index = (virt_addr >> 12) & 0x03FF;

    /* Check if the page table exists */
    if (!((page_directory[pd_index]) & PAGE_PRESENT)) {
        /* Page table not present. In a full OS, we'd allocate a new physical page for the table.
           Here, we don't have a physical memory manager yet! 
           This is a limitation. We can only map into the first 4MB for now unless we statically alloc more tables.
           
           CRITICAL: for this task, if we try to map outside existing tables, we effectively fail or need a simpler allocator.
           For now, let's just assume we only map if table exists or we are reusing the first one.
        */
        return; // Fail silently or panic
    }

    uint32_t *pt = (uint32_t *)(page_directory[pd_index] & ~0xFFF);
    
    /* Since we are not mapping the page tables themselves to virtual memory recursively yet,
       we must access them by physical address IF identity mapped. 
       Luckily first 4MB is identity mapped, and our static arrays likely fall there.
    */
    
    pt[pt_index] = (phys_addr & ~0xFFF) | (flags & 0xFFF) | PAGE_PRESENT;
    
    /* Invalidate TLB for this address */
    asm volatile("invlpg (%0)" :: "r" (virt_addr) : "memory");
}

void unmap_page(uint32_t virt_addr)
{
    uint32_t pd_index = virt_addr >> 22;
    uint32_t pt_index = (virt_addr >> 12) & 0x03FF;

    if (page_directory[pd_index] & PAGE_PRESENT) {
        uint32_t *pt = (uint32_t *)(page_directory[pd_index] & ~0xFFF);
        pt[pt_index] = 0; // Clear entry
        asm volatile("invlpg (%0)" :: "r" (virt_addr) : "memory");
    }
}

void page_fault_handler(void)
{
    uint32_t faulting_address;
    asm volatile("mov %%cr2, %0" : "=r" (faulting_address));

    /* Demand Paging Logic */
    /* Check if this address is in our Swap Store */
    if (swap_exists(faulting_address)) {
        /* It is! Allocate a new physical frame */
        uint32_t new_phys = pmm_alloc_page();
        if (new_phys == 0) {
           // OOM Panic
           goto panic;
        }

        /* Load data from swap */
        swap_read(faulting_address, new_phys);

        /* Map it (User + RW) */
        map_page(new_phys, faulting_address, PAGE_PRESENT | PAGE_RW | PAGE_USER);

        /* Print "Loaded" message to screen (bottom row) */
        char *video = (char*)0xb8000;
        const char *msg = "Page Fault Handled: Loaded from Swap!";
        int i = 0;
        int offset = 80 * 24 * 2; // Bottom row
        while(msg[i]) {
            video[offset + i*2] = msg[i];
            video[offset + i*2+1] = 0x02; // Green text
            i++;
        }
        
        return; // Resume execution
    }

panic:
    /* Original Panic Code */
    char *video = (char*)0xb8000;
    
    const char *msg = "PAGE FAULT DETECTED!";
    int i = 0;
    while(msg[i]) {
        video[i*2] = msg[i];
        video[i*2+1] = 0x4F; // Red background, White text
        i++;
    }
    
    while(1) asm volatile("hlt");
}
