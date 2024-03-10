/*
  Copyright (C) 2012 Joseph J. Pfeiffer, Jr., Ph.D. <pfeiffer@cs.nmsu.edu>

  This program can be distributed under the terms of the GNU GPLv3.
  See the file COPYING.

  There are a couple of symbols that need to be #defined before
  #including all the headers.
*/

#ifndef _PARAMS_H_
#define _PARAMS_H_

// The FUSE API has been changed a number of times.  So, our code
// needs to define the version of the API that we assume.  As of this
// writing, the most current API version is 26
#define FUSE_USE_VERSION 26

// need this to get pwrite().  I have to use setvbuf() instead of
// setlinebuf() later in consequence.
#define _XOPEN_SOURCE 500

#define NAME_LEN 255
#define INODES_LIMIT 128
#define INODES_IN_DIRECTORY 32

#include <sys/stat.h>
#include <stdlib.h>

struct inode {
    struct stat stat;
    void* data_ptr; // struct dir if directory, struct reg if regular file
    struct inode *parent;
    char is_active;
    size_t open_count;
};

struct dentry {
    char name[NAME_LEN];
    struct inode *inode;
    char is_active;
};

struct dir {
    struct dentry entries[INODES_IN_DIRECTORY];
};

#include <limits.h>
#include <stdio.h>
struct tmpfs_state {
    struct inode inodes[INODES_LIMIT];
};

#define TMPFS_DATA ((struct tmpfs_state*) fuse_get_context()->private_data)

#endif
