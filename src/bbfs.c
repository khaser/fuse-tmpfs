/*
  Big Brother File System
  Copyright (C) 2012 Joseph J. Pfeiffer, Jr., Ph.D. <pfeiffer@cs.nmsu.edu>

  This program can be distributed under the terms of the GNU GPLv3.
  See the file COPYING.

  This code is derived from function prototypes found /usr/include/fuse/fuse.h
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  His code is licensed under the LGPLv2.
  A copy of that code is included in the file fuse.h

  The point of this FUSE filesystem is to provide an introduction to
  FUSE.  It was my first FUSE filesystem as I got to know the
  software; hopefully, the comments in this code will help people who
  follow later to get a gentler introduction.

  This might be called a no-op filesystem:  it doesn't impose
  filesystem semantics on top of any other existing structure.  It
  simply reports the requests that come in, and passes them to an
  underlying filesystem.  The information is saved in a logfile named
  bbfs.log, in the directory from which you run bbfs.
*/
#include "config.h"
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

#include "log.h"
#include <assert.h>

static int add_dentry(struct dir* dir, const struct dentry dentry)
{
    int retstat = -EMLINK;

    for (struct dentry *entry = dir->entries; entry != dir->entries + INODES_IN_DIRECTORY; ++entry) {
        if (!entry->is_active) {
            memcpy(entry, &dentry, sizeof(struct dentry));
            retstat = 0;
            break;
        }
    }

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
            if (entry->is_active && memcmp(path + 1, entry->name, next_token - path - 1) == 0) {
                cur = entry->inode;
                goto found;
            }
        }

        retstat = -ENOENT;
        break;

        found:
        path = next_token;
    }

    log_msg("    tmpfs_resolve:  path = %s, inode = %p\n", path, cur);
    *res = cur;
    return retstat;
}

int tmpfs_getattr(const char *path, struct stat *statbuf)
{
    int retstat = 0;
    log_msg("\ntmpfs_getattr(path=\"%s\", statbuf=0x%08x)\n", path, statbuf);

    struct inode* inode;
    retstat = resolve_inode(path, -1, &inode);

    if (!retstat) {
        memcpy(statbuf, &inode->stat, sizeof(struct stat));
    }

    return retstat;
}

// TODO
/** Create a file node
 *
 * There is no create() operation, mknod() will be called for
 * creation of all non-directory, non-symlink nodes.
 */
// shouldn't that comment be "if" there is no.... ?
int tmpfs_mknod(const char *path, mode_t mode, dev_t dev)
{
    int retstat = 0;
    log_msg("\nbb_mknod(path=\"%s\", mode=0%3o, dev=%lld)\n",
         path, mode, dev);
    assert(0);
    return retstat;
}

int tmpfs_mkdir(const char *path, mode_t mode)
{
    int retstat = -ENOMEM;
    log_msg("\nmkdir(path=\"%s\", mode=0%3o)\n", path, mode);
    int i = 0;
    for (; i < INODES_LIMIT; ++i) {
        if (!TMPFS_DATA->inodes[i].is_active) {
            struct stat stat;
            stat.st_mode |= S_IFDIR | mode;
            struct fuse_context* ctx = fuse_get_context();
            stat.st_uid = ctx->uid;
            stat.st_gid = ctx->gid;

            struct inode* parent_inode;
            retstat = resolve_inode(path, -2, &parent_inode);
            if (retstat) break;

            struct dir *dir = init_dir(TMPFS_DATA->inodes + i, parent_inode);
            TMPFS_DATA->inodes[i] = (struct inode) { stat, DIRECTORY, dir, parent_inode, 1 };

            struct dentry dentry;
            dentry.is_active = 1;
            dentry.inode = TMPFS_DATA->inodes + i;

            char *dirname = strrchr(path, '/') + 1;

            if (strlen(dirname) >= NAME_LEN) {
                retstat = -ENAMETOOLONG;
                break;
            }
            strcpy(dentry.name, dirname);

            retstat = add_dentry(parent_inode->data_ptr, dentry);
            break;
        }
    }

    return retstat;
}

// TODO
/** Remove a file */
int tmpfs_unlink(const char *path)
{
    char fpath[PATH_MAX];

    log_msg("bb_unlink(path=\"%s\")\n",
           path);
    assert(0);

    return log_syscall("unlink", unlink(fpath), 0);
}

int tmpfs_rmdir(const char *path)
{

    log_msg("tmpfs_rmdir(path=\"%s\")\n",
           path);

    int retstat = 0;

    struct inode* inode;
    retstat = resolve_inode(path, -1, &inode);
    if (retstat) goto ret;

    if (inode->type != DIRECTORY) {
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

    ret:
    return retstat;
}

// TODO
/** Rename a file */
// both path and newpath are fs-relative
int tmpfs_rename(const char *path, const char *newpath)
{
    char fpath[PATH_MAX];
    char fnewpath[PATH_MAX];

    log_msg("\nbb_rename(fpath=\"%s\", newpath=\"%s\")\n",
           path, newpath);
    assert(0);
    assert(0);

    return log_syscall("rename", rename(fpath, fnewpath), 0);
}

// TODO
/** Create a hard link to a file */
int tmpfs_link(const char *path, const char *newpath)
{
    char fpath[PATH_MAX], fnewpath[PATH_MAX];

    log_msg("\nbb_link(path=\"%s\", newpath=\"%s\")\n",
           path, newpath);
    assert(0);
    assert(0);

    return log_syscall("link", link(fpath, fnewpath), 0);
}

// TODO
/** Change the size of a file */
int tmpfs_truncate(const char *path, off_t newsize)
{
    char fpath[PATH_MAX];

    log_msg("\nbb_truncate(path=\"%s\", newsize=%lld)\n",
           path, newsize);
    assert(0);

    return log_syscall("truncate", truncate(fpath, newsize), 0);
}

// TODO
int tmpfs_open(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    int fd;
    char fpath[PATH_MAX];

    log_msg("\nbb_open(path\"%s\", fi=0x%08x)\n",
           path, fi);
    assert(0);

    // if the open call succeeds, my retstat is the file descriptor,
    // else it's -errno.  I'm making sure that in that case the saved
    // file descriptor is exactly -1.
    fd = log_syscall("open", open(fpath, fi->flags), 0);
    if (fd < 0)
       retstat = log_error("open");

    fi->fh = fd;

    log_fi(fi);

    return retstat;
}

// TODO
int tmpfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int retstat = 0;

    log_msg("\nbb_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
           path, buf, size, offset, fi);
    // no need to get fpath on this one, since I work from fi->fh not the path
    log_fi(fi);

    return log_syscall("pread", pread(fi->fh, buf, size, offset), 0);
}

