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

// TODO: refactor to single return

static int add_dentry(struct dir* dir, const struct dentry dentry)
{
    for (struct dentry *entry = dir->entries; entry != dir->entries + INODES_IN_DIRECTORY; ++entry) {
        if (!entry->is_active) {
            memcpy(entry, &dentry, sizeof(struct dentry));
            return 0;
        }
    }
    return -EMLINK;
}

static int resolve_inode(const char *path, int req_component)
{
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

        return -ENOENT;

        found:
        path = next_token;
    }

    log_msg("    tmpfs_resolve:  path = \"%s\", inode = \"%p\"\n", path, cur);
    return cur;
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int tmpfs_getattr(const char *path, struct stat *statbuf)
{
    log_msg("\ntmpfs_getattr(path=\"%s\", statbuf=0x%08x)\n", path, statbuf);

    int res = resolve_inode(path, -1);
    if (res < 0) {
        return res;
    }
    struct inode* inode = (struct inode*) res;

    memcpy(statbuf, &inode->stat, sizeof(struct stat));
    return 0;
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

struct dir* init_dir(struct inode *self, struct inode *parent) {
    struct dir *dir = malloc(sizeof(struct dir));
    dir->entries[0] = (struct dentry) { "..", parent, 1 };
    dir->entries[1] = (struct dentry) { ".", self, 1 };
    return dir;
}

/** Create a directory */
int tmpfs_mkdir(const char *path, mode_t mode)
{
    log_msg("\nmkdir(path=\"%s\", mode=0%3o)\n", path, mode);
    int i = 0;
    for (; i < INODES_LIMIT; ++i) {
        if (!TMPFS_DATA->inodes[i].is_active) {
            struct stat stat;
            stat.st_mode |= S_IFDIR | mode;
            struct fuse_context* ctx = fuse_get_context();
            stat.st_uid = ctx->uid;
            stat.st_gid = ctx->gid;

            int tmp = resolve_inode(path, -2);
            if (tmp < 0) {
                return tmp;
            }
            struct inode* parent_inode = tmp;

            struct dir *dir = init_dir(TMPFS_DATA->inodes + i, parent_inode);
            TMPFS_DATA->inodes[i] = (struct inode) { stat, DIRECTORY, dir, parent_inode, 1 };

            struct dentry dentry;
            dentry.is_active = 1;
            dentry.inode = TMPFS_DATA->inodes + i;

            char *dirname = strrchr(path, '/') + 1;

            if (strlen(dirname) >= NAME_LEN) {
                return -ENAMETOOLONG;
            }
            strcpy(dentry.name, dirname);

            return add_dentry(parent_inode->data_ptr, dentry);
        }
    }

    return -ENOMEM;
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

// TODO
/** Remove a directory */
int tmpfs_rmdir(const char *path)
{
    char fpath[PATH_MAX];

    log_msg("bb_rmdir(path=\"%s\")\n",
           path);
    assert(0);

    return log_syscall("rmdir", rmdir(fpath), 0);
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
/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
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
/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
// I don't fully understand the documentation above -- it doesn't
// match the documentation for the read() system call which says it
// can return with anything up to the amount of data requested. nor
// with the fusexmp code which returns the amount of data also
// returned by read.
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
/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
// As  with read(), the documentation above is inconsistent with the
// documentation for the write() system call.
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
/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int tmpfs_release(const char *path, struct fuse_file_info *fi)
{
    log_msg("\nbb_release(path=\"%s\", fi=0x%08x)\n",
         path, fi);
    log_fi(fi);

    // We need to close the file.  Had we allocated any resources
    // (buffers etc) we'd need to free them here as well.
    return log_syscall("close", close(fi->fh), 0);
}

/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int tmpfs_opendir(const char *path, struct fuse_file_info *fi)
{
    log_msg("\ntmpfs_opendir(path=\"%s\", fi=0x%08x)\n",
         path, fi);

    int res = resolve_inode(path, -1);
    if (res >= 0) {
        struct inode* inode = (struct inode*) res;
        fi->fh = inode;
        return 0;
    } else {
        return res;
    }
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */

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
               log_msg("    ERROR bb_readdir filler:  buffer full");
               return -ENOMEM;
           }
        }
    }

    return retstat;
}

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
// Undocumented but extraordinarily useful fact:  the fuse_context is
// set up before this function is called, and
// fuse_get_context()->private_data returns the user_data passed to
// fuse_main().  Really seems like either it should be a third
// parameter coming in here, or else the fact should be documented
// (and this might as well return void, as it did in older versions of
// FUSE).
void *tmpfs_init(struct fuse_conn_info *conn)
{
    log_msg("\ntmpfs_init()\n");

    log_conn(conn);
    log_fuse_context(fuse_get_context());

    return TMPFS_DATA;
}

// TODO
/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void tmpfs_destroy(void *userdata)
{
    log_msg("\nbb_destroy(userdata=0x%08x)\n", userdata);
}

/**
 * Check file access permissions
 *
 * This will be called for the access() system call.  If the
 * 'default_permissions' mount option is given, this method is not
 * called.
 *
 * This method is not called under Linux kernel versions 2.4.x
 *
 * Introduced in version 2.5
 */
int tmpfs_access(const char *path, int mask)
{
    log_msg("\ntmpfs_access(path=\"%s\", mask=0%o)\n",
           path, mask);

    int res = resolve_inode(path, -1);
    if (res >= 0) {
        struct inode* inode = (struct inode*) res;
        return inode->stat.st_mode & mask;
    } else {
        return res;
    }
}

struct fuse_operations tmpfs_oper = {
  .getattr = tmpfs_getattr, // done
  .opendir = tmpfs_opendir, // done
  .getdir = NULL,
  .mknod = tmpfs_mknod,
  .mkdir = tmpfs_mkdir, // done
  .unlink = tmpfs_unlink,
  .rmdir = tmpfs_rmdir,
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
