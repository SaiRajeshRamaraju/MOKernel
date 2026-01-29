// paging.c - 32-bit x86 bare-metal paging example
// NO standard headers, NO libc

/* =======================
   Basic integer types
   ======================= */
typedef unsigned int  uint32_t;

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
   Page directory & table
   ======================= */
__attribute__((aligned(PAGE_SIZE)))
uint32_t page_directory[PAGE_ENTRIES];

__attribute__((aligned(PAGE_SIZE)))
uint32_t first_page_table[PAGE_ENTRIES];

/* =======================
   Paging setup
   ======================= */
void paging_init(void)
{
    uint32_t i;

    /* Clear page directory */
    for (i = 0; i < PAGE_ENTRIES; i++) {
        page_directory[i] = 0x00000000;
    }

    /* Identity-map first 4 MB */
    for (i = 0; i < PAGE_ENTRIES; i++) {
        first_page_table[i] =
            (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_RW;
    }

    /* Point first PDE to first page table */
    page_directory[0] =
        ((uint32_t)first_page_table) | PAGE_PRESENT | PAGE_RW;

    /* Load page directory into CR3 */
    asm volatile(
        "mov %0, %%cr3"
        :
        : "r"(page_directory)
    );

    /* Enable paging (set PG bit in CR0) */
    asm volatile(
        "mov %%cr0, %%eax\n"
        "or  $0x80000000, %%eax\n"
        "mov %%eax, %%cr0"
        :
        :
        : "eax"
    );
}

