// ============================================================
// MOKernel In-Memory Filesystem Implementation
// ============================================================
#include "fs.h"

extern void kprint(const char *str);
extern int  strcmp (const char *s1, const char *s2);
extern int  strncmp(const char *s1, const char *s2, int n);

// --------------- Globals -------------------------------------
fs_entry_t fs_table[FS_MAX_ENTRIES];
int        fs_cwd = FS_ROOT_IDX;

// --------------- Local helpers -------------------------------

static void fs_strncpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

// Find an inode by name inside a given parent directory.
// If parent == FS_NULL_IDX, searches only the root entry.
static int fs_find_in(const char *name, int parent_idx) {
    for (int i = 0; i < FS_MAX_ENTRIES; i++) {
        if (fs_table[i].type   == FS_TYPE_NONE) continue;
        if (fs_table[i].parent != parent_idx)   continue;
        if (strcmp(fs_table[i].name, name) == 0) return i;
    }
    return FS_NULL_IDX;
}

// Allocate a free inode slot.
static int fs_alloc(void) {
    for (int i = 0; i < FS_MAX_ENTRIES; i++) {
        if (fs_table[i].type == FS_TYPE_NONE) return i;
    }
    return FS_NULL_IDX;
}

// Print the full path of an inode recursively.
static void fs_print_path(int idx) {
    if (idx == FS_ROOT_IDX) {
        kprint("/");
        return;
    }
    // Walk up to root, collect indices
    int stack[FS_MAX_ENTRIES];
    int depth = 0;
    int cur   = idx;
    while (cur != FS_ROOT_IDX && cur != FS_NULL_IDX) {
        stack[depth++] = cur;
        cur = fs_table[cur].parent;
    }
    // Print from root down
    for (int i = depth - 1; i >= 0; i--) {
        kprint("/");
        kprint(fs_table[stack[i]].name);
    }
}

// Does `dir_idx` have any children?
static int fs_dir_empty(int dir_idx) {
    for (int i = 0; i < FS_MAX_ENTRIES; i++) {
        if (fs_table[i].type != FS_TYPE_NONE &&
            fs_table[i].parent == dir_idx) return 0;
    }
    return 1;
}

// ---- Resolve a name relative to cwd:
//   ".."  -> parent of cwd
//   "."   -> cwd
//   else  -> child named `name` in cwd
// Returns inode index or FS_NULL_IDX.
static int fs_resolve_dir(const char *name) {
    if (strcmp(name, ".") == 0) return fs_cwd;
    if (strcmp(name, "..") == 0) {
        if (fs_table[fs_cwd].parent == FS_NULL_IDX) return FS_ROOT_IDX;
        return fs_table[fs_cwd].parent;
    }
    return fs_find_in(name, fs_cwd);
}

// ============================================================
// Public API
// ============================================================

void fs_init(void) {
    for (int i = 0; i < FS_MAX_ENTRIES; i++) {
        fs_table[i].type    = FS_TYPE_NONE;
        fs_table[i].size    = 0;
        fs_table[i].parent  = FS_NULL_IDX;
        for (int j = 0; j <= FS_MAX_NAME_LEN; j++) fs_table[i].name[j] = 0;
        for (int j = 0; j <  FS_MAX_FILE_SIZE; j++) fs_table[i].content[j] = 0;
    }

    // Create root directory at index 0
    fs_table[FS_ROOT_IDX].type   = FS_TYPE_DIR;
    fs_table[FS_ROOT_IDX].parent = FS_NULL_IDX;
    fs_table[FS_ROOT_IDX].name[0] = '\0'; // root has empty name — printed as "/"
    fs_cwd = FS_ROOT_IDX;
}

// ---- mkdir --------------------------------------------------
int fs_mkdir(const char *name) {
    // Validate name
    if (!name || name[0] == '\0') { kprint("mkdir: invalid name\n"); return -1; }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        kprint("mkdir: cannot use '.' or '..'\n"); return -1;
    }

    // Duplicate check in cwd
    if (fs_find_in(name, fs_cwd) != FS_NULL_IDX) {
        kprint("mkdir: '"); kprint(name); kprint("' already exists\n");
        return -1;
    }

    int slot = fs_alloc();
    if (slot == FS_NULL_IDX) { kprint("mkdir: filesystem full\n"); return -1; }

    fs_table[slot].type   = FS_TYPE_DIR;
    fs_table[slot].parent = fs_cwd;
    fs_table[slot].size   = 0;
    fs_strncpy(fs_table[slot].name, name, FS_MAX_NAME_LEN);
    return slot;
}

