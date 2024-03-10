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

///////////////////////////////////////////////////////////
//
// Prototypes for all these functions, and the C-style comments,
// come from /usr/include/fuse.h
//
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

/** Read the target of a symbolic link
 *
 * The buffer should be filled with a null terminated string.  The
 * buffer size argument includes the space for the terminating
 * null character.  If the linkname is too long to fit in the
 * buffer, it should be truncated.  The return value should be 0
 * for success.
 */
// Note the system readlink() will truncate and lose the terminating
// null.  So, the size passed to to the system readlink() must be one
// less than the size passed to bb_readlink()
// bb_readlink() code by Bernardo F Costa (thanks!)
int tmpfs_readlink(const char *path, char *link, size_t size)
{
    int retstat = 0;
    log_msg("\nbb_readlink(path=\"%s\", link=\"%s\", size=%d)\n",
         path, link, size);
    assert(0);
    return retstat;
}

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

/** Remove a file */
int tmpfs_unlink(const char *path)
{
    char fpath[PATH_MAX];

    log_msg("bb_unlink(path=\"%s\")\n",
           path);
    assert(0);

    return log_syscall("unlink", unlink(fpath), 0);
}

/** Remove a directory */
int tmpfs_rmdir(const char *path)
{
    char fpath[PATH_MAX];

    log_msg("bb_rmdir(path=\"%s\")\n",
           path);
    assert(0);

    return log_syscall("rmdir", rmdir(fpath), 0);
}

/** Create a symbolic link */
// The parameters here are a little bit confusing, but do correspond
// to the symlink() system call.  The 'path' is where the link points,
// while the 'link' is the link itself.  So we need to leave the path
// unaltered, but insert the link into the mounted directory.
int tmpfs_symlink(const char *path, const char *link)
{
    char flink[PATH_MAX];

    log_msg("\nbb_symlink(path=\"%s\", link=\"%s\")\n",
           path, link);
    assert(0);

    return log_syscall("symlink", symlink(path, flink), 0);
}

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

/** Change the permission bits of a file */
int tmpfs_chmod(const char *path, mode_t mode)
{
    char fpath[PATH_MAX];

    log_msg("\nbb_chmod(fpath=\"%s\", mode=0%03o)\n",
           path, mode);
    assert(0);

    return log_syscall("chmod", chmod(fpath, mode), 0);
}

/** Change the owner and group of a file */
int tmpfs_chown(const char *path, uid_t uid, gid_t gid)

{
    char fpath[PATH_MAX];

    log_msg("\nbb_chown(path=\"%s\", uid=%d, gid=%d)\n",
           path, uid, gid);
    assert(0);

    return log_syscall("chown", chown(fpath, uid, gid), 0);
}

/** Change the size of a file */
int tmpfs_truncate(const char *path, off_t newsize)
{
    char fpath[PATH_MAX];

    log_msg("\nbb_truncate(path=\"%s\", newsize=%lld)\n",
           path, newsize);
    assert(0);

    return log_syscall("truncate", truncate(fpath, newsize), 0);
}

/** Change the access and/or modification times of a file */
/* note -- I'll want to change this as soon as 2.6 is in debian testing */
int tmpfs_utime(const char *path, struct utimbuf *ubuf)
{
    char fpath[PATH_MAX];

    log_msg("\nbb_utime(path=\"%s\", ubuf=0x%08x)\n",
           path, ubuf);
    assert(0);

    return log_syscall("utime", utime(fpath, ubuf), 0);
}

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

/** Get file system statistics
 *
 * The 'f_frsize', 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
 *
 * Replaced 'struct statfs' parameter with 'struct statvfs' in
 * version 2.5
 */
int tmpfs_statfs(const char *path, struct statvfs *statv)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    log_msg("\nbb_statfs(path=\"%s\", statv=0x%08x)\n",
           path, statv);
    assert(0);

    // get stats for underlying filesystem
    retstat = log_syscall("statvfs", statvfs(fpath, statv), 0);

    log_statvfs(statv);

    return retstat;
}

