/*
 * mount.c
 *
 *  Created on: Sep 22, 2017
 *      Author: petera
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "spfs_lowlevel.h"
#include "spfs_dbg.h"


static const char *hello_str = "Hello World!\n";
static const char *hello_path = "/hello";

static int hello_getattr(const char *path, struct stat *stbuf) {
  int res = 0;
  memset(stbuf, 0, sizeof(struct stat));
  if (strcmp(path, "/") == 0) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
  } else if (strcmp(path, hello_path) == 0) {
    stbuf->st_mode = S_IFREG | 0444;
    stbuf->st_nlink = 1;
    stbuf->st_size = strlen(hello_str);
  } else
    res = -ENOENT;

  return res;
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
  (void) offset;
  (void) fi;

  if (strcmp(path, "/") != 0) return -ENOENT;

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);
  filler(buf, hello_path + 1, NULL, 0);

  return 0;
}

static int hello_open(const char *path, struct fuse_file_info *fi) {
  if (strcmp(path, hello_path) != 0) return -ENOENT;

  if ((fi->flags & 3) != O_RDONLY) return -EACCES;

  return 0;
}

static int hello_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  size_t len;
  (void) fi;
  if (strcmp(path, hello_path) != 0) return -ENOENT;

  len = strlen(hello_str);
  if (offset < (off_t) len) {
    if (offset + size > len)
      size = len - offset;
    memcpy(buf, hello_str + offset, size);
  } else
    size = 0;

  return size;
}

static struct fuse_operations hello_oper = {
  .open = hello_open,
  .read = hello_read,
  .getattr = hello_getattr,
  .readdir = hello_readdir
};

int main(int argc, char *argv[]) {
  printf("spfs mount [fs v%d.%d.%d]\n",
      (SPFS_VERSION >> 12), (SPFS_VERSION >> 8) & 0xf, SPFS_VERSION & 0xff);
  if (argc < 2) {
    printf("Mounts a spfs image to a folder using fuse.\n");
    printf("usage:\n%s <image file> <mount path> (fuse params)\n", argv[0]);
    return 1;
  }
  return fuse_main(argc-1, &argv[1], &hello_oper, NULL);
}
