/**************************************************************************
 *
 * Copyright (c) 2004-22 Simon Peter
 * Portions Copyright (c) 2007 Alexander Larsson
 * Portions from WjCryptLib_Md5 originally written by Alexander Peslyak,
   modified by WaterJuice retaining Public Domain license
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **************************************************************************/

#ident "AppImage by Simon Peter, https://appimage.org/"

#define _GNU_SOURCE

#include <stddef.h>

#include <squashfuse/ll.h>

/* Print a usage string */
void sqfs_usage(char *progname, bool fuse_usage);

/* Parse command-line arguments */
typedef struct {
	char *progname;
	const char *image;
	int mountpoint;
	size_t offset;
	unsigned int idle_timeout_secs;
} sqfs_opts;

extern dev_t sqfs_makedev(int maj, int min);

extern int sqfs_opt_proc(void* data, const char* arg, int key, struct fuse_args* outargs);

#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/wait.h>
#include <fnmatch.h>
#include <sys/mman.h>

#include <stdint.h>

typedef struct {
    uint32_t lo;
    uint32_t hi;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint8_t buffer[64];
    uint32_t block[16];
} Md5Context;

#define MD5_HASH_SIZE (128 / 8)

typedef struct {
    uint8_t bytes[MD5_HASH_SIZE];
} MD5_HASH;

typedef uint16_t Elf32_Half;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf32_Word;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;
typedef uint32_t Elf32_Addr;
typedef uint64_t Elf64_Addr;
typedef uint32_t Elf32_Off;
typedef uint64_t Elf64_Off;

#define EI_NIDENT 16

typedef struct elf32_hdr {
    unsigned char e_ident[EI_NIDENT];
    Elf32_Half e_type;
    Elf32_Half e_machine;
    Elf32_Word e_version;
    Elf32_Addr e_entry; /* Entry point */
    Elf32_Off e_phoff;
    Elf32_Off e_shoff;
    Elf32_Word e_flags;
    Elf32_Half e_ehsize;
    Elf32_Half e_phentsize;
    Elf32_Half e_phnum;
    Elf32_Half e_shentsize;
    Elf32_Half e_shnum;
    Elf32_Half e_shstrndx;
} Elf32_Ehdr;

typedef struct elf64_hdr {
    unsigned char e_ident[EI_NIDENT]; /* ELF "magic number" */
    Elf64_Half e_type;
    Elf64_Half e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry; /* Entry point virtual address */
    Elf64_Off e_phoff; /* Program header table file offset */
    Elf64_Off e_shoff; /* Section header table file offset */
    Elf64_Word e_flags;
    Elf64_Half e_ehsize;
    Elf64_Half e_phentsize;
    Elf64_Half e_phnum;
    Elf64_Half e_shentsize;
    Elf64_Half e_shnum;
    Elf64_Half e_shstrndx;
} Elf64_Ehdr;

typedef struct elf32_shdr {
    Elf32_Word sh_name;
    Elf32_Word sh_type;
    Elf32_Word sh_flags;
    Elf32_Addr sh_addr;
    Elf32_Off sh_offset;
    Elf32_Word sh_size;
    Elf32_Word sh_link;
    Elf32_Word sh_info;
    Elf32_Word sh_addralign;
    Elf32_Word sh_entsize;
} Elf32_Shdr;

typedef struct elf64_shdr {
    Elf64_Word sh_name; /* Section name, index in string tbl */
    Elf64_Word sh_type; /* Type of section */
    Elf64_Xword sh_flags; /* Miscellaneous section attributes */
    Elf64_Addr sh_addr; /* Section virtual addr at execution */
    Elf64_Off sh_offset; /* Section file offset */
    Elf64_Xword sh_size; /* Size of section in bytes */
    Elf64_Word sh_link; /* Index of another section */
    Elf64_Word sh_info; /* Additional section information */
    Elf64_Xword sh_addralign; /* Section alignment */
    Elf64_Xword sh_entsize; /* Entry size if section holds table */
} Elf64_Shdr;

/* Note header in a PT_NOTE section */
typedef struct elf32_note {
    Elf32_Word n_namesz; /* Name size */
    Elf32_Word n_descsz; /* Content size */
    Elf32_Word n_type; /* Content type */
} Elf32_Nhdr;

#define ELFCLASS32  1
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2
#define ELFCLASS64  2
#define EI_CLASS    4
#define EI_DATA     5

#define bswap_16(value) \
((((value) & 0xff) << 8) | ((value) >> 8))

#define bswap_32(value) \
(((uint32_t)bswap_16((uint16_t)((value) & 0xffff)) << 16) | \
(uint32_t)bswap_16((uint16_t)((value) >> 16)))

#define bswap_64(value) \
(((uint64_t)bswap_32((uint32_t)((value) & 0xffffffff)) \
<< 32) | \
(uint64_t)bswap_32((uint32_t)((value) >> 32)))

typedef Elf32_Nhdr Elf_Nhdr;

static char* fname;
static Elf64_Ehdr ehdr;

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define ELFDATANATIVE ELFDATA2LSB
#elif __BYTE_ORDER == __BIG_ENDIAN
#define ELFDATANATIVE ELFDATA2MSB
#else
#error "Unknown machine endian"
#endif

static uint16_t file16_to_cpu(uint16_t val) {
    if (ehdr.e_ident[EI_DATA] != ELFDATANATIVE)
        val = bswap_16(val);
    return val;
}

static uint32_t file32_to_cpu(uint32_t val) {
    if (ehdr.e_ident[EI_DATA] != ELFDATANATIVE)
        val = bswap_32(val);
    return val;
}

static uint64_t file64_to_cpu(uint64_t val) {
    if (ehdr.e_ident[EI_DATA] != ELFDATANATIVE)
        val = bswap_64(val);
    return val;
}

static off_t read_elf32(FILE* fd) {
    Elf32_Ehdr ehdr32;
    Elf32_Shdr shdr32;
    off_t last_shdr_offset;
    ssize_t ret;
    off_t sht_end, last_section_end;

    fseeko(fd, 0, SEEK_SET);
    ret = fread(&ehdr32, 1, sizeof(ehdr32), fd);
    if (ret < 0 || (size_t) ret != sizeof(ehdr32)) {
        fprintf(stderr, "Read of ELF header from %s failed: %s\n",
                fname, strerror(errno));
        return -1;
    }

    ehdr.e_shoff = file32_to_cpu(ehdr32.e_shoff);
    ehdr.e_shentsize = file16_to_cpu(ehdr32.e_shentsize);
    ehdr.e_shnum = file16_to_cpu(ehdr32.e_shnum);

    last_shdr_offset = ehdr.e_shoff + (ehdr.e_shentsize * (ehdr.e_shnum - 1));
    fseeko(fd, last_shdr_offset, SEEK_SET);
    ret = fread(&shdr32, 1, sizeof(shdr32), fd);
    if (ret < 0 || (size_t) ret != sizeof(shdr32)) {
        fprintf(stderr, "Read of ELF section header from %s failed: %s\n",
                fname, strerror(errno));
        return -1;
    }

    /* ELF ends either with the table of section headers (SHT) or with a section. */
    sht_end = ehdr.e_shoff + (ehdr.e_shentsize * ehdr.e_shnum);
    last_section_end = file64_to_cpu(shdr32.sh_offset) + file64_to_cpu(shdr32.sh_size);
    return sht_end > last_section_end ? sht_end : last_section_end;
}

