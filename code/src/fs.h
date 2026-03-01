#ifndef FS_H
#define FS_H

#define MAX_FILES 10
#define MAX_FILENAME_LEN 16
#define MAX_FILE_SIZE 256

void fs_init(void);
int fs_create_file(const char *name);
void fs_list_files(void);
void fs_write_file(const char *name, const char *data);
void fs_read_file(const char *name);

#endif
