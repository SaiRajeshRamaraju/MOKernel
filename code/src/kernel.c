#define IDT_SIZE 256
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64
#define PIT_COMMAND_PORT 0x43
#define PIT_CHANNEL_0_PORT 0x40

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
extern void timer_handler(void);
extern void mouse_handler(void);
extern void page_fault_stub(void);

#include "./paging.h"
#include "./swap.h"
#include "./fs.h"

char *vidptr             = (char *)0xb8000;
unsigned int current_loc = 0;
volatile unsigned int timer_ticks = 0;

void kprint(const char *str)
{
        unsigned int i = 0;
        while (str[i] != '\0')
        {
                write_port(0x3F8, str[i]); // Echo to Serial COM1
                if (str[i] == '\n') {
                        current_loc += 160 - (current_loc % 160);
                } else if (str[i] == '\b') {
                        if (current_loc >= 2) {
                                current_loc -= 2;
                                vidptr[current_loc]     = ' ';
                                vidptr[current_loc + 1] = 0x07;
                        }
                } else {
                        vidptr[current_loc++] = str[i];
                        vidptr[current_loc++] = 0x07;
                }
                i++;
        }
}

void kprint_hex(unsigned int val) 
{
        char buf[16];
        int idx = 0;
        buf[idx++] = '0';
        buf[idx++] = 'x';
        if (val == 0) {
                buf[idx++] = '0';
        } else {
                char temp_buf[16];
                int temp_idx = 0;
                unsigned int temp = val;
                while(temp > 0) {
                        unsigned char nibble = temp & 0xF;
                        temp_buf[temp_idx++] = (nibble < 10) ? (nibble + '0') : (nibble - 10 + 'A');
                        temp >>= 4;
                }
                while(temp_idx > 0) {
                        buf[idx++] = temp_buf[--temp_idx];
                }
        }
        buf[idx] = '\0';
        kprint(buf);
}