static off_t read_elf64(FILE* fd) {
    Elf64_Ehdr ehdr64;
    Elf64_Shdr shdr64;
    off_t last_shdr_offset;
    off_t ret;
    off_t sht_end, last_section_end;

    fseeko(fd, 0, SEEK_SET);
    ret = fread(&ehdr64, 1, sizeof(ehdr64), fd);
    if (ret < 0 || (size_t) ret != sizeof(ehdr64)) {
        fprintf(stderr, "Read of ELF header from %s failed: %s\n",
                fname, strerror(errno));
        return -1;
    }

    ehdr.e_shoff = file64_to_cpu(ehdr64.e_shoff);
    ehdr.e_shentsize = file16_to_cpu(ehdr64.e_shentsize);
    ehdr.e_shnum = file16_to_cpu(ehdr64.e_shnum);

    last_shdr_offset = ehdr.e_shoff + (ehdr.e_shentsize * (ehdr.e_shnum - 1));
    fseeko(fd, last_shdr_offset, SEEK_SET);
    ret = fread(&shdr64, 1, sizeof(shdr64), fd);
    if (ret < 0 || ret != sizeof(shdr64)) {
        fprintf(stderr, "Read of ELF section header from %s failed: %s\n",
                fname, strerror(errno));
        return -1;
    }

    /* ELF ends either with the table of section headers (SHT) or with a section. */
    sht_end = ehdr.e_shoff + (ehdr.e_shentsize * ehdr.e_shnum);
    last_section_end = file64_to_cpu(shdr64.sh_offset) + file64_to_cpu(shdr64.sh_size);
    return sht_end > last_section_end ? sht_end : last_section_end;
}

ssize_t appimage_get_elf_size(const char* fname) {
    off_t ret;
    FILE* fd = NULL;
    off_t size = -1;

    fd = fopen(fname, "rb");
    if (fd == NULL) {
        fprintf(stderr, "Cannot open %s: %s\n",
                fname, strerror(errno));
        return -1;
    }
    ret = fread(ehdr.e_ident, 1, EI_NIDENT, fd);
    if (ret != EI_NIDENT) {
        fprintf(stderr, "Read of e_ident from %s failed: %s\n",
                fname, strerror(errno));
        return -1;
    }
    if ((ehdr.e_ident[EI_DATA] != ELFDATA2LSB) &&
        (ehdr.e_ident[EI_DATA] != ELFDATA2MSB)) {
        fprintf(stderr, "Unknown ELF data order %u\n",
                ehdr.e_ident[EI_DATA]);
        return -1;
    }
    if (ehdr.e_ident[EI_CLASS] == ELFCLASS32) {
        size = read_elf32(fd);
    } else if (ehdr.e_ident[EI_CLASS] == ELFCLASS64) {
        size = read_elf64(fd);
    } else {
        fprintf(stderr, "Unknown ELF class %u\n", ehdr.e_ident[EI_CLASS]);
        return -1;
    }

    fclose(fd);
    return size;
}

/* Return the offset, and the length of an ELF section with a given name in a given ELF file */
char* read_file_offset_length(const char* fname, unsigned long offset, unsigned long length) {
    FILE* f;
    if ((f = fopen(fname, "r")) == NULL) {
        return NULL;
    }

    fseek(f, offset, SEEK_SET);

    char* buffer = calloc(length + 1, sizeof(char));
    fread(buffer, length, sizeof(char), f);

    fclose(f);

    return buffer;
}


/* Exit status to use when launching an AppImage fails.
 * For applications that assign meanings to exit status codes (e.g. rsync),
 * we avoid "cluttering" pre-defined exit status codes by using 127 which
 * is known to alias an application exit status and also known as launcher
 * error, see SYSTEM(3POSIX).
 */
#define EXIT_EXECERROR  127     /* Execution error exit status.  */

struct stat st;

static ssize_t fs_offset; // The offset at which a filesystem image is expected = end of this ELF

static void die(const char* msg) {
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_EXECERROR);
}

/* Check whether directory is writable */
bool is_writable_directory(char* str) {
    if (access(str, W_OK) == 0) {
        return true;
    } else {
        return false;
    }
}

bool startsWith(const char* pre, const char* str) {
    size_t lenpre = strlen(pre),
            lenstr = strlen(str);
    return lenstr < lenpre ? false : strncmp(pre, str, lenpre) == 0;
}

/* Fill in a stat structure. Does not set st_ino */
sqfs_err private_sqfs_stat(sqfs* fs, sqfs_inode* inode, struct stat* st) {
    sqfs_err err = SQFS_OK;
    uid_t id;

    memset(st, 0, sizeof(*st));
    st->st_mode = inode->base.mode;
    st->st_nlink = inode->nlink;
    st->st_mtime = st->st_ctime = st->st_atime = inode->base.mtime;

    if (S_ISREG(st->st_mode)) {
        /* FIXME: do symlinks, dirs, etc have a size? */
        st->st_size = inode->xtra.reg.file_size;
        st->st_blocks = st->st_size / 512;
    } else if (S_ISBLK(st->st_mode) || S_ISCHR(st->st_mode)) {
        st->st_rdev = sqfs_makedev(inode->xtra.dev.major,
                                   inode->xtra.dev.minor);
    } else if (S_ISLNK(st->st_mode)) {
        st->st_size = inode->xtra.symlink_size;
    }

    st->st_blksize = fs->sb.block_size; /* seriously? */

    err = sqfs_id_get(fs, inode->base.uid, &id);
    if (err)
        return err;
    st->st_uid = id;
    err = sqfs_id_get(fs, inode->base.guid, &id);
    st->st_gid = id;
    if (err)
        return err;

    return SQFS_OK;
}

/* ================= End ELF parsing */

extern int fusefs_main(int argc, char* argv[], void (* mounted)(void));
// extern void ext2_quit(void);

static pid_t fuse_pid;
static int keepalive_pipe[2];

static void*
write_pipe_thread(void* arg) {
    char c[32];
    int res;
    //  sprintf(stderr, "Called write_pipe_thread");
    memset(c, 'x', sizeof(c));
    while (1) {
        /* Write until we block, on broken pipe, exit */
        res = write(keepalive_pipe[1], c, sizeof(c));
        if (res == -1) {
            kill(fuse_pid, SIGTERM);
            break;
        }
    }
    return NULL;
}

void
fuse_mounted(void) {
    pthread_t thread;
    fuse_pid = getpid();
    pthread_create(&thread, NULL, write_pipe_thread, keepalive_pipe);
}

char* getArg(int argc, char* argv[], char chr) {
    int i;
    for (i = 1; i < argc; ++i)
        if ((argv[i][0] == '-') && (argv[i][1] == chr))
            return &(argv[i][2]);
    return NULL;
}

/* mkdir -p implemented in C, needed for https://github.com/AppImage/AppImageKit/issues/333
 * https://gist.github.com/JonathonReinhart/8c0d90191c38af2dcadb102c4e202950 */
