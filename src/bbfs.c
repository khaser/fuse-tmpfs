#include "params.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#include <assert.h>
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

static int _add_dentry(struct dir *dir, const struct dentry *dentry)
{
    int retstat = 0;

    struct dentry *to_alloc;

    for (struct dentry *entry = dir->entries; entry != dir->entries + INODES_IN_DIRECTORY; ++entry) {
        if (!entry->is_active) {
            to_alloc = entry;
        } else {
            if (strcmp(dentry->name, entry->name) == 0) {
                retstat = -EEXIST;
                goto ret;
            }
        }
    }

    if (to_alloc) {
        memcpy(to_alloc, dentry, sizeof(struct dentry));
    } else {
        retstat = -EMLINK;
    }

    ret:
    return retstat;
}

static int add_dentry(const char *path, struct inode *target_inode, struct inode *dir_inode) {
    int retstat = 0;
    struct dentry dentry;
    dentry.is_active = 1;
    dentry.inode = target_inode;

    const char *comp_name = strrchr(path, '/') + 1;

    if (strlen(comp_name) >= NAME_LEN) {
        retstat = -ENAMETOOLONG;
        goto ret;
    }
    strcpy(dentry.name, comp_name);

    retstat = _add_dentry(dir_inode->data_ptr, &dentry);
    ret:
    return retstat;
}

static int rm_dentry(struct dir* dir, const struct inode* inode)
{
    int retstat = 0;

    for (struct dentry *entry = dir->entries; entry != dir->entries + INODES_IN_DIRECTORY; ++entry) {
        if (entry->is_active && entry->inode == inode) {
            entry->is_active = 0;
            goto ret;
        }
    }
    assert(0 && "Inode not found in directory on delete");

    ret:
    return retstat;
}

static struct dir* init_dir(struct inode *self, struct inode *parent)
{
    struct dir *dir = malloc(sizeof(struct dir));
    dir->entries[0] = (struct dentry) { "..", parent, 1 };
    dir->entries[1] = (struct dentry) { ".", self, 1 };
    return dir;
}

static int resolve_inode(const char *path, int req_component, struct inode **res)
{
    int retstat = 0;

    if (req_component < 0) {
        int components = 0;
        const char *i = path;
        for (; *i != '\0'; ++i) {
            components += (*i == '/');
        }
        components += (*(i-1) != '/');
        req_component = components + req_component;
    }

    struct inode *cur = TMPFS_DATA->inodes;
    for (int i = 0; i < req_component; ++i) {

        // /comp1/comp2
        // ^     ^
        // path  next_token
        const char *next_token = strchr(path + 1, '/');
        if (next_token == 0) {
            next_token = path + strlen(path);
        }

        struct dir* dir = cur->data_ptr;

        for (struct dentry *entry = dir->entries; entry != dir->entries + INODES_IN_DIRECTORY; ++entry) {
            if (entry->is_active && strlen(entry->name) == next_token - (path + 1) && memcmp(path + 1, entry->name, next_token - path - 1) == 0) {
                cur = entry->inode;
                goto found;
            }
        }

        retstat = -ENOENT;
        break;

        found:
        path = next_token;
    }

    *res = cur;
    return retstat;
}

static int alloc_inode(struct inode **res)
{
    int retstat = -EDQUOT;
    for (struct inode* i = TMPFS_DATA->inodes; i < TMPFS_DATA->inodes + INODES_LIMIT; ++i) {
        if (!i->is_active) {
            retstat = 0;
            i->stat.st_ino = i - TMPFS_DATA->inodes;
            *res = i;
            break;
        }
    }
    return retstat;
}

static int add_node(const char *path, mode_t mode, struct inode **res_inode)
{
    int retstat = 0;

    struct inode* inode;
    retstat = alloc_inode(&inode);
    if (retstat) {
        goto ret;
    }

    inode->stat.st_mode |= mode;
    struct fuse_context* ctx = fuse_get_context();
    inode->stat.st_uid = ctx->uid;
    inode->stat.st_gid = ctx->gid;
    inode->stat.st_nlink = 1;
    retstat = resolve_inode(path, -2, &inode->parent);
    if (retstat) {
        goto ret;
    }
    inode->is_active = 1;
    add_dentry(path, inode, inode->parent);
    *res_inode = inode;
    ret:
    return retstat;
}

int tmpfs_getattr(const char *path, struct stat *statbuf)
{
    int retstat = 0;

    struct inode* inode;
    retstat = resolve_inode(path, -1, &inode);

    if (!retstat) {
        memcpy(statbuf, &inode->stat, sizeof(struct stat));
    }

    return retstat;
}

