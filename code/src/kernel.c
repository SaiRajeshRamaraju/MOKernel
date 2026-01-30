
#define IDT_SIZE 256
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

struct IDT_entry
{
        unsigned short int offset_lowerbits;
        unsigned short int selector;
        unsigned char zero;
        unsigned char type_attr;
        unsigned short int offset_higherbits;
} __attribute__((packed));

struct IDT_entry IDT[IDT_SIZE];

extern void write_port(unsigned short port, unsigned char data);
extern unsigned char read_port(unsigned short port);
extern void load_idt(unsigned long *);
extern void keyboard_handler(void);
extern void page_fault_stub(void);
#include "./paging.h"
char *vidptr             = (char *)0xb8000;
unsigned int current_loc = 0;

unsigned char keyboard_map[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', // 0x0E =
                                                                             // Backspace
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',  // 0x1C = Enter
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', // Spacebar at 0x39
                                                  // Add more if needed
};

void keyboard_handler_main(void)
{
        unsigned char status;
        char keycode;

        write_port(0x20, 0x20); // Send EOI to PIC
        status = read_port(KEYBOARD_STATUS_PORT);

        if (status & 0x01)
        {
                keycode = read_port(KEYBOARD_DATA_PORT);
                if (keycode < 0)
                        return;

                char chr = keyboard_map[(unsigned char)keycode];

                if (chr == '\n')
                {
                        // Move to next line
                        current_loc += 160 - (current_loc % 160); // 80 cols * 2 bytes/char
                }
                else if (chr == '\b')
                {
                        // Backspace
                        if (current_loc >= 2)
                        {
                                current_loc -= 2;
                                vidptr[current_loc]     = ' ';
                                vidptr[current_loc + 1] = 0x07;
                        }
                }
                else if (chr)
                {
                        vidptr[current_loc++] = chr;
                        vidptr[current_loc++] = 0x07;
                }
        }
}
void idt_init(void)
{
        unsigned long keyboard_address = (unsigned long)keyboard_handler;
        unsigned long idt_address      = (unsigned long)IDT;
        unsigned long idt_ptr[2];

        IDT[0x21].offset_lowerbits  = keyboard_address & 0xFFFF;
        IDT[0x21].selector          = 0x08;
        IDT[0x21].zero              = 0;
        IDT[0x21].type_attr         = 0x8E;
        IDT[0x21].offset_higherbits = (keyboard_address >> 16) & 0xFFFF;

        // PIC remapping
        write_port(0x20, 0x11);
        write_port(0xA0, 0x11);
        write_port(0x21, 0x20);
        write_port(0xA1, 0x28);
        write_port(0x21, 0x00);
        write_port(0xA1, 0x00);
        write_port(0x21, 0x01);
        write_port(0xA1, 0x01);
        write_port(0x21, 0xFD); // enable only IRQ1 (keyboard)
        write_port(0xA1, 0xFF);

        /* Register Page Fault Handler (Vector 14) */
        unsigned long pf_address = (unsigned long)page_fault_stub;
        IDT[14].offset_lowerbits  = pf_address & 0xFFFF;
        IDT[14].selector          = 0x08;
        IDT[14].zero              = 0;
        IDT[14].type_attr         = 0x8E; // Interrupt Gate
        IDT[14].offset_higherbits = (pf_address >> 16) & 0xFFFF;

        idt_ptr[0] = (sizeof(struct IDT_entry) * IDT_SIZE) | ((idt_address & 0xFFFF) << 16);
        idt_ptr[1] = idt_address >> 16;
        load_idt((unsigned long *)idt_ptr);
}

void kmain(void)
{
        // kernel still not booting , struct on booting from ROM .
        const char *boot_msg = "Booting MOKernel...";
        unsigned int k = 0;
        /* Print simple message directly to VGA to verify boot */
        while(boot_msg[k] != '\0') {
            vidptr[k*2] = boot_msg[k];
            vidptr[k*2+1] = 0x02; // Green
            k++;
        }
        current_loc = 160; // Next line

        paging_init(); // Initialize Paging
        const char *str = "Kernel Message";
        unsigned int i  = 0;
	    volatile uint32_t *test = (uint32_t *)0x00100000; // 1 MB
		*test = 0xDEADBEEF;


        for (unsigned int j = 0; j < 80 * 25 * 2; j += 2)
        {
                vidptr[j]     = ' ';
                vidptr[j + 1] = 0x07;
        }

        while (str[i] != '\0')
        {
                vidptr[current_loc++] = str[i++];
                vidptr[current_loc++] = 0x07;
        }

        idt_init();
        while (1)
        {
                // halt CPU, waiting for interrupts
                asm volatile("hlt");
        }
}