int
mkdir_p(const char* const path) {
    /* Adapted from http://stackoverflow.com/a/2336245/119527 */
    const size_t len = strlen(path);
    char _path[PATH_MAX];
    char* p;

    errno = 0;

    /* Copy string so its mutable */
    if (len > sizeof(_path) - 1) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(_path, path);

    /* Iterate the string */
    for (p = _path + 1; *p; p++) {
        if (*p == '/') {
            /* Temporarily truncate */
            *p = '\0';

            if (mkdir(_path, 0755) != 0) {
                if (errno != EEXIST)
                    return -1;
            }

            *p = '/';
        }
    }

    if (mkdir(_path, 0755) != 0) {
        if (errno != EEXIST)
            return -1;
    }

    return 0;
}

void print_help(const char* appimage_path) {
    // TODO: "--appimage-list                 List content from embedded filesystem image\n"
    fprintf(stderr,
            "AppImage options:\n\n"
            "  --appimage-extract [<pattern>]  Extract content from embedded filesystem image\n"
            "                                  If pattern is passed, only extract matching files\n"
            "  --appimage-help                 Print this help\n"
            "  --appimage-mount                Mount embedded filesystem image and print\n"
            "                                  mount point and wait for kill with Ctrl-C\n"
            "  --appimage-offset               Print byte offset to start of embedded\n"
            "                                  filesystem image\n"
            "  --appimage-portable-home        Create a portable home folder to use as $HOME\n"
            "  --appimage-portable-config      Create a portable config folder to use as\n"
            "                                  $XDG_CONFIG_HOME\n"
            "  --appimage-signature            Print digital signature embedded in AppImage\n"
            "  --appimage-updateinfo[rmation]  Print update info embedded in AppImage\n"
            "  --appimage-version              Print version of AppImageKit\n"
            "\n"
            "Portable home:\n"
            "\n"
            "  If you would like the application contained inside this AppImage to store its\n"
            "  data alongside this AppImage rather than in your home directory, then you can\n"
            "  place a directory named\n"
            "\n"
            "  %s.home\n"
            "\n"
            "  Or you can invoke this AppImage with the --appimage-portable-home option,\n"
            "  which will create this directory for you. As long as the directory exists\n"
            "  and is neither moved nor renamed, the application contained inside this\n"
            "  AppImage to store its data in this directory rather than in your home\n"
            "  directory\n"
            "\n"
            "License:\n"
            "  This executable contains code from\n"
            "  * runtime, licensed under the terms of\n"
            "    https://github.com/probonopd/static-tools/blob/master/LICENSE\n"
            "  * musl libc, licensed under the terms of\n"
            "    https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT\n"
            "  * libfuse, licensed under the terms of\n"
            "    https://github.com/libfuse/libfuse/blob/master/LGPL2.txt\n"
            "  * squashfuse, licensed under the terms of\n"
            "    https://github.com/vasi/squashfuse/blob/master/LICENSE\n"
            "  * libzstd, licensed under the terms of\n"
            "    https://github.com/facebook/zstd/blob/dev/LICENSE\n"
            "Please see https://github.com/probonopd/static-tools/\n"
            "for information on how to obtain and build the source code\n", appimage_path);
}

void portable_option(const char* arg, const char* appimage_path, const char* name) {
    char option[32];
    sprintf(option, "appimage-portable-%s", name);

    if (arg && strcmp(arg, option) == 0) {
        char portable_dir[PATH_MAX];
        char fullpath[PATH_MAX];

        ssize_t length = readlink(appimage_path, fullpath, sizeof(fullpath));
        if (length < 0) {
            fprintf(stderr, "Error getting realpath for %s\n", appimage_path);
            exit(EXIT_FAILURE);
        }
        fullpath[length] = '\0';

        sprintf(portable_dir, "%s.%s", fullpath, name);
        if (!mkdir(portable_dir, S_IRWXU))
            fprintf(stderr, "Portable %s directory created at %s\n", name, portable_dir);
        else
            fprintf(stderr, "Error creating portable %s directory at %s: %s\n", name, portable_dir, strerror(errno));

        exit(0);
    }
}