/** Possibly flush cached data
 *
 * BIG NOTE: This is not equivalent to fsync().  It's not a
 * request to sync dirty data.
 *
 * Flush is called on each close() of a file descriptor.  So if a
 * filesystem wants to return write errors in close() and the file
 * has cached dirty data, this is a good place to write back data
 * and return any errors.  Since many applications ignore close()
 * errors this is not always useful.
 *
 * NOTE: The flush() method may be called more than once for each
 * open().  This happens if more than one file descriptor refers
 * to an opened file due to dup(), dup2() or fork() calls.  It is
 * not possible to determine if a flush is final, so each flush
 * should be treated equally.  Multiple write-flush sequences are
 * relatively rare, so this shouldn't be a problem.
 *
 * Filesystems shouldn't assume that flush will always be called
 * after some writes, or that if will be called at all.
 *
 * Changed in version 2.2
 */
// this is a no-op in BBFS.  It just logs the call and returns success
int tmpfs_flush(const char *path, struct fuse_file_info *fi)
{
    log_msg("\nbb_flush(path=\"%s\", fi=0x%08x)\n", path, fi);
    // no need to get fpath on this one, since I work from fi->fh not the path
    log_fi(fi);

    return 0;
}

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

/** Synchronize file contents
 *
 * If the datasync parameter is non-zero, then only the user data
 * should be flushed, not the meta data.
 *
 * Changed in version 2.2
 */
int tmpfs_fsync(const char *path, int datasync, struct fuse_file_info *fi)
{
    log_msg("\nbb_fsync(path=\"%s\", datasync=%d, fi=0x%08x)\n",
           path, datasync, fi);
    log_fi(fi);

    // some unix-like systems (notably freebsd) don't have a datasync call
#ifdef HAVE_FDATASYNC
    if (datasync)
       return log_syscall("fdatasync", fdatasync(fi->fh), 0);
    else
#endif
       return log_syscall("fsync", fsync(fi->fh), 0);
}

#ifdef HAVE_SYS_XATTR_H
/** Note that my implementations of the various xattr functions use
    the 'l-' versions of the functions (eg bb_setxattr() calls
    lsetxattr() not setxattr(), etc).  This is because it appears any
    symbolic links are resolved before the actual call takes place, so
    I only need to use the system-provided calls that don't follow
    them */

/** Set extended attributes */
int tmpfs_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
    char fpath[PATH_MAX];

    log_msg("\nbb_setxattr(path=\"%s\", name=\"%s\", value=\"%s\", size=%d, flags=0x%08x)\n",
           path, name, value, size, flags);
    assert(0);

    return log_syscall("lsetxattr", lsetxattr(fpath, name, value, size, flags), 0);
}

/** Get extended attributes */
int tmpfs_getxattr(const char *path, const char *name, char *value, size_t size)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    log_msg("\nbb_getxattr(path = \"%s\", name = \"%s\", value = 0x%08x, size = %d)\n",
           path, name, value, size);
    assert(0);

    retstat = log_syscall("lgetxattr", lgetxattr(fpath, name, value, size), 0);
    if (retstat >= 0)
       log_msg("    value = \"%s\"\n", value);

    return retstat;
}

/** List extended attributes */
int tmpfs_listxattr(const char *path, char *list, size_t size)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    char *ptr;

    log_msg("\nbb_listxattr(path=\"%s\", list=0x%08x, size=%d)\n",
           path, list, size
           );
    assert(0);

    retstat = log_syscall("llistxattr", llistxattr(fpath, list, size), 0);
    if (retstat >= 0) {
       log_msg("    returned attributes (length %d):\n", retstat);
       if (list != NULL)
           for (ptr = list; ptr < list + retstat; ptr += strlen(ptr)+1)
              log_msg("    \"%s\"\n", ptr);
       else
           log_msg("    (null)\n");
    }

    return retstat;
}

/** Remove extended attributes */
int tmpfs_removexattr(const char *path, const char *name)
{
    char fpath[PATH_MAX];

    log_msg("\nbb_removexattr(path=\"%s\", name=\"%s\")\n",
           path, name);
    assert(0);

    return log_syscall("lremovexattr", lremovexattr(fpath, name), 0);
}
#endif

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
    struct inode* inode = resolve_inode(path, -1);
    fi->fh = inode;
    log_msg("    opendir returned 0x%p\n", inode);

    // TODO better catch error reasons
    if (inode == 0) {
        return 1;
    }

    return 0;
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