// TODO
int tmpfs_write(const char *path, const char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    int retstat = 0;

    log_msg("\nbb_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
           path, buf, size, offset, fi
           );
    // no need to get fpath on this one, since I work from fi->fh not the path
    log_fi(fi);

    return log_syscall("pwrite", pwrite(fi->fh, buf, size, offset), 0);
}

// TODO
int tmpfs_release(const char *path, struct fuse_file_info *fi)
{
    log_msg("\nbb_release(path=\"%s\", fi=0x%08x)\n",
         path, fi);
    log_fi(fi);

    // We need to close the file.  Had we allocated any resources
    // (buffers etc) we'd need to free them here as well.
    return log_syscall("close", close(fi->fh), 0);
}

int tmpfs_opendir(const char *path, struct fuse_file_info *fi)
{
    log_msg("\ntmpfs_opendir(path=\"%s\", fi=0x%08x)\n",
         path, fi);

    struct inode* inode;
    int retstat = resolve_inode(path, -1, &inode);
    if (!retstat) {
        fi->fh = inode;
    }
    return retstat;
}

int tmpfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
              struct fuse_file_info *fi)
{
    int retstat = 0;
    struct dirent *de;

    log_msg("\ntmpfs_readdir(path=\"%s\", buf=0x%08x, filler=0x%08x, offset=%lld, fi=0x%08x)\n",
           path, buf, filler, offset, fi);

    // once again, no need for fullpath -- but note that I need to cast fi->fh
    struct inode *inode = (struct inode*) fi->fh;

    struct dir *dir = inode->data_ptr;
    for (struct dentry* entry = dir->entries; entry != dir->entries + INODES_IN_DIRECTORY; ++entry) {
        if (entry->is_active) {
           log_msg("calling filler with name %s\n", entry->name);
           if (filler(buf, entry->name, NULL, 0) != 0) {
               log_msg("    ERROR tmpfs_readdir filler:  buffer full");
               retstat = -ENOMEM;
               break;
           }
        }
    }

    return retstat;
}

void *tmpfs_init(struct fuse_conn_info *conn)
{
    log_msg("\ntmpfs_init()\n");

    log_conn(conn);
    log_fuse_context(fuse_get_context());

    return TMPFS_DATA;
}

// TODO
void tmpfs_destroy(void *userdata)
{
    log_msg("\nbb_destroy(userdata=0x%08x)\n", userdata);
}

int tmpfs_access(const char *path, int mask)
{
    log_msg("\ntmpfs_access(path=\"%s\", mask=0%o)\n",
           path, mask);

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
  .mknod = tmpfs_mknod,
  .mkdir = tmpfs_mkdir, // done
  .unlink = tmpfs_unlink,
  .rmdir = tmpfs_rmdir, // done
  .rename = tmpfs_rename,
  .link = tmpfs_link,
  .truncate = tmpfs_truncate,
  .open = tmpfs_open,
  .read = tmpfs_read,
  .write = tmpfs_write,
  .release = tmpfs_release,
  .readdir = tmpfs_readdir, // done
  .init = tmpfs_init, // done
  .destroy = tmpfs_destroy,
  .access = tmpfs_access, // done
};

void tmpfs_usage()
{
    fprintf(stderr, "usage:  bbfs [FUSE and mount options] rootDir mountPoint\n");
    abort();
}

int main(int argc, char *argv[])
{
    int fuse_stat;
    struct tmpfs_state *tmpfs_data;

    // bbfs doesn't do any access checking on its own (the comment
    // blocks in fuse.h mention some of the functions that need
    // accesses checked -- but note there are other functions, like
    // chown(), that also need checking!).  Since running bbfs as root
    // will therefore open Metrodome-sized holes in the system
    // security, we'll check if root is trying to mount the filesystem
    // and refuse if it is.  The somewhat smaller hole of an ordinary
    // user doing it with the allow_other flag is still there because
    // I don't want to parse the options string.
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

    tmpfs_data = malloc(sizeof(struct tmpfs_state));
    if (tmpfs_data == NULL) {
       perror("main calloc");
       abort();
    }

    struct dir *dir = init_dir(tmpfs_data->inodes, tmpfs_data->inodes);
    struct stat stat;
    stat.st_mode |= S_IFDIR;
    tmpfs_data->inodes[0] = (struct inode) { stat, DIRECTORY, dir, tmpfs_data->inodes, 1 };

    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main\n");
    fuse_stat = fuse_main(argc, argv, &tmpfs_oper, tmpfs_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);

    return fuse_stat;
}