bool extract_appimage(const char* const appimage_path, const char* const _prefix, const char* const _pattern,
                      const bool overwrite, const bool verbose) {
    sqfs_err err = SQFS_OK;
    sqfs_traverse trv;
    sqfs fs;
    char prefixed_path_to_extract[1024];

    // local copy we can modify safely
    // allocate 1 more byte than we would need so we can add a trailing slash if there is none yet
    char* prefix = malloc(strlen(_prefix) + 2);
    strcpy(prefix, _prefix);

    // sanitize prefix
    if (prefix[strlen(prefix) - 1] != '/')
        strcat(prefix, "/");

    if (access(prefix, F_OK) == -1) {
        if (mkdir_p(prefix) == -1) {
            perror("mkdir_p error");
            return false;
        }
    }

    if ((err = sqfs_open_image(&fs, appimage_path, (size_t) fs_offset))) {
        fprintf(stderr, "Failed to open squashfs image\n");
        return false;
    };

    // track duplicate inodes for hardlinks
    char** created_inode = calloc(fs.sb.inodes, sizeof(char*));
    if (created_inode == NULL) {
        fprintf(stderr, "Failed allocating memory to track hardlinks\n");
        return false;
    }

    if ((err = sqfs_traverse_open(&trv, &fs, sqfs_inode_root(&fs)))) {
        fprintf(stderr, "sqfs_traverse_open error\n");
        free(created_inode);
        return false;
    }

    bool rv = true;

    while (sqfs_traverse_next(&trv, &err)) {
        if (!trv.dir_end) {
            if (_pattern == NULL || fnmatch(_pattern, trv.path, FNM_FILE_NAME | FNM_LEADING_DIR) == 0) {
                // fprintf(stderr, "trv.path: %s\n", trv.path);
                // fprintf(stderr, "sqfs_inode_id: %lu\n", trv.entry.inode);
                sqfs_inode inode;
                if (sqfs_inode_get(&fs, &inode, trv.entry.inode)) {
                    fprintf(stderr, "sqfs_inode_get error\n");
                    rv = false;
                    break;
                }
                // fprintf(stderr, "inode.base.inode_type: %i\n", inode.base.inode_type);
                // fprintf(stderr, "inode.xtra.reg.file_size: %lu\n", inode.xtra.reg.file_size);
                strcpy(prefixed_path_to_extract, "");
                strcat(strcat(prefixed_path_to_extract, prefix), trv.path);

                if (verbose)
                    fprintf(stdout, "%s\n", prefixed_path_to_extract);

                if (inode.base.inode_type == SQUASHFS_DIR_TYPE || inode.base.inode_type == SQUASHFS_LDIR_TYPE) {
                    // fprintf(stderr, "inode.xtra.dir.parent_inode: %ui\n", inode.xtra.dir.parent_inode);
                    // fprintf(stderr, "mkdir_p: %s/\n", prefixed_path_to_extract);
                    if (access(prefixed_path_to_extract, F_OK) == -1) {
                        if (mkdir_p(prefixed_path_to_extract) == -1) {
                            perror("mkdir_p error");
                            rv = false;
                            break;
                        }
                    }
                } else if (inode.base.inode_type == SQUASHFS_REG_TYPE || inode.base.inode_type == SQUASHFS_LREG_TYPE) {
                    // if we've already created this inode, then this is a hardlink
                    char* existing_path_for_inode = created_inode[inode.base.inode_number - 1];
                    if (existing_path_for_inode != NULL) {
                        unlink(prefixed_path_to_extract);
                        if (link(existing_path_for_inode, prefixed_path_to_extract) == -1) {
                            fprintf(stderr, "Couldn't create hardlink from \"%s\" to \"%s\": %s\n",
                                    prefixed_path_to_extract, existing_path_for_inode, strerror(errno));
                            rv = false;
                            break;
                        } else {
                            continue;
                        }
                    } else {
                        struct stat st;
                        if (!overwrite && stat(prefixed_path_to_extract, &st) == 0 &&
                            st.st_size == inode.xtra.reg.file_size) {
                            // fprintf(stderr, "File exists and file size matches, skipping\n");
                            continue;
                        }

                        // track the path we extract to for this inode, so that we can `link` if this inode is found again
                        created_inode[inode.base.inode_number - 1] = strdup(prefixed_path_to_extract);
                        // fprintf(stderr, "Extract to: %s\n", prefixed_path_to_extract);
                        if (private_sqfs_stat(&fs, &inode, &st) != 0)
                            die("private_sqfs_stat error");

                        // create parent dir
                        char* p = strrchr(prefixed_path_to_extract, '/');
                        if (p) {
                            // set an \0 to end the split the string
                            *p = '\0';
                            mkdir_p(prefixed_path_to_extract);

                            // restore dir seprator
                            *p = '/';
                        }

                        // Read the file in chunks
                        off_t bytes_already_read = 0;
                        sqfs_off_t bytes_at_a_time = 64 * 1024;
                        FILE* f;
                        f = fopen(prefixed_path_to_extract, "w+");
                        if (f == NULL) {
                            perror("fopen error");
                            rv = false;
                            break;
                        }
                        while (bytes_already_read < inode.xtra.reg.file_size) {
                            char buf[bytes_at_a_time];
                            if (sqfs_read_range(&fs, &inode, (sqfs_off_t) bytes_already_read, &bytes_at_a_time, buf)) {
                                perror("sqfs_read_range error");
                                rv = false;
                                break;
                            }
                            // fwrite(buf, 1, bytes_at_a_time, stdout);
                            fwrite(buf, 1, bytes_at_a_time, f);
                            bytes_already_read = bytes_already_read + bytes_at_a_time;
                        }
                        fclose(f);
                        chmod(prefixed_path_to_extract, st.st_mode);
                        if (!rv)
                            break;
                    }
                } else if (inode.base.inode_type == SQUASHFS_SYMLINK_TYPE ||
                           inode.base.inode_type == SQUASHFS_LSYMLINK_TYPE) {
                    size_t size;
                    sqfs_readlink(&fs, &inode, NULL, &size);
                    char buf[size];
                    int ret = sqfs_readlink(&fs, &inode, buf, &size);
                    if (ret != 0) {
                        perror("symlink error");
                        rv = false;
                        break;
                    }
                    // fprintf(stderr, "Symlink: %s to %s \n", prefixed_path_to_extract, buf);
                    unlink(prefixed_path_to_extract);
                    ret = symlink(buf, prefixed_path_to_extract);
                    if (ret != 0)
                        fprintf(stderr, "WARNING: could not create symlink\n");
                } else {
                    fprintf(stderr, "TODO: Implement inode.base.inode_type %i\n", inode.base.inode_type);
                }
                // fprintf(stderr, "\n");

                if (!rv)
                    break;
            }
        }
    }
    for (int i = 0; i < fs.sb.inodes; i++) {
        free(created_inode[i]);
    }
    free(created_inode);

    if (err != SQFS_OK) {
        fprintf(stderr, "sqfs_traverse_next error\n");
        rv = false;
    }
    sqfs_traverse_close(&trv);
    sqfs_fd_close(fs.fd);

    return rv;
}

int rm_recursive_callback(const char* path, const struct stat* stat, const int type, struct FTW* ftw) {
    (void) stat;
    (void) ftw;

    switch (type) {
        case FTW_NS:
        case FTW_DNR:
            fprintf(stderr, "%s: ftw error: %s\n",
                    path, strerror(errno));
            return 1;

        case FTW_D:
            // ignore directories at first, will be handled by FTW_DP
            break;

        case FTW_F:
        case FTW_SL:
        case FTW_SLN:
            if (remove(path) != 0) {
                fprintf(stderr, "Failed to remove %s: %s\n", path, strerror(errno));
                return false;
            }
            break;


        case FTW_DP:
            if (rmdir(path) != 0) {
                fprintf(stderr, "Failed to remove directory %s: %s\n", path, strerror(errno));
                return false;
            }
            break;

        default:
            fprintf(stderr, "Unexpected fts_info\n");
            return 1;
    }

    return 0;
};

bool rm_recursive(const char* const path) {
    // FTW_DEPTH: perform depth-first search to make sure files are deleted before the containing directories
    // FTW_MOUNT: prevent deletion of files on other mounted filesystems
    // FTW_PHYS: do not follow symlinks, but report symlinks as such; this way, the symlink targets, which might point
    //           to locations outside path will not be deleted accidentally (attackers might abuse this)
    int rv = nftw(path, &rm_recursive_callback, 0, FTW_DEPTH | FTW_MOUNT | FTW_PHYS);

    return rv == 0;
}

void build_mount_point(char* mount_dir, const char* const argv0, char const* const temp_base, const size_t templen) {
    const size_t maxnamelen = 6;

    char* path_basename;
    path_basename = basename(argv0);

    size_t namelen = strlen(path_basename);
    // limit length of tempdir name
    if (namelen > maxnamelen) {
        namelen = maxnamelen;
    }

    strcpy(mount_dir, temp_base);
    strncpy(mount_dir + templen, "/.mount_", 8);
    strncpy(mount_dir + templen + 8, path_basename, namelen);
    strncpy(mount_dir + templen + 8 + namelen, "XXXXXX", 6);
    mount_dir[templen + 8 + namelen + 6] = 0; // null terminate destination
}