/** Synchronize directory contents
 *
 * If the datasync parameter is non-zero, then only the user data
 * should be flushed, not the meta data
 *
 * Introduced in version 2.3
 */
// when exactly is this called?  when a user calls fsync and it
// happens to be a directory? ??? >>> I need to implement this...
int tmpfs_fsyncdir(const char *path, int datasync, struct fuse_file_info *fi)
{
    int retstat = 0;

    log_msg("\nbb_fsyncdir(path=\"%s\", datasync=%d, fi=0x%08x)\n",
           path, datasync, fi);
    log_fi(fi);

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

    struct inode* inode = resolve_inode(path, -1);
    return inode->stat.st_mode & mask;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
// Not implemented.  I had a version that used creat() to create and
// open the file, which it turned out opened the file write-only.

/**
 * Change the size of an open file
 *
 * This method is called instead of the truncate() method if the
 * truncation was invoked from an ftruncate() system call.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the truncate() method will be
 * called instead.
 *
 * Introduced in version 2.5
 */
int tmpfs_ftruncate(const char *path, off_t offset, struct fuse_file_info *fi)
{
    int retstat = 0;

    log_msg("\nbb_ftruncate(path=\"%s\", offset=%lld, fi=0x%08x)\n",
           path, offset, fi);
    log_fi(fi);

    retstat = ftruncate(fi->fh, offset);
    if (retstat < 0)
       retstat = log_error("bb_ftruncate ftruncate");

    return retstat;
}

/**
 * Get attributes from an open file
 *
 * This method is called instead of the getattr() method if the
 * file information is available.
 *
 * Currently this is only called after the create() method if that
 * is implemented (see above).  Later it may be called for
 * invocations of fstat() too.
 *
 * Introduced in version 2.5
 */
int tmpfs_fgetattr(const char *path, struct stat *statbuf, struct fuse_file_info *fi)
{
    int retstat = 0;

    log_msg("\nbb_fgetattr(path=\"%s\", statbuf=0x%08x, fi=0x%08x)\n",
           path, statbuf, fi);
    log_fi(fi);

    // On FreeBSD, trying to do anything with the mountpoint ends up
    // opening it, and then using the FD for an fgetattr.  So in the
    // special case of a path of "/", I need to do a getattr on the
    // underlying root directory instead of doing the fgetattr().
    if (!strcmp(path, "/"))
       return tmpfs_getattr(path, statbuf);

    retstat = fstat(fi->fh, statbuf);
    if (retstat < 0)
       retstat = log_error("bb_fgetattr fstat");

    log_stat(statbuf);

    return retstat;
}

struct fuse_operations tmpfs_oper = {
  .getattr = tmpfs_getattr,
  .opendir = tmpfs_opendir,
  .readlink = tmpfs_readlink,
  // no .getdir -- that's deprecated
  .getdir = NULL,
  .mknod = tmpfs_mknod,
  .mkdir = tmpfs_mkdir,
  .unlink = tmpfs_unlink,
  .rmdir = tmpfs_rmdir,
  .symlink = tmpfs_symlink,
  .rename = tmpfs_rename,
  .link = tmpfs_link,
  .chmod = tmpfs_chmod,
  .chown = tmpfs_chown,
  .truncate = tmpfs_truncate,
  .utime = tmpfs_utime,
  .open = tmpfs_open,
  .read = tmpfs_read,
  .write = tmpfs_write,
  /** Just a placeholder, don't set */ // huh???
  .statfs = tmpfs_statfs,
  .flush = tmpfs_flush,
  .release = tmpfs_release,
  .fsync = tmpfs_fsync,

#ifdef HAVE_SYS_XATTR_H
  .setxattr = tmpfs_setxattr,
  .getxattr = tmpfs_getxattr,
  .listxattr = tmpfs_listxattr,
  .removexattr = tmpfs_removexattr,
#endif

  .readdir = tmpfs_readdir,
  .fsyncdir = tmpfs_fsyncdir,
  .init = tmpfs_init,
  .destroy = tmpfs_destroy,
  .access = tmpfs_access,
  .ftruncate = tmpfs_ftruncate,
  .fgetattr = tmpfs_fgetattr
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