void clear_screen(void)
{
        for (unsigned int j = 0; j < 80 * 25 * 2; j += 2)
        {
                vidptr[j]     = ' ';
                vidptr[j + 1] = 0x07;
        }
        current_loc = 0;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++; s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char *s1, const char *s2, int n) {
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++; s2++; n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

// ==== Terminal / Shell ====
#define CMD_BUFFER_SIZE 256
char cmd_buffer[CMD_BUFFER_SIZE];
int cmd_len = 0;

void execute_command(char* cmd) {
    char *c = cmd;
    while (*c == ' ') c++; // trim leading space

    if (*c == '\0') {
        // do nothing
    } else if (strcmp(c, "help") == 0) {
        kprint("Commands: \n");
        kprint("  help  - Show this message\n");
        kprint("  clear - Clear screen\n");
        kprint("  echo  - Print text (echo <msg>)\n");
        kprint("  info  - Show system info\n");
        kprint("  ls    - List all files\n");
        kprint("  touch - Create empty file (touch <filename>)\n");
        kprint("  write - Write text to file (write <filename> <content>)\n");
        kprint("  cat   - Read file content (cat <filename>)\n");
    } else if (strcmp(c, "clear") == 0) {
        clear_screen();
    } else if (strcmp(c, "ls") == 0) {
        fs_list_files();
    } else if (strncmp(c, "touch ", 6) == 0) {
        char *filename = c + 6;
        while (*filename == ' ') filename++;
        if (*filename != '\0') {
            fs_create_file(filename);
        } else {
            kprint("Usage: touch <filename>\n");
        }
    } else if (strncmp(c, "cat ", 4) == 0) {
        char *filename = c + 4;
        while (*filename == ' ') filename++;
        if (*filename != '\0') {
            fs_read_file(filename);
        } else {
            kprint("Usage: cat <filename>\n");
        }
    } else if (strncmp(c, "write ", 6) == 0) {
        char *args = c + 6;
        while (*args == ' ') args++;
        char filename[16];
        int i = 0;
        while (*args != ' ' && *args != '\0' && i < 15) {
            filename[i++] = *args++;
        }
        filename[i] = '\0';
        while (*args == ' ') args++;
        if (filename[0] != '\0' && *args != '\0') {
            fs_write_file(filename, args);
        } else {
            kprint("Usage: write <filename> <content>\n");
        }
    } else if (strncmp(c, "echo ", 5) == 0) {
        kprint(c + 5);
        kprint("\n");
    } else if (strcmp(c, "info") == 0) {
        kprint("MOKernel - Now with Terminal, Shift keys & Mouse support!\n");
    } else {
        kprint("Unknown param or command: ");
        kprint(c);
        kprint("\n");
    }
}

// ==== Keyboard ====

unsigned char keyboard_map[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

unsigned char keyboard_map_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

int shift_pressed = 0;
int ctrl_pressed = 0;
int e0_pressed = 0;

#define HISTORY_MAX 10
char history[HISTORY_MAX][CMD_BUFFER_SIZE];
int history_count = 0;
int history_index = 0;

void cmd_clear_display(void) {
    while (cmd_len > 0) {
        cmd_len--;
        if (current_loc >= 2) {
            current_loc -= 2;
            vidptr[current_loc]     = ' ';
            vidptr[current_loc + 1] = 0x07;
        }
    }
}

void cmd_set(const char *str) {
    cmd_clear_display();
    while (*str) {
        if (cmd_len < CMD_BUFFER_SIZE - 1) {
            cmd_buffer[cmd_len++] = *str;
            char s[2] = {*str, '\0'};
            kprint(s);
        }
        str++;
    }
}

void history_up(void) {
    if (history_count == 0) return;
    if (history_index > 0) {
        history_index--;
        cmd_set(history[history_index]);
    }
}

void history_down(void) {
    if (history_count == 0) return;
    if (history_index < history_count - 1) {
        history_index++;
        cmd_set(history[history_index]);
    } else if (history_index == history_count - 1) {
        history_index++;
        cmd_set("");
    }
}

void backspace_word(void) {
    while (cmd_len > 0 && cmd_buffer[cmd_len - 1] == ' ') {
        cmd_len--;
        if (current_loc >= 2) {
             current_loc -= 2;
             vidptr[current_loc] = ' ';
             vidptr[current_loc + 1] = 0x07;
        }
    }
    while (cmd_len > 0 && cmd_buffer[cmd_len - 1] != ' ') {
        cmd_len--;
        if (current_loc >= 2) {
             current_loc -= 2;
             vidptr[current_loc] = ' ';
             vidptr[current_loc + 1] = 0x07;
        }
    }
}

void keyboard_handler_main(void)
{
        unsigned char status;
        unsigned char keycode;

        write_port(0x20, 0x20); // Send EOI to PIC
        status = read_port(KEYBOARD_STATUS_PORT);

        if (status & 0x01)
        {
                keycode = read_port(KEYBOARD_DATA_PORT);

                if (keycode == 0xE0) {
                    e0_pressed = 1;
                    return;
                }
                
                if (e0_pressed) {
                    e0_pressed = 0;
                    if (keycode & 0x80) return; // Ignore release for extended
                    if (keycode == 0x48) { history_up(); return; }
                    if (keycode == 0x50) { history_down(); return; }
                    return;
                }

                if (keycode == 0x1D) { // Left Ctrl
                    ctrl_pressed = 1;
                    return;
                }
                if (keycode == 0x9D) { // Left Ctrl release
                    ctrl_pressed = 0;
                    return;
                }

                if (keycode == 0x2A || keycode == 0x36) { // Left/Right Shift pressed
                    shift_pressed = 1;
                    return;
                }
                if (keycode == 0xAA || keycode == 0xB6) { // Shift released
                    shift_pressed = 0;
                    return;
                }
                
                if (keycode & 0x80) { // All other key releases ignore
                    return;
                }

                char chr = shift_pressed ? keyboard_map_shift[keycode] : keyboard_map[keycode];

                if (ctrl_pressed) {
                    if (chr == 'w' || chr == 'W') {
                        backspace_word();
                        return;
                    }
                    if (chr == 'p' || chr == 'P') {
                        history_up();
                        return;
                    }
                }

                if (chr == '\n')
                {
                        kprint("\n");
                        cmd_buffer[cmd_len] = '\0';
                        if (cmd_len > 0) {
                            if (history_count < HISTORY_MAX) {
                                int i; for(i=0; i<=cmd_len; i++) history[history_count][i] = cmd_buffer[i];
                                history_count++;
                            } else {
                                for(int j=0; j<HISTORY_MAX-1; j++) {
                                    int k=0; while(history[j+1][k] != '\0') { history[j][k] = history[j+1][k]; k++; }
                                    history[j][k] = '\0';
                                }
                                int i; for(i=0; i<=cmd_len; i++) history[HISTORY_MAX-1][i] = cmd_buffer[i];
                            }
                        }
                        history_index = history_count;
                        execute_command(cmd_buffer);
                        cmd_len = 0;
                        kprint("OS> ");
                }
                else if (chr == '\b')
                {
                        if (cmd_len > 0) {
                                cmd_len--;
                                if (current_loc >= 2) {
                                        current_loc -= 2;
                                        vidptr[current_loc]     = ' ';
                                        vidptr[current_loc + 1] = 0x07;
                                }
                        }
                }
                else if (chr)
                {
                        if (cmd_len < CMD_BUFFER_SIZE - 1) {
                                cmd_buffer[cmd_len++] = chr;
                                char str[2] = {chr, '\0'};
                                kprint(str);
                        }
                }
        }
}

// ==== Timer ====
void timer_handler_main(void)
{
        timer_ticks++;
        write_port(0x20, 0x20);
}

void pit_init(void)
{
        unsigned short divisor = 11931; // ~100 Hz
        write_port(PIT_COMMAND_PORT, 0x36);
        write_port(PIT_CHANNEL_0_PORT, (unsigned char)(divisor & 0xFF));
        write_port(PIT_CHANNEL_0_PORT, (unsigned char)((divisor >> 8) & 0xFF));
}

// ==== Mouse ====
int mouse_cycle = 0;
char mouse_byte[3];
int mouse_x = 40;
int mouse_y = 12;

void mouse_wait(unsigned char a_type)
{
    unsigned int time_out = 100000;
    if (a_type == 0) {
        while (time_out--) {
            if ((read_port(0x64) & 1) == 1) return;
        }
    } else {
        while (time_out--) {
            if ((read_port(0x64) & 2) == 0) return;
        }
    }
}

void mouse_write(unsigned char a_write)
{
    mouse_wait(1);
    write_port(0x64, 0xD4);
    mouse_wait(1);
    write_port(0x60, a_write);
}

unsigned char mouse_read()
{
    mouse_wait(0);
    return read_port(0x60);
}

void mouse_init(void)
{
    unsigned char status;

    // Enable auxiliary device
    mouse_wait(1);
    write_port(0x64, 0xA8);

    // Get Compaq status byte
    mouse_wait(1);
    write_port(0x64, 0x20);
    mouse_wait(0);
    status = (read_port(0x60) | 2);

    // Write back status with IRQ12 enabled
    mouse_wait(1);
    write_port(0x64, 0x60);
    mouse_wait(1);
    write_port(0x60, status);

    // Tell mouse to use default settings
    mouse_write(0xF6);
    mouse_read();

    // Enable mouse data
    mouse_write(0xF4);
    mouse_read();
}

void update_mouse_cursor(int invert) {
    int pos = (mouse_y * 80 + mouse_x) * 2;
    if (invert) vidptr[pos + 1] ^= 0x77; // Flip background/foreground
}

void mouse_handler_main(void)
{
    unsigned char status = read_port(0x64);
    if (!(status & 1)) {
        write_port(0xA0, 0x20);
        write_port(0x20, 0x20);
        return;
    }

    unsigned char in = read_port(0x60);

    // Un-invert current cursor
    update_mouse_cursor(1);

    switch(mouse_cycle)
    {
        case 0:
            if ((in & 0x08) == 0) break; // Mouse sync bit not set
            mouse_byte[0] = in;
            mouse_cycle++;
            break;
        case 1:
            mouse_byte[1] = in;
            mouse_cycle++;
            break;
        case 2:
            mouse_byte[2] = in;
            
            if (mouse_byte[0] & 0x10) mouse_x -= (256 - mouse_byte[1]);
            else mouse_x += mouse_byte[1];

            if (mouse_byte[0] & 0x20) mouse_y += (256 - mouse_byte[2]);
            else mouse_y -= mouse_byte[2];

            if (mouse_x < 0) mouse_x = 0;
            if (mouse_x > 79) mouse_x = 79;
            if (mouse_y < 0) mouse_y = 24;
            if (mouse_y > 24) mouse_y = 24;

            mouse_cycle = 0;
            break;
    }

    // Reinvert cursor at new position
    update_mouse_cursor(1);

    write_port(0xA0, 0x20); // EOI to Slave PIC
    write_port(0x20, 0x20); // EOI to Master PIC
}

// ==== IDT / PIC Initialization ====

void idt_init(void)
{
        unsigned long keyboard_address = (unsigned long)keyboard_handler;
        unsigned long timer_address    = (unsigned long)timer_handler;
        unsigned long mouse_address    = (unsigned long)mouse_handler;
        unsigned long pf_address       = (unsigned long)page_fault_stub;
        unsigned long idt_address      = (unsigned long)IDT;
        unsigned long idt_ptr[2];

        // Timer (IRQ0) -> Int 0x20
        IDT[0x20].offset_lowerbits  = timer_address & 0xFFFF;
        IDT[0x20].selector          = 0x08;
        IDT[0x20].zero              = 0;
        IDT[0x20].type_attr         = 0x8E;
        IDT[0x20].offset_higherbits = (timer_address >> 16) & 0xFFFF;

        // Keyboard (IRQ1) -> Int 0x21
        IDT[0x21].offset_lowerbits  = keyboard_address & 0xFFFF;
        IDT[0x21].selector          = 0x08;
        IDT[0x21].zero              = 0;
        IDT[0x21].type_attr         = 0x8E;
        IDT[0x21].offset_higherbits = (keyboard_address >> 16) & 0xFFFF;

        // Mouse (IRQ12) -> Int 0x2C
        IDT[0x2C].offset_lowerbits  = mouse_address & 0xFFFF;
        IDT[0x2C].selector          = 0x08;
        IDT[0x2C].zero              = 0;
        IDT[0x2C].type_attr         = 0x8E;
        IDT[0x2C].offset_higherbits = (mouse_address >> 16) & 0xFFFF;

        // Page Fault (Int 14) -> Int 0x0E
        IDT[14].offset_lowerbits  = pf_address & 0xFFFF;
        IDT[14].selector          = 0x08;
        IDT[14].zero              = 0;
        IDT[14].type_attr         = 0x8E; // Interrupt Gate
        IDT[14].offset_higherbits = (pf_address >> 16) & 0xFFFF;

        // PIC remapping
        write_port(0x20, 0x11);
        write_port(0xA0, 0x11);
        write_port(0x21, 0x20);
        write_port(0xA1, 0x28);
        write_port(0x21, 0x00);
        write_port(0xA1, 0x00);
        write_port(0x21, 0x01);
        write_port(0xA1, 0x01);
        
        // Unmask IRQ0, IRQ1, IRQ2 on Master
        write_port(0x21, 0xF8); 
        // Unmask IRQ12 on Slave
        write_port(0xA1, 0xEF); 

        idt_ptr[0] = (sizeof(struct IDT_entry) * IDT_SIZE) | ((idt_address & 0xFFFF) << 16);
        idt_ptr[1] = idt_address >> 16;
        load_idt((unsigned long *)idt_ptr);
}

void kmain(void)
{
        // clear_screen();
        write_port(0x3F8, 'A');
        write_port(0x3F8, '\n');
        kprint("Booting MOKernel...\n");

        kprint("Initializing Paging...\n");
        paging_init();

        kprint("Initializing PIT Timer...\n");
        pit_init();

        kprint("Initializing PS/2 Mouse...\n");
        mouse_init();

        kprint("Initializing IDT...\n");
        idt_init();

        kprint("Initializing File System...\n");
        fs_init();

        // Draw initial cursor
        update_mouse_cursor(1);

        kprint("\nKernel initialization complete!\n");
        kprint("OS> ");

        while (1)
        {
                asm volatile("hlt");
        }
}