int fusefs_main(int argc, char* argv[], void (* mounted)(void)) {
    struct fuse_args args;
    sqfs_opts opts;

#if FUSE_USE_VERSION >= 30
    struct fuse_cmdline_opts fuse_cmdline_opts;
#else
    struct {
        char *mountpoint;
        int mt, foreground;
    } fuse_cmdline_opts;
#endif

    int err;
    sqfs_ll* ll;
    struct fuse_opt fuse_opts[] = {
            {"offset=%zu", offsetof(sqfs_opts, offset), 0},
            {"timeout=%u", offsetof(sqfs_opts, idle_timeout_secs), 0},
            FUSE_OPT_END
    };

    struct fuse_lowlevel_ops sqfs_ll_ops;
    memset(&sqfs_ll_ops, 0, sizeof(sqfs_ll_ops));
    sqfs_ll_ops.getattr = sqfs_ll_op_getattr;
    sqfs_ll_ops.opendir = sqfs_ll_op_opendir;
    sqfs_ll_ops.releasedir = sqfs_ll_op_releasedir;
    sqfs_ll_ops.readdir = sqfs_ll_op_readdir;
    sqfs_ll_ops.lookup = sqfs_ll_op_lookup;
    sqfs_ll_ops.open = sqfs_ll_op_open;
    sqfs_ll_ops.create = sqfs_ll_op_create;
    sqfs_ll_ops.release = sqfs_ll_op_release;
    sqfs_ll_ops.read = sqfs_ll_op_read;
    sqfs_ll_ops.readlink = sqfs_ll_op_readlink;
    sqfs_ll_ops.listxattr = sqfs_ll_op_listxattr;
    sqfs_ll_ops.getxattr = sqfs_ll_op_getxattr;
    sqfs_ll_ops.forget = sqfs_ll_op_forget;
    sqfs_ll_ops.statfs = stfs_ll_op_statfs;

    /* PARSE ARGS */
    args.argc = argc;
    args.argv = argv;
    args.allocated = 0;

    opts.progname = argv[0];
    opts.image = NULL;
    opts.mountpoint = 0;
    opts.offset = 0;
    opts.idle_timeout_secs = 0;
    if (fuse_opt_parse(&args, &opts, fuse_opts, sqfs_opt_proc) == -1)
        sqfs_usage(argv[0], true);

#if FUSE_USE_VERSION >= 30
    if (fuse_parse_cmdline(&args, &fuse_cmdline_opts) != 0)
#else
        if (fuse_parse_cmdline(&args,
                               &fuse_cmdline_opts.mountpoint,
                               &fuse_cmdline_opts.mt,
                               &fuse_cmdline_opts.foreground) == -1)
#endif
        sqfs_usage(argv[0], true);
    if (fuse_cmdline_opts.mountpoint == NULL)
        sqfs_usage(argv[0], true);

    /* fuse_daemonize() will unconditionally clobber fds 0-2.
     *
     * If we get one of these file descriptors in sqfs_ll_open,
     * we're going to have a bad time. Just make sure that all
     * these fds are open before opening the image file, that way
     * we must get a different fd.
     */
    while (true) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd == -1) {
            /* Can't open /dev/null, how bizarre! However,
             * fuse_deamonize won't clobber fds if it can't
             * open /dev/null either, so we ought to be OK.
             */
            break;
        }
        if (fd > 2) {
            /* fds 0-2 are now guaranteed to be open. */
            close(fd);
            break;
        }
    }

    /* OPEN FS */
    err = !(ll = sqfs_ll_open(opts.image, opts.offset));

    /* STARTUP FUSE */
    if (!err) {
        sqfs_ll_chan ch;
        err = -1;
        if (sqfs_ll_mount(
                &ch,
                fuse_cmdline_opts.mountpoint,
                &args,
                &sqfs_ll_ops,
                sizeof(sqfs_ll_ops),
                ll) == SQFS_OK) {
            if (sqfs_ll_daemonize(fuse_cmdline_opts.foreground) != -1) {
                if (fuse_set_signal_handlers(ch.session) != -1) {
                    if (opts.idle_timeout_secs) {
                        setup_idle_timeout(ch.session, opts.idle_timeout_secs);
                    }
                    if (mounted)
                        mounted();
                    /* FIXME: multithreading */
                    err = fuse_session_loop(ch.session);
                    teardown_idle_timeout();
                    fuse_remove_signal_handlers(ch.session);
                }
            }
            sqfs_ll_destroy(ll);
            sqfs_ll_unmount(&ch, fuse_cmdline_opts.mountpoint);
        }
    }
    fuse_opt_free_args(&args);
    if (mounted)
        rmdir(fuse_cmdline_opts.mountpoint);
    free(ll);
    free(fuse_cmdline_opts.mountpoint);

    return -err;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//  WjCryptLib_Md5
//
//  Implementation of MD5 hash function. Originally written by Alexander Peslyak. Modified by WaterJuice retaining
//  Public Domain license.
//
//  This is free and unencumbered software released into the public domain - June 2013 waterjuice.org
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//  INTERNAL FUNCTIONS

//  F, G, H, I
//
//  The basic MD5 functions. F and G are optimised compared to their RFC 1321 definitions for architectures that lack
//  an AND-NOT instruction, just like in Colin Plumb's implementation.
#define F(x, y, z)            ( (z) ^ ((x) & ((y) ^ (z))) )
#define G(x, y, z)            ( (y) ^ ((z) & ((x) ^ (y))) )
#define H(x, y, z)            ( (x) ^ (y) ^ (z) )
#define I(x, y, z)            ( (y) ^ ((x) | ~(z)) )

//  STEP
//
//  The MD5 transformation for all four rounds.
#define STEP(f, a, b, c, d, x, t, s)                          \
(a) += f((b), (c), (d)) + (x) + (t);                        \
(a) = (((a) << (s)) | (((a) & 0xffffffff) >> (32 - (s))));  \
(a) += (b);

