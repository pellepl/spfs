/*
 * cfs_sillyfs.c
 *
 *  Created on: Sep 15, 2017
 *      Author: petera
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>

#include "cfs_posix.h"

#ifndef DEFAULT_PATH
#define DEFAULT_PATH "/dev/shm/cfs/test-data/"
#endif
#define MAX_PATH_LEN 512

// taken from http://stackoverflow.com/questions/675039/how-can-i-create-directory-tree-in-c-linux
// thanks Jonathan Leffler

static int do_mkdir(const char *path, mode_t mode)
{
  struct stat st;
  int status = 0;

  if (stat(path, &st) != 0) {
    /* Directory does not exist. EEXIST for race condition */
    if (mkdir(path, mode) != 0 && errno != EEXIST) {
      status = -1;
    }
  } else if (!S_ISDIR(st.st_mode)) {
    errno = ENOTDIR;
    status = -1;
  }

  return status;
}

/**
** mkpath - ensure all directories in path exist
** Algorithm takes the pessimistic view and works top-down to ensure
** each directory in path exists, rather than optimistically creating
** the last element and working backwards.
*/
static int mkpath(const char *path, mode_t mode) {
  char *pp;
  char *sp;
  int status;
  char *copypath = strdup(path);

  status = 0;
  pp = copypath;
  while (status == 0 && (sp = strchr(pp, '/')) != 0) {
    if (sp != pp)  {
      /* Neither root nor double slash in path */
      *sp = '\0';
      status = do_mkdir(copypath, mode);
      *sp = '/';
    }
    pp = sp + 1;
  }
  if (status == 0) {
    status = do_mkdir(path, mode);
  }
  free(copypath);
  return status;
}

// end take

