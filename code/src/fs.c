#include "fs.h"

extern void kprint(const char *str);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, int n);

typedef struct {
    char name[MAX_FILENAME_LEN];
    char content[MAX_FILE_SIZE];
    int size;
    int in_use;
} file_t;

file_t file_system[MAX_FILES];

void fs_init(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        file_system[i].in_use = 0;
        file_system[i].size = 0;
        for(int j = 0; j < MAX_FILENAME_LEN; j++) file_system[i].name[j] = 0;
        for(int j = 0; j < MAX_FILE_SIZE; j++) file_system[i].content[j] = 0;
    }
}

int fs_create_file(const char *name) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (file_system[i].in_use == 1 && strcmp(file_system[i].name, name) == 0) {
            kprint("Error: File already exists.\n");
            return -1;
        }
    }
    for (int i = 0; i < MAX_FILES; i++) {
        if (file_system[i].in_use == 0) {
            file_system[i].in_use = 1;
            int j = 0;
            while (name[j] != '\0' && j < MAX_FILENAME_LEN - 1) {
                file_system[i].name[j] = name[j];
                j++;
            }
            file_system[i].name[j] = '\0';
            file_system[i].size = 0;
            return i;
        }
    }
    kprint("Error: File system full.\n");
    return -1;
}

void fs_list_files(void) {
    int count = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (file_system[i].in_use) {
            kprint(file_system[i].name);
            kprint("\n");
            count++;
        }
    }
    if (count == 0) {
        kprint("No files found.\n");
    }
}

void fs_write_file(const char *name, const char *data) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (file_system[i].in_use == 1 && strcmp(file_system[i].name, name) == 0) {
            int j = 0;
            while (data[j] != '\0' && j < MAX_FILE_SIZE - 1) {
                file_system[i].content[j] = data[j];
                j++;
            }
            file_system[i].content[j] = '\0';
            file_system[i].size = j;
            return;
        }
    }
    kprint("Error: File not found.\n");
}

void fs_read_file(const char *name) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (file_system[i].in_use == 1 && strcmp(file_system[i].name, name) == 0) {
            kprint(file_system[i].content);
            kprint("\n");
            return;
        }
    }
    kprint("Error: File not found.\n");
}