//  TransformFunction
//
//  This processes one or more 64-byte data blocks, but does NOT update the bit counters. There are no alignment
//  requirements.
static
void*
TransformFunction
        (
                Md5Context* ctx,
                void const* data,
                uintmax_t size
        ) {
    uint8_t* ptr;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t saved_a;
    uint32_t saved_b;
    uint32_t saved_c;
    uint32_t saved_d;

#define GET(n) (ctx->block[(n)])
#define SET(n) (ctx->block[(n)] =             \
    ((uint32_t)ptr[(n)*4 + 0] << 0 )      \
    |   ((uint32_t)ptr[(n)*4 + 1] << 8 )      \
    |   ((uint32_t)ptr[(n)*4 + 2] << 16)      \
    |   ((uint32_t)ptr[(n)*4 + 3] << 24) )

    ptr = (uint8_t*) data;

    a = ctx->a;
    b = ctx->b;
    c = ctx->c;
    d = ctx->d;

    do {
        saved_a = a;
        saved_b = b;
        saved_c = c;
        saved_d = d;

        // Round 1
        STEP(F, a, b, c, d, SET(0), 0xd76aa478, 7)
        STEP(F, d, a, b, c, SET(1), 0xe8c7b756, 12)
        STEP(F, c, d, a, b, SET(2), 0x242070db, 17)
        STEP(F, b, c, d, a, SET(3), 0xc1bdceee, 22)
        STEP(F, a, b, c, d, SET(4), 0xf57c0faf, 7)
        STEP(F, d, a, b, c, SET(5), 0x4787c62a, 12)
        STEP(F, c, d, a, b, SET(6), 0xa8304613, 17)
        STEP(F, b, c, d, a, SET(7), 0xfd469501, 22)
        STEP(F, a, b, c, d, SET(8), 0x698098d8, 7)
        STEP(F, d, a, b, c, SET(9), 0x8b44f7af, 12)
        STEP(F, c, d, a, b, SET(10), 0xffff5bb1, 17)
        STEP(F, b, c, d, a, SET(11), 0x895cd7be, 22)
        STEP(F, a, b, c, d, SET(12), 0x6b901122, 7)
        STEP(F, d, a, b, c, SET(13), 0xfd987193, 12)
        STEP(F, c, d, a, b, SET(14), 0xa679438e, 17)
        STEP(F, b, c, d, a, SET(15), 0x49b40821, 22)

        // Round 2
        STEP(G, a, b, c, d, GET(1), 0xf61e2562, 5)
        STEP(G, d, a, b, c, GET(6), 0xc040b340, 9)
        STEP(G, c, d, a, b, GET(11), 0x265e5a51, 14)
        STEP(G, b, c, d, a, GET(0), 0xe9b6c7aa, 20)
        STEP(G, a, b, c, d, GET(5), 0xd62f105d, 5)
        STEP(G, d, a, b, c, GET(10), 0x02441453, 9)
        STEP(G, c, d, a, b, GET(15), 0xd8a1e681, 14)
        STEP(G, b, c, d, a, GET(4), 0xe7d3fbc8, 20)
        STEP(G, a, b, c, d, GET(9), 0x21e1cde6, 5)
        STEP(G, d, a, b, c, GET(14), 0xc33707d6, 9)
        STEP(G, c, d, a, b, GET(3), 0xf4d50d87, 14)
        STEP(G, b, c, d, a, GET(8), 0x455a14ed, 20)
        STEP(G, a, b, c, d, GET(13), 0xa9e3e905, 5)
        STEP(G, d, a, b, c, GET(2), 0xfcefa3f8, 9)
        STEP(G, c, d, a, b, GET(7), 0x676f02d9, 14)
        STEP(G, b, c, d, a, GET(12), 0x8d2a4c8a, 20)

        // Round 3
        STEP(H, a, b, c, d, GET(5), 0xfffa3942, 4)
        STEP(H, d, a, b, c, GET(8), 0x8771f681, 11)
        STEP(H, c, d, a, b, GET(11), 0x6d9d6122, 16)
        STEP(H, b, c, d, a, GET(14), 0xfde5380c, 23)
        STEP(H, a, b, c, d, GET(1), 0xa4beea44, 4)
        STEP(H, d, a, b, c, GET(4), 0x4bdecfa9, 11)
        STEP(H, c, d, a, b, GET(7), 0xf6bb4b60, 16)
        STEP(H, b, c, d, a, GET(10), 0xbebfbc70, 23)
        STEP(H, a, b, c, d, GET(13), 0x289b7ec6, 4)
        STEP(H, d, a, b, c, GET(0), 0xeaa127fa, 11)
        STEP(H, c, d, a, b, GET(3), 0xd4ef3085, 16)
        STEP(H, b, c, d, a, GET(6), 0x04881d05, 23)
        STEP(H, a, b, c, d, GET(9), 0xd9d4d039, 4)
        STEP(H, d, a, b, c, GET(12), 0xe6db99e5, 11)
        STEP(H, c, d, a, b, GET(15), 0x1fa27cf8, 16)
        STEP(H, b, c, d, a, GET(2), 0xc4ac5665, 23)

        // Round 4
        STEP(I, a, b, c, d, GET(0), 0xf4292244, 6)
        STEP(I, d, a, b, c, GET(7), 0x432aff97, 10)
        STEP(I, c, d, a, b, GET(14), 0xab9423a7, 15)
        STEP(I, b, c, d, a, GET(5), 0xfc93a039, 21)
        STEP(I, a, b, c, d, GET(12), 0x655b59c3, 6)
        STEP(I, d, a, b, c, GET(3), 0x8f0ccc92, 10)
        STEP(I, c, d, a, b, GET(10), 0xffeff47d, 15)
        STEP(I, b, c, d, a, GET(1), 0x85845dd1, 21)
        STEP(I, a, b, c, d, GET(8), 0x6fa87e4f, 6)
        STEP(I, d, a, b, c, GET(15), 0xfe2ce6e0, 10)
        STEP(I, c, d, a, b, GET(6), 0xa3014314, 15)
        STEP(I, b, c, d, a, GET(13), 0x4e0811a1, 21)
        STEP(I, a, b, c, d, GET(4), 0xf7537e82, 6)
        STEP(I, d, a, b, c, GET(11), 0xbd3af235, 10)
        STEP(I, c, d, a, b, GET(2), 0x2ad7d2bb, 15)
        STEP(I, b, c, d, a, GET(9), 0xeb86d391, 21)

        a += saved_a;
        b += saved_b;
        c += saved_c;
        d += saved_d;

        ptr += 64;
    } while (size -= 64);

    ctx->a = a;
    ctx->b = b;
    ctx->c = c;
    ctx->d = d;

#undef GET
#undef SET

    return ptr;
}

//  Md5Initialise
//
//  Initialises an MD5 Context. Use this to initialise/reset a context.
void
Md5Initialise
        (
                Md5Context* Context         // [out]
        ) {
    Context->a = 0x67452301;
    Context->b = 0xefcdab89;
    Context->c = 0x98badcfe;
    Context->d = 0x10325476;

    Context->lo = 0;
    Context->hi = 0;
}

//  Md5Update
//
//  Adds data to the MD5 context. This will process the data and update the internal state of the context. Keep on
//  calling this function until all the data has been added. Then call Md5Finalise to calculate the hash.
void
Md5Update
        (
                Md5Context* Context,        // [in out]
                void const* Buffer,         // [in]
                uint32_t BufferSize      // [in]
        ) {
    uint32_t saved_lo;
    uint32_t used;
    uint32_t free;

    saved_lo = Context->lo;
    if ((Context->lo = (saved_lo + BufferSize) & 0x1fffffff) < saved_lo) {
        Context->hi++;
    }
    Context->hi += (uint32_t) (BufferSize >> 29);

    used = saved_lo & 0x3f;

    if (used) {
        free = 64 - used;

        if (BufferSize < free) {
            memcpy(&Context->buffer[used], Buffer, BufferSize);
            return;
        }

        memcpy(&Context->buffer[used], Buffer, free);
        Buffer = (uint8_t*) Buffer + free;
        BufferSize -= free;
        TransformFunction(Context, Context->buffer, 64);
    }

    if (BufferSize >= 64) {
        Buffer = TransformFunction(Context, Buffer, BufferSize & ~(unsigned long) 0x3f);
        BufferSize &= 0x3f;
    }

    memcpy(Context->buffer, Buffer, BufferSize);
}

//  Md5Finalise
//
//  Performs the final calculation of the hash and returns the digest (16 byte buffer containing 128bit hash). After
//  calling this, Md5Initialised must be used to reuse the context.
void
Md5Finalise
        (
                Md5Context* Context,        // [in out]
                MD5_HASH* Digest          // [in]
        ) {
    uint32_t used;
    uint32_t free;

    used = Context->lo & 0x3f;

    Context->buffer[used++] = 0x80;

    free = 64 - used;

    if (free < 8) {
        memset(&Context->buffer[used], 0, free);
        TransformFunction(Context, Context->buffer, 64);
        used = 0;
        free = 64;
    }

    memset(&Context->buffer[used], 0, free - 8);

    Context->lo <<= 3;
    Context->buffer[56] = (uint8_t) (Context->lo);
    Context->buffer[57] = (uint8_t) (Context->lo >> 8);
    Context->buffer[58] = (uint8_t) (Context->lo >> 16);
    Context->buffer[59] = (uint8_t) (Context->lo >> 24);
    Context->buffer[60] = (uint8_t) (Context->hi);
    Context->buffer[61] = (uint8_t) (Context->hi >> 8);
    Context->buffer[62] = (uint8_t) (Context->hi >> 16);
    Context->buffer[63] = (uint8_t) (Context->hi >> 24);

    TransformFunction(Context, Context->buffer, 64);

    Digest->bytes[0] = (uint8_t) (Context->a);
    Digest->bytes[1] = (uint8_t) (Context->a >> 8);
    Digest->bytes[2] = (uint8_t) (Context->a >> 16);
    Digest->bytes[3] = (uint8_t) (Context->a >> 24);
    Digest->bytes[4] = (uint8_t) (Context->b);
    Digest->bytes[5] = (uint8_t) (Context->b >> 8);
    Digest->bytes[6] = (uint8_t) (Context->b >> 16);
    Digest->bytes[7] = (uint8_t) (Context->b >> 24);
    Digest->bytes[8] = (uint8_t) (Context->c);
    Digest->bytes[9] = (uint8_t) (Context->c >> 8);
    Digest->bytes[10] = (uint8_t) (Context->c >> 16);
    Digest->bytes[11] = (uint8_t) (Context->c >> 24);
    Digest->bytes[12] = (uint8_t) (Context->d);
    Digest->bytes[13] = (uint8_t) (Context->d >> 8);
    Digest->bytes[14] = (uint8_t) (Context->d >> 16);
    Digest->bytes[15] = (uint8_t) (Context->d >> 24);
}