int tmpfs_mknod(const char *path, mode_t mode, dev_t dev)
{
    int retstat = 0;
    struct inode* inode;
    retstat = add_node(path, mode | S_IFREG, &inode);
    if (!retstat) {
        inode->stat.st_size = 0;
        inode->data_ptr = malloc(0);
    }
    return retstat;
}

int tmpfs_mkdir(const char *path, mode_t mode)
{
    int retstat = 0;
    struct inode* inode;
    retstat = add_node(path, mode | S_IFDIR, &inode);
    if (!retstat) {
        inode->data_ptr = init_dir(inode, inode->parent);
    }
    return retstat;
}

int tmpfs_unlink(const char *path)
{
    struct inode* inode;
    int retstat = resolve_inode(path, -1, &inode);
    if (!retstat) {
        struct inode* parent_inode;
        retstat = resolve_inode(path, -2, &parent_inode);
        if (retstat) {
            goto ret;
        }
        rm_dentry(parent_inode->data_ptr, inode);
        if (--inode->stat.st_nlink == 0 && inode->open_count == 0) {
            inode->is_active = 0;
        }
    }
    ret:
    return retstat;
}

int tmpfs_rmdir(const char *path)
{
    int retstat = 0;

    struct inode* inode;
    retstat = resolve_inode(path, -1, &inode);
    if (retstat) goto ret;

    if ((inode->stat.st_mode & S_IFDIR) == 0) {
        retstat = -ENOTDIR;
        goto ret;
    }
    struct dir* dir = inode->data_ptr;

    // first two entries is .. and ., so skip it
    for (struct dentry* i = dir->entries + 2; i != dir->entries + INODES_IN_DIRECTORY; ++i) {
        if (i->is_active) {
            retstat = -ENOTEMPTY;
            goto ret;
        }
    }

    inode->is_active = 0;
    free(dir);

    rm_dentry(inode->parent->data_ptr, inode);

    ret:
    return retstat;
}

int tmpfs_rename(const char *path, const char *newpath)
{
    // Check that newpath is not subdirectory of path
    int retstat = 0;
    if (strstr(newpath, path) == newpath) {
        retstat = -EINVAL;
        goto ret;
    }

    struct inode* inode;
    retstat = resolve_inode(path, -1, &inode);
    if (retstat) {
        goto ret;
    }
    struct inode* parent_inode;
    retstat = resolve_inode(path, -2, &parent_inode);
    if (retstat) {
        goto ret;
    }
    retstat = rm_dentry(parent_inode->data_ptr, inode);
    if (retstat) {
        goto ret;
    }

    struct inode* dir_inode;
    retstat = resolve_inode(newpath, -2, &dir_inode);
    if (retstat) {
        goto ret;
    }

    retstat = add_dentry(newpath, inode, dir_inode);

    ret:
    return retstat;
}

int tmpfs_link(const char *path, const char *newpath)
{
    struct inode* target_inode;
    int retstat = resolve_inode(path, -1, &target_inode);
    if (retstat) {
        goto ret;
    }
    if (target_inode->stat.st_mode & S_IFDIR) {
        retstat = -EPERM;
        goto ret;
    }

    struct inode* dir_inode;
    retstat = resolve_inode(newpath, -2, &dir_inode);
    if (retstat) {
        goto ret;
    }

    retstat = add_dentry(newpath, target_inode, dir_inode);
    if (!retstat) {
        target_inode->stat.st_nlink++;
    }

    ret:
    return retstat;
}

int tmpfs_truncate(const char *path, off_t newsize)
{
    struct inode* inode;
    int retstat = resolve_inode(path, -1, &inode);
    if (retstat) {
        goto ret;
    }

    inode->data_ptr = realloc(inode->data_ptr, newsize);
    if (newsize > inode->stat.st_size) {
        memset(inode->data_ptr + inode->stat.st_size, 0, newsize - inode->stat.st_size);
    }
    inode->stat.st_size = newsize;

    ret:
    return retstat;
}

int tmpfs_open(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;

    struct inode* inode;
    retstat = resolve_inode(path, -1, &inode);
    if (!retstat) {
        if (inode->stat.st_mode & S_IFREG) {
            fi->fh = (uint64_t) inode;
            inode->open_count++;
        } else {
            retstat = -EISDIR;
        }
    }

    return retstat;
}

int tmpfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int retstat = 0;
    struct inode* inode = (struct inode*) fi->fh;

    int to_read = MIN((inode->stat.st_size - offset) - size, size);
    if (to_read < 0) {
        retstat = 0;
    } else {
        memcpy(buf, inode->data_ptr + offset, to_read);
        retstat = to_read;
    }

    return retstat;
}

int tmpfs_write(const char *path, const char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    struct inode* inode = (struct inode*) fi->fh;

    if (size + offset > inode->stat.st_size) {
        inode->data_ptr = realloc(inode->data_ptr, size + offset);
        inode->stat.st_size = size + offset;
    }

    memcpy(inode->data_ptr + offset, buf, size);

    return size;
}

int tmpfs_release(const char *path, struct fuse_file_info *fi)
{
    struct inode* inode = (struct inode*) fi->fh;
    inode->open_count--;
    if (inode->stat.st_nlink == 0 && inode->open_count == 0) {
        inode->is_active = 0;
    }
    return 0;
}

int tmpfs_opendir(const char *path, struct fuse_file_info *fi)
{
    struct inode* inode;
    int retstat = resolve_inode(path, -1, &inode);
    if (!retstat) {
        fi->fh = (uint64_t) inode;
    }
    return retstat;
}

int tmpfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
              struct fuse_file_info *fi)
{
    int retstat = 0;
    struct dirent *de;

    struct inode *inode = (struct inode*) fi->fh;

    struct dir *dir = inode->data_ptr;
    for (struct dentry* entry = dir->entries; entry != dir->entries + INODES_IN_DIRECTORY; ++entry) {
        if (entry->is_active) {
           if (filler(buf, entry->name, NULL, 0) != 0) {
               retstat = -ENOMEM;
               break;
           }
        }
    }

    return retstat;
}

void *tmpfs_init(struct fuse_conn_info *conn)
{
    return TMPFS_DATA;
}

int tmpfs_access(const char *path, int mask)
{
    struct inode* inode;
    int retstat = resolve_inode(path, -1, &inode);
    if (!retstat) {
        retstat = (inode->stat.st_mode & mask ? 0 : -EACCES);
    }
    return retstat;
}

struct fuse_operations tmpfs_oper = {
  .getattr = tmpfs_getattr, // done
  .opendir = tmpfs_opendir, // done
  .getdir = NULL,
  .mknod = tmpfs_mknod, // done
  .mkdir = tmpfs_mkdir, // done
  .unlink = tmpfs_unlink, // done
  .rmdir = tmpfs_rmdir, // done
  .rename = tmpfs_rename, // done
  .link = tmpfs_link, // done
  .truncate = tmpfs_truncate, // done
  .open = tmpfs_open, // done
  .read = tmpfs_read, // done
  .write = tmpfs_write, // done
  .release = tmpfs_release, // done
  .readdir = tmpfs_readdir, // done
  .init = tmpfs_init, // done
  .access = tmpfs_access, // done
};

void tmpfs_usage()
{
    fprintf(stderr, "usage:  tmpfs [FUSE and mount options] mountPoint\n");
    abort();
}

int main(int argc, char *argv[])
{
    int fuse_stat;
    struct tmpfs_state *tmpfs_data;

    if ((getuid() == 0) || (geteuid() == 0)) {
           fprintf(stderr, "Running TMPFS as root opens unnacceptable security holes\n");
           return 1;
    }

    // See which version of fuse we're running
    fprintf(stderr, "Fuse library version %d.%d\n", FUSE_MAJOR_VERSION, FUSE_MINOR_VERSION);

    // Perform some sanity checking on the command line:  make sure
    // there are enough arguments, and that neither of the last two
    // start with a hyphen (this will break if you actually have a
    // rootpoint or mountpoint whose name starts with a hyphen, but so
    // will a zillion other programs)
    if ((argc < 2) || (argv[argc-1][0] == '-'))
       tmpfs_usage();

    tmpfs_data = calloc(1, sizeof(struct tmpfs_state));
    if (tmpfs_data == NULL) {
       perror("main calloc");
       abort();
    }

    struct dir *dir = init_dir(tmpfs_data->inodes, tmpfs_data->inodes);
    struct stat stat;
    stat.st_uid = getuid();
    stat.st_gid = getgid();
    stat.st_mode = S_IFDIR | 0x1ed;
    stat.st_nlink = 1;
    tmpfs_data->inodes[0] = (struct inode) { stat, dir, tmpfs_data->inodes, 1 };

    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main\n");
    fuse_stat = fuse_main(argc, argv, &tmpfs_oper, tmpfs_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);

    return fuse_stat;
}