static int _mount(void *wfs, void *cfg) {
  if (mkpath((char *)wfs, 0755)) {
    printf("could not create path %s\n", (char *)wfs);
    return errno;
  }
  return 0;
}
static int _umount(void *wfs) {
  return 0;
}
static int _creat(void *wfs, const char *path) {
  char _path[MAX_PATH_LEN];
  snprintf(_path, MAX_PATH_LEN, "%s/%s", (char *)wfs, path);
  return creat(_path, 0644);
}
static int _open(void *wfs, const char *path, uint32_t oflags){
  char _path[MAX_PATH_LEN];
  snprintf(_path, MAX_PATH_LEN, "%s/%s", (char *)wfs, path);
//  printf("posix open %s %s%s%s%s%s%s%s\n",
//         path,
//         oflags & O_APPEND ? "APPEND " : "",
//         oflags & O_CREAT ? "CREAT " : "",
//         oflags & O_TRUNC ? "TRUNC " : "",
//         oflags & O_RDONLY ? "RDONLY " : "",
//         oflags & O_WRONLY ? "WRONLY " : "",
//         oflags & O_RDWR ? "RDWR " : "",
//         oflags & O_EXCL ? "EXCL " : ""
//         );
  int res = open(_path, oflags, S_IRUSR | S_IWUSR);
  return res;
}
static int _read(void *wfs, int fh, void *buf, uint32_t sz){
  return read(fh, buf ,sz);
}
static int _write(void *wfs, int fh, const void *buf, uint32_t sz) {
  errno=0;
  int res = write(fh, buf, sz);
  return res;
}
static int _truncate(void *wfs, const char *path, uint32_t sz){
  char _path[MAX_PATH_LEN];
  snprintf(_path, MAX_PATH_LEN, "%s/%s", (char *)wfs, path);
  struct stat s;
  int res = stat(_path, &s);
  if (res) return -1;
  // emulate spfs behaviour, clamp trunc beyond end
  if (sz > s.st_size) sz = s.st_size;
  return truncate(_path, sz);
}
static int _ftruncate(void *wfs, int fh, uint32_t sz){
  struct stat s;
  int res = fstat(fh, &s);
  if (res) return -1;
  // emulate spfs behaviour, clamp trunc beyond end
  if (sz > s.st_size) sz = s.st_size;
  return ftruncate(fh, sz);
}
static int _close(void *wfs, int fh){
  return close(fh);
}
static int _lseek(void *wfs, int fh, int offs, int whence){
  struct stat s;
  int res = fstat(fh, &s);
  if (res) return -1;
  int pos = lseek(fh, 0, SEEK_CUR);
  switch (whence) {
  case SEEK_CUR: pos += offs; break;
  case SEEK_SET: pos = offs; break;
  case SEEK_END: pos = s.st_size + offs; break;
  }
  if (pos > s.st_size) {
    // emulate spfs behaviour, clamp seeking beyond end
    return lseek(fh, 0, SEEK_END);
  } else {
    return lseek(fh, offs, whence);
  }
}
static int _ftell(void *wfs, int fh){
  return lseek(fh, 0, SEEK_CUR );
}
static int _stat(void *wfs, const char *path, struct cfs_stat *buf){
  char _path[MAX_PATH_LEN];
  snprintf(_path, MAX_PATH_LEN, "%s/%s", (char *)wfs, path);
  struct stat s;
  int res = stat(_path, &s);
  strncpy(buf->name, path, sizeof(buf->name));
  buf->size = s.st_size;
  return res;
}
static int _remove(void *wfs, const char *path){
  char _path[MAX_PATH_LEN];
  snprintf(_path, MAX_PATH_LEN, "%s/%s", (char *)wfs, path);
  return remove(_path);
}
static int _rename(void *wfs, const char *old_path, const char *new_path){
  char _path1[MAX_PATH_LEN];
  snprintf(_path1, MAX_PATH_LEN, "%s/%s", (char *)wfs, old_path);
  char _path2[MAX_PATH_LEN];
  snprintf(_path2, MAX_PATH_LEN, "%s/%s", (char *)wfs, new_path);
  return rename(_path1, _path2);
}
static int _opendir(void *wfs, cfs_DIR *d, const char *path) {
  char _path[MAX_PATH_LEN];
  snprintf(_path, MAX_PATH_LEN, "%s/%s", (char *)wfs, path);
  DIR *dp = opendir(_path);
  d->user = dp;
  return dp ? 0 : errno;
}
static int _readdir(void *wfs, cfs_DIR *d) {
  struct dirent *de;
  struct stat s;
  do {
    de = readdir((DIR *)d->user);
    if (de == NULL || !(strcmp(".", de->d_name) == 0 || strcmp("..", de->d_name) == 0)) {
      break;
    }
  } while (1);
  if (de) {
    char _path[MAX_PATH_LEN];
    snprintf(_path, MAX_PATH_LEN, "%s/%s", (char *)wfs, de->d_name);
    stat(_path, &s);

    strncpy(d->de.name, de->d_name, sizeof(d->de.name));
    d->de.size = s.st_size;
    return 0;
  }
  return -ENOENT;
}
static int _closedir(void *wfs, cfs_DIR *d) {
  return closedir((DIR *)d->user);
}
static void _free(void *wfs) {
  DIR *dp;
  struct dirent *ep;
  dp = opendir((char *)wfs);

  if (dp != NULL) {
    while ((ep = readdir(dp))) {
      char _path[MAX_PATH_LEN];
      if (ep->d_name[0] != '.') {
        snprintf(_path, MAX_PATH_LEN, "%s/%s", (char *)wfs, ep->d_name);
        (void)remove(_path);
      }
    }
    closedir(dp);
  }
  free(wfs);
}
static uint32_t _translate_oflags(void *wfs, uint32_t oflags) {
  uint32_t cfs_oflags = 0;
  if ((oflags & CFS_O_APPEND) == CFS_O_APPEND)  cfs_oflags |= O_APPEND;
  if ((oflags & CFS_O_CREAT)  == CFS_O_CREAT)   cfs_oflags |= O_CREAT;
  if ((oflags & CFS_O_EXCL)   == CFS_O_EXCL)    cfs_oflags |= O_EXCL;
  if ((oflags & CFS_O_RDONLY) == CFS_O_RDONLY)  cfs_oflags |= O_RDONLY;
  if ((oflags & CFS_O_RDWR)   == CFS_O_RDWR)    cfs_oflags |= O_RDWR;
  if ((oflags & CFS_O_TRUNC)  == CFS_O_TRUNC)   cfs_oflags |= O_TRUNC;
  if ((oflags & CFS_O_WRONLY) == CFS_O_WRONLY)  cfs_oflags |= O_WRONLY;
  return cfs_oflags;
}
static uint32_t _translate_whence(void *wfs, uint32_t whence) {
  switch (whence) {
  case CFS_SEEK_CUR: return SEEK_CUR;
  case CFS_SEEK_SET: return SEEK_SET;
  case CFS_SEEK_END: return SEEK_END;
  }
  return 0;
}

static int _translate_err(void *wfs, int err) {
  return -errno;
}

void cposfs_link(cfs_t *cfs, const char *test_path) {
  cfs_fs_t *f = malloc(sizeof(cfs_fs_t));
  memset(f, 0, sizeof(cfs_fs_t));
  char *p = malloc(MAX_PATH_LEN);
  memset(p, 0, MAX_PATH_LEN);

  strncpy(p, test_path ? test_path : DEFAULT_PATH, MAX_PATH_LEN);

  f->fs = p;

  f->close = _close;
  f->creat = _creat;
  f->free = _free;
  f->ftell = _ftell;
  f->ftruncate = _ftruncate;
  f->lseek = _lseek;
  f->mount = _mount;
  f->open = _open;
  f->read = _read;
  f->remove = _remove;
  f->rename = _rename;
  f->stat = _stat;
  f->truncate = _truncate;
  f->umount = _umount;
  f->write = _write;
  f->opendir = _opendir;
  f->readdir = _readdir;
  f->closedir = _closedir;
  f->translate_err = _translate_err;
  f->translate_oflags = _translate_oflags;
  f->translate_whence = _translate_whence;
  cfs_link_fs(cfs, "posixfs", f);
}