//  Md5Calculate
//
//  Combines Md5Initialise, Md5Update, and Md5Finalise into one function. Calculates the MD5 hash of the buffer.
void
Md5Calculate
        (
                void const* Buffer,         // [in]
                uint32_t BufferSize,     // [in]
                MD5_HASH* Digest          // [in]
        ) {
    Md5Context context;

    Md5Initialise(&context);
    Md5Update(&context, Buffer, BufferSize);
    Md5Finalise(&context, Digest);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//  End of WjCryptLib_Md5
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

char* appimage_hexlify(const char* bytes, const size_t numBytes) {
    // first of all, allocate the new string
    // a hexadecimal representation works like "every byte will be represented by two chars"
    // additionally, we need to null-terminate the string
    char* hexlified = (char*) calloc((2 * numBytes + 1), sizeof(char));

    for (size_t i = 0; i < numBytes; i++) {
        char buffer[3];
        sprintf(buffer, "%02x", (unsigned char) bytes[i]);
        strcat(hexlified, buffer);
    }

    return hexlified;
}

int main(int argc, char* argv[]) {
    char appimage_path[PATH_MAX];
    char argv0_path[PATH_MAX];
    char* arg;

    /* We might want to operate on a target appimage rather than this file itself,
     * e.g., for appimaged which must not run untrusted code from random AppImages.
     * This variable is intended for use by e.g., appimaged and is subject to
     * change any time. Do not rely on it being present. We might even limit this
     * functionality specifically for builds used by appimaged.
     */
    if (getenv("TARGET_APPIMAGE") == NULL) {
        strcpy(appimage_path, "/proc/self/exe");
        strcpy(argv0_path, argv[0]);
    } else {
        strcpy(appimage_path, getenv("TARGET_APPIMAGE"));
        strcpy(argv0_path, getenv("TARGET_APPIMAGE"));
    }

    // temporary directories are required in a few places
    // therefore we implement the detection of the temp base dir at the top of the code to avoid redundancy
    char temp_base[PATH_MAX] = P_tmpdir;

    {
        const char* const TMPDIR = getenv("TMPDIR");
        if (TMPDIR != NULL)
            strcpy(temp_base, getenv("TMPDIR"));
    }

    fs_offset = appimage_get_elf_size(appimage_path);

    // error check
    if (fs_offset < 0) {
        fprintf(stderr, "Failed to get fs offset for %s\n", appimage_path);
        exit(EXIT_EXECERROR);
    }

    arg = getArg(argc, argv, '-');

    /* Print the help and then exit */
    if (arg && strcmp(arg, "appimage-help") == 0) {
        char fullpath[PATH_MAX];

        ssize_t length = readlink(appimage_path, fullpath, sizeof(fullpath));
        if (length < 0) {
            fprintf(stderr, "Error getting realpath for %s\n", appimage_path);
            exit(EXIT_EXECERROR);
        }
        fullpath[length] = '\0';

        print_help(fullpath);
        exit(0);
    }

    /* Just print the offset and then exit */
    if (arg && strcmp(arg, "appimage-offset") == 0) {
        printf("%zu\n", fs_offset);
        exit(0);
    }

    /* extract the AppImage */
    if (arg && strcmp(arg, "appimage-extract") == 0) {
        char* pattern;

        // default use case: use standard prefix
        if (argc == 2) {
            pattern = NULL;
        } else if (argc == 3) {
            pattern = argv[2];
        } else {
            fprintf(stderr, "Unexpected argument count: %d\n", argc - 1);
            fprintf(stderr, "Usage: %s --appimage-extract [<prefix>]\n", argv0_path);
            exit(1);
        }

        if (!extract_appimage(appimage_path, "squashfs-root/", pattern, true, true)) {
            exit(1);
        }

        exit(0);
    }

    // calculate full path of AppImage
    int length;
    char fullpath[PATH_MAX];

    if (getenv("TARGET_APPIMAGE") == NULL) {
        // If we are operating on this file itself
        ssize_t len = readlink(appimage_path, fullpath, sizeof(fullpath));
        if (len < 0) {
            perror("Failed to obtain absolute path");
            exit(EXIT_EXECERROR);
        }
        fullpath[len] = '\0';
    } else {
        char* abspath = realpath(appimage_path, NULL);
        if (abspath == NULL) {
            perror("Failed to obtain absolute path");
            exit(EXIT_EXECERROR);
        }
        strcpy(fullpath, abspath);
        free(abspath);
    }

    if (getenv("APPIMAGE_EXTRACT_AND_RUN") != NULL || (arg && strcmp(arg, "appimage-extract-and-run") == 0)) {
        char* hexlified_digest = NULL;

        // calculate MD5 hash of file, and use it to make extracted directory name "content-aware"
        // see https://github.com/AppImage/AppImageKit/issues/841 for more information
        {
            FILE* f = fopen(appimage_path, "rb");
            if (f == NULL) {
                perror("Failed to open AppImage file");
                exit(EXIT_EXECERROR);
            }

            Md5Context ctx;
            Md5Initialise(&ctx);

            char buf[4096];
            for (size_t bytes_read; (bytes_read = fread(buf, sizeof(char), sizeof(buf), f)); bytes_read > 0) {
                Md5Update(&ctx, buf, (uint32_t) bytes_read);
            }

            MD5_HASH digest;
            Md5Finalise(&ctx, &digest);

            hexlified_digest = appimage_hexlify(digest.bytes, sizeof(digest.bytes));
        }

        char* prefix = malloc(strlen(temp_base) + 20 + strlen(hexlified_digest) + 2);
        strcpy(prefix, temp_base);
        strcat(prefix, "/appimage_extracted_");
        strcat(prefix, hexlified_digest);
        free(hexlified_digest);

        const bool verbose = (getenv("VERBOSE") != NULL);

        if (!extract_appimage(appimage_path, prefix, NULL, false, verbose)) {
            fprintf(stderr, "Failed to extract AppImage\n");
            exit(EXIT_EXECERROR);
        }

        int pid;
        if ((pid = fork()) == -1) {
            int error = errno;
            fprintf(stderr, "fork() failed: %s\n", strerror(error));
            exit(EXIT_EXECERROR);
        } else if (pid == 0) {
            const char apprun_fname[] = "AppRun";
            char* apprun_path = malloc(strlen(prefix) + 1 + strlen(apprun_fname) + 1);
            strcpy(apprun_path, prefix);
            strcat(apprun_path, "/");
            strcat(apprun_path, apprun_fname);

            // create copy of argument list without the --appimage-extract-and-run parameter
            char* new_argv[argc];
            int new_argc = 0;
            new_argv[new_argc++] = strdup(apprun_path);
            for (int i = 1; i < argc; ++i) {
                if (strcmp(argv[i], "--appimage-extract-and-run") != 0) {
                    new_argv[new_argc++] = strdup(argv[i]);
                }
            }
            new_argv[new_argc] = NULL;

            /* Setting some environment variables that the app "inside" might use */
            setenv("APPIMAGE", fullpath, 1);
            setenv("ARGV0", argv0_path, 1);
            setenv("APPDIR", prefix, 1);

            execv(apprun_path, new_argv);

            int error = errno;
            fprintf(stderr, "Failed to run %s: %s\n", apprun_path, strerror(error));

            free(apprun_path);
            exit(EXIT_EXECERROR);
        }

        int status = 0;
        int rv = waitpid(pid, &status, 0);
        status = rv > 0 && WIFEXITED (status) ? WEXITSTATUS (status) : EXIT_EXECERROR;

        if (getenv("NO_CLEANUP") == NULL) {
            if (!rm_recursive(prefix)) {
                fprintf(stderr, "Failed to clean up cache directory\n");
                if (status == 0)        /* avoid messing existing failure exit status */
                    status = EXIT_EXECERROR;
            }
        }

        // template == prefix, must be freed only once
        free(prefix);

        exit(status);
    }

    if (arg && (strcmp(arg, "appimage-updateinformation") == 0 || strcmp(arg, "appimage-updateinfo") == 0)) {
        fprintf(stderr, "--%s is not yet implemented in version %s\n", arg, GIT_COMMIT);
        // NOTE: Must be implemented in this .c file with no additional dependencies
        exit(1);
    }

    if (arg && strcmp(arg, "appimage-signature") == 0) {
        fprintf(stderr, "--%s is not yet implemented in version %s\n", arg, GIT_COMMIT);
        // NOTE: Must be implemented in this .c file with no additional dependencies
        exit(1);
    }

    portable_option(arg, appimage_path, "home");
    portable_option(arg, appimage_path, "config");

    // If there is an argument starting with appimage- (but not appimage-mount which is handled further down)
    // then stop here and print an error message
    if ((arg && strncmp(arg, "appimage-", 8) == 0) && (arg && strcmp(arg, "appimage-mount") != 0)) {
        fprintf(stderr, "--%s is not yet implemented in version %s\n", arg, GIT_COMMIT);
        exit(1);
    }

    int dir_fd, res;

    size_t templen = strlen(temp_base);

    // allocate enough memory (size of name won't exceed 60 bytes)
    char mount_dir[templen + 60];

    build_mount_point(mount_dir, argv[0], temp_base, templen);

    size_t mount_dir_size = strlen(mount_dir);
    pid_t pid;
    char** real_argv;
    int i;

    if (mkdtemp(mount_dir) == NULL) {
        perror("create mount dir error");
        exit(EXIT_EXECERROR);
    }

    if (pipe(keepalive_pipe) == -1) {
        perror("pipe error");
        exit(EXIT_EXECERROR);
    }

    pid = fork();
    if (pid == -1) {
        perror("fork error");
        exit(EXIT_EXECERROR);
    }

    if (pid == 0) {
        /* in child */

        char* child_argv[5];

        /* close read pipe */
        close(keepalive_pipe[0]);

        char* dir = realpath(appimage_path, NULL);

        char options[100];
        sprintf(options, "ro,offset=%zu", fs_offset);

        child_argv[0] = dir;
        child_argv[1] = "-o";
        child_argv[2] = options;
        child_argv[3] = dir;
        child_argv[4] = mount_dir;

        if (0 != fusefs_main(5, child_argv, fuse_mounted)) {
            char* title;
            char* body;
            title = "Cannot mount AppImage, please check your FUSE setup.";
            body = "You might still be able to extract the contents of this AppImage \n"
                   "if you run it with the --appimage-extract option. \n"
                   "See https://github.com/AppImage/AppImageKit/wiki/FUSE \n"
                   "for more information";
            printf("\n%s\n", title);
            printf("%s\n", body);
        };
    } else {
        /* in parent, child is $pid */
        int c;

        /* close write pipe */
        close(keepalive_pipe[1]);

        /* Pause until mounted */
        read(keepalive_pipe[0], &c, 1);

        /* Fuse process has now daemonized, reap our child */
        waitpid(pid, NULL, 0);

        dir_fd = open(mount_dir, O_RDONLY);
        if (dir_fd == -1) {
            perror("open dir error");
            exit(EXIT_EXECERROR);
        }

        res = dup2(dir_fd, 1023);
        if (res == -1) {
            perror("dup2 error");
            exit(EXIT_EXECERROR);
        }
        close(dir_fd);

        real_argv = malloc(sizeof(char*) * (argc + 1));
        for (i = 0; i < argc; i++) {
            real_argv[i] = argv[i];
        }
        real_argv[i] = NULL;

        if (arg && strcmp(arg, "appimage-mount") == 0) {
            char real_mount_dir[PATH_MAX];

            if (realpath(mount_dir, real_mount_dir) == real_mount_dir) {
                printf("%s\n", real_mount_dir);
            } else {
                printf("%s\n", mount_dir);
            }

            // stdout is, by default, buffered (unlike stderr), therefore in order to allow other processes to read
            // the path from stdout, we need to flush the buffers now
            // this is a less-invasive alternative to setbuf(stdout, NULL);
            fflush(stdout);

            for (;;) pause();

            exit(0);
        }

        /* Setting some environment variables that the app "inside" might use */
        setenv("APPIMAGE", fullpath, 1);
        setenv("ARGV0", argv0_path, 1);
        setenv("APPDIR", mount_dir, 1);

        char portable_home_dir[PATH_MAX];
        char portable_config_dir[PATH_MAX];

        /* If there is a directory with the same name as the AppImage plus ".home", then export $HOME */
        strcpy(portable_home_dir, fullpath);
        strcat(portable_home_dir, ".home");
        if (is_writable_directory(portable_home_dir)) {
            fprintf(stderr, "Setting $HOME to %s\n", portable_home_dir);
            setenv("HOME", portable_home_dir, 1);
        }

        /* If there is a directory with the same name as the AppImage plus ".config", then export $XDG_CONFIG_HOME */
        strcpy(portable_config_dir, fullpath);
        strcat(portable_config_dir, ".config");
        if (is_writable_directory(portable_config_dir)) {
            fprintf(stderr, "Setting $XDG_CONFIG_HOME to %s\n", portable_config_dir);
            setenv("XDG_CONFIG_HOME", portable_config_dir, 1);
        }

        /* Original working directory */
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            setenv("OWD", cwd, 1);
        }

        char filename[mount_dir_size + 8]; /* enough for mount_dir + "/AppRun" */
        strcpy(filename, mount_dir);
        strcat(filename, "/AppRun");

        /* TODO: Find a way to get the exit status and/or output of this */
        execv(filename, real_argv);
        /* Error if we continue here */
        perror("execv error");
        exit(EXIT_EXECERROR);
    }

    return 0;
}