// ---- cd -----------------------------------------------------
int fs_cd(const char *name) {
    if (!name || name[0] == '\0') { kprint("cd: missing argument\n"); return -1; }

    // "/" always goes to root
    if (name[0] == '/' && name[1] == '\0') { fs_cwd = FS_ROOT_IDX; return 0; }

    int target = fs_resolve_dir(name);

    // Handle ".." at root — stay at root
    if (strcmp(name, "..") == 0 && fs_table[fs_cwd].parent == FS_NULL_IDX) {
        fs_cwd = FS_ROOT_IDX;
        return 0;
    }

    if (target == FS_NULL_IDX) {
        kprint("cd: '"); kprint(name); kprint("': no such directory\n");
        return -1;
    }
    if (fs_table[target].type != FS_TYPE_DIR) {
        kprint("cd: '"); kprint(name); kprint("': not a directory\n");
        return -1;
    }
    fs_cwd = target;
    return 0;
}

// ---- pwd ----------------------------------------------------
void fs_pwd(void) {
    fs_print_path(fs_cwd);
    kprint("\n");
}

// ---- ls -----------------------------------------------------
void fs_list_files(void) {
    int count = 0;
    for (int i = 0; i < FS_MAX_ENTRIES; i++) {
        if (fs_table[i].type == FS_TYPE_NONE) continue;
        if (fs_table[i].parent != fs_cwd)     continue;
        // skip root's self-reference
        if (fs_table[i].type == FS_TYPE_DIR) {
            kprint("[DIR]  "); kprint(fs_table[i].name); kprint("\n");
        } else {
            kprint("[FILE] "); kprint(fs_table[i].name); kprint("\n");
        }
        count++;
    }
    if (count == 0) kprint("(empty)\n");
}

// ---- touch --------------------------------------------------
int fs_create_file(const char *name) {
    if (!name || name[0] == '\0') { kprint("touch: invalid name\n"); return -1; }

    if (fs_find_in(name, fs_cwd) != FS_NULL_IDX) {
        kprint("touch: '"); kprint(name); kprint("' already exists\n");
        return -1;
    }

    int slot = fs_alloc();
    if (slot == FS_NULL_IDX) { kprint("touch: filesystem full\n"); return -1; }

    fs_table[slot].type   = FS_TYPE_FILE;
    fs_table[slot].parent = fs_cwd;
    fs_table[slot].size   = 0;
    fs_strncpy(fs_table[slot].name, name, FS_MAX_NAME_LEN);
    return slot;
}

// ---- write --------------------------------------------------
void fs_write_file(const char *name, const char *data) {
    int idx = fs_find_in(name, fs_cwd);
    if (idx == FS_NULL_IDX || fs_table[idx].type != FS_TYPE_FILE) {
        kprint("write: '"); kprint(name); kprint("': no such file\n");
        return;
    }
    int j = 0;
    while (data[j] != '\0' && j < FS_MAX_FILE_SIZE - 1) {
        fs_table[idx].content[j] = data[j]; j++;
    }
    fs_table[idx].content[j] = '\0';
    fs_table[idx].size = j;
}

// ---- cat ----------------------------------------------------
void fs_read_file(const char *name) {
    int idx = fs_find_in(name, fs_cwd);
    if (idx == FS_NULL_IDX || fs_table[idx].type != FS_TYPE_FILE) {
        kprint("cat: '"); kprint(name); kprint("': no such file\n");
        return;
    }
    kprint(fs_table[idx].content);
    kprint("\n");
}

// ---- rm -----------------------------------------------------
int fs_rm(const char *name) {
    int idx = fs_find_in(name, fs_cwd);
    if (idx == FS_NULL_IDX || fs_table[idx].type != FS_TYPE_FILE) {
        kprint("rm: '"); kprint(name); kprint("': no such file\n");
        return -1;
    }
    fs_table[idx].type = FS_TYPE_NONE;
    return 0;
}

// ---- rmdir --------------------------------------------------
int fs_rmdir(const char *name) {
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        kprint("rmdir: cannot remove '.' or '..'\n"); return -1;
    }
    int idx = fs_find_in(name, fs_cwd);
    if (idx == FS_NULL_IDX || fs_table[idx].type != FS_TYPE_DIR) {
        kprint("rmdir: '"); kprint(name); kprint("': no such directory\n");
        return -1;
    }
    if (!fs_dir_empty(idx)) {
        kprint("rmdir: '"); kprint(name); kprint("': directory not empty\n");
        return -1;
    }
    fs_table[idx].type = FS_TYPE_NONE;
    return 0;
}
