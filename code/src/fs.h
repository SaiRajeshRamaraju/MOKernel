#ifndef FS_H
#define FS_H

// ============================================================
// MOKernel In-Memory Filesystem
// Supports files AND directories with a current working dir.
// ============================================================

#define FS_MAX_ENTRIES    32          // total inodes (files + dirs)
#define FS_MAX_NAME_LEN   16          // max name length (excl. NUL)
#define FS_MAX_FILE_SIZE  256         // max bytes per file
#define FS_ROOT_IDX       0           // inode index of root "/"
#define FS_NULL_IDX       -1          // sentinel for "no parent"

// inode types
#define FS_TYPE_NONE  0
#define FS_TYPE_FILE  1
#define FS_TYPE_DIR   2

typedef struct {
    char name[FS_MAX_NAME_LEN + 1];  // entry name (not full path)
    char content[FS_MAX_FILE_SIZE];  // file content (dirs ignore this)
    int  size;                       // bytes of content
    int  type;                       // FS_TYPE_FILE | FS_TYPE_DIR
    int  parent;                     // parent inode index (FS_NULL_IDX for root)
} fs_entry_t;

extern fs_entry_t fs_table[FS_MAX_ENTRIES];
extern int        fs_cwd;            // current working directory inode index

// ---- Core API -----------------------------------------------
void fs_init(void);

// File ops
int  fs_create_file(const char *name);       // in cwd
void fs_write_file (const char *name, const char *data);
void fs_read_file  (const char *name);

// Directory ops
int  fs_mkdir      (const char *name);       // in cwd
int  fs_cd         (const char *name);       // change cwd
void fs_pwd        (void);                   // print cwd path
void fs_list_files (void);                   // ls in cwd
int  fs_rm         (const char *name);       // delete file in cwd
int  fs_rmdir      (const char *name);       // delete empty dir in cwd

#endif /* FS_H */
