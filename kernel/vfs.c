#include <nyx/vfs.h>
#include <nyx/nofs.h>
#include <nyx/console.h>
#include <stdbool.h>

#define VFS_MAX_PATH 64
#define VFS_MAX_DIRS 16

static char cwd[VFS_MAX_PATH];
static char dirs[VFS_MAX_DIRS][VFS_MAX_PATH];
static int dir_count = 0;

static int str_len(const char *s) {
    int n = 0;
    while (*s++) n++;
    return n;
}

static bool str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void str_cpy(char *dst, const char *src, int max) {
    int i;
    for (i = 0; i < max - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static bool starts_with(const char *s, const char *pre) {
    while (*pre) {
        if (*s != *pre) return false;
        s++; pre++;
    }
    return true;
}

static void add_dir(const char *path) {
    if (dir_count >= VFS_MAX_DIRS) return;
    str_cpy(dirs[dir_count], path, VFS_MAX_PATH);
    dir_count++;
}

static bool dir_exists(const char *path) {
    if (str_eq(path, "/")) return true;
    for (int i = 0; i < dir_count; i++) {
        if (str_eq(dirs[i], path)) return true;
    }
    return false;
}

static void resolve_path(const char *base, const char *input, char *out, int max) {
    char temp[VFS_MAX_PATH];
    int ti = 0;

    if (!input || !input[0]) {
        str_cpy(out, base, max);
        return;
    }

    if (str_eq(input, "~")) {
        str_cpy(out, "/home", max);
        return;
    }

    if (input[0] == '/') {
        ti = 0;
    } else {
        for (int i = 0; base[i] && ti < max - 1; i++)
            temp[ti++] = base[i];
    }

    if (ti == 0 || temp[ti - 1] != '/')
        temp[ti++] = '/';

    for (int i = 0; input[i] && ti < max - 1; i++)
        temp[ti++] = input[i];
    temp[ti] = '\0';

    char segs[16][VFS_MAX_PATH];
    int sc = 0;
    char buf[VFS_MAX_PATH];
    int bi = 0;

    for (int i = 0; temp[i]; i++) {
        if (temp[i] == '/') {
            if (bi > 0) {
                buf[bi] = '\0';
                if (str_eq(buf, "..")) {
                    if (sc > 0) sc--;
                } else if (!str_eq(buf, ".")) {
                    str_cpy(segs[sc], buf, VFS_MAX_PATH);
                    sc++;
                }
                bi = 0;
            }
        } else {
            buf[bi++] = temp[i];
        }
    }
    if (bi > 0) {
        buf[bi] = '\0';
        if (str_eq(buf, "..")) {
            if (sc > 0) sc--;
        } else if (!str_eq(buf, ".")) {
            str_cpy(segs[sc], buf, VFS_MAX_PATH);
            sc++;
        }
    }

    ti = 0;
    temp[ti++] = '/';
    for (int i = 0; i < sc; i++) {
        for (int j = 0; segs[i][j] && ti < max - 1; j++)
            temp[ti++] = segs[i][j];
        if (i < sc - 1) temp[ti++] = '/';
    }
    temp[ti] = '\0';

    if (ti > 1 && temp[ti - 1] == '/')
        temp[ti - 1] = '\0';

    str_cpy(out, temp, max);
}

void vfs_init(void) {
    str_cpy(cwd, "/home", VFS_MAX_PATH);
    dir_count = 0;
    add_dir("/");
    add_dir("/home");
    add_dir("/docs");
    add_dir("/downloads");
    add_dir("/desktop");
    console_write("[VFS] initialized: /home, /docs, /downloads, /desktop\n");
}

const char* vfs_get_cwd(void) {
    return cwd;
}

int vfs_cd(const char *path) {
    if (!path || !path[0]) {
        str_cpy(cwd, "/home", VFS_MAX_PATH);
        return 0;
    }
    char target[VFS_MAX_PATH];
    resolve_path(cwd, path, target, VFS_MAX_PATH);

    if (!dir_exists(target)) return -1;

    str_cpy(cwd, target, VFS_MAX_PATH);
    return 0;
}

int vfs_mkdir(const char *path) {
    if (!path || !path[0]) return -1;
    char target[VFS_MAX_PATH];
    resolve_path(cwd, path, target, VFS_MAX_PATH);

    if (dir_exists(target)) return -1;

    add_dir(target);
    return 0;
}

int vfs_ls(char names[][NOFS_MAX_NAME], int max) {
    return vfs_ls_at(cwd, names, max);
}

int vfs_ls_at(const char *path, char names[][NOFS_MAX_NAME], int max) {
    char all[NOFS_MAX_FILES][NOFS_MAX_NAME];
    int count = nofs_list(all, NOFS_MAX_FILES);
    int out = 0;
    int path_len = str_len(path);

    for (int i = 0; i < dir_count && out < max; i++) {
        if (str_eq(dirs[i], "/") || str_eq(dirs[i], path)) continue;

        if (starts_with(dirs[i], path)) {
            const char *rest = dirs[i] + path_len;
            if (*rest == '/') rest++;
            if (!rest[0]) continue;

            bool sub = false;
            for (int j = 0; rest[j]; j++) {
                if (rest[j] == '/') { sub = true; break; }
            }
            if (!sub) {
                int ni = 0;
                while (rest[ni] && ni < NOFS_MAX_NAME - 2) {
                    names[out][ni] = rest[ni];
                    ni++;
                }
                names[out][ni++] = '/';
                names[out][ni] = '\0';
                out++;
            }
        }
    }

    for (int i = 0; i < count && out < max; i++) {
        if (str_eq(path, "/")) {
            bool has_slash = false;
            for (int j = 0; all[i][j]; j++) {
                if (all[i][j] == '/') { has_slash = true; break; }
            }
            if (!has_slash) {
                str_cpy(names[out], all[i], NOFS_MAX_NAME);
                out++;
            }
        } else if (starts_with(all[i], path) && all[i][path_len] == '/') {
            const char *rest = all[i] + path_len + 1;
            bool sub = false;
            for (int j = 0; rest[j]; j++) {
                if (rest[j] == '/') { sub = true; break; }
            }
            if (!sub && rest[0]) {
                str_cpy(names[out], rest, NOFS_MAX_NAME);
                out++;
            }
        }
    }
    return out;
}

static void make_nofs_name(const char *path, char *out, int max) {
    char resolved[VFS_MAX_PATH];
    resolve_path(cwd, path, resolved, VFS_MAX_PATH);

    int ri = 0;
    while (resolved[ri]) ri++;

    if (ri == 1 && resolved[0] == '/') {
        out[0] = '\0';
        return;
    }

    int ti = 0;
    for (int i = 1; i < ri && ti < max - 1; i++)
        out[ti++] = resolved[i];
    out[ti] = '\0';
}

int vfs_create(const char *path) {
    char flat[NOFS_MAX_NAME];
    make_nofs_name(path, flat, NOFS_MAX_NAME);
    if (!flat[0]) return -1;
    char parent[VFS_MAX_PATH];
    int last = -1;
    for (int i = 0; flat[i]; i++) {
        if (flat[i] == '/') last = i;
    }
    if (last > 0) {
        int pi = 0;
        parent[pi++] = '/';
        for (int i = 0; i < last; i++)
            parent[pi++] = flat[i];
        parent[pi] = '\0';
        if (!dir_exists(parent)) return -1;
    }
    return nofs_create(flat);
}

int vfs_open(const char *path) {
    char flat[NOFS_MAX_NAME];
    make_nofs_name(path, flat, NOFS_MAX_NAME);
    if (!flat[0]) return -1;
    return nofs_open(flat);
}

int vfs_read(int fd, uint8_t *buf, uint32_t size) {
    return nofs_read(fd, buf, size);
}

int vfs_write(int fd, const uint8_t *buf, uint32_t size) {
    return nofs_write(fd, buf, size);
}

int vfs_delete(const char *path) {
    char flat[NOFS_MAX_NAME];
    make_nofs_name(path, flat, NOFS_MAX_NAME);
    if (!flat[0]) return -1;
    return nofs_delete(flat);
}

int vfs_get_size(int fd) {
    return nofs_get_size(fd);
}