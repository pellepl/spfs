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

#include "cfs_sillyfs.h"
#include "sillyfs.h"

static int _mount(void *wfs, void *cfg) {
  return sfs_mount((sfs_t *)wfs);
}
static int _umount(void *wfs) {
  return 0;
}
static int _creat(void *wfs, const char *path) {
  return sfs_creat((sfs_t *)wfs, path);
}
static int _open(void *wfs, const char *path, uint32_t oflags){
  return sfs_open((sfs_t *)wfs, path, oflags);
}
static int _read(void *wfs, int fh, void *buf, uint32_t sz){
  return sfs_read((sfs_t *)wfs, fh, buf, sz);
}
static int _write(void *wfs, int fh, const void *buf, uint32_t sz){
  return sfs_write((sfs_t *)wfs, fh, buf, sz);
}
static int _truncate(void *wfs, const char *path, uint32_t sz){
  return sfs_truncate((sfs_t *)wfs, path, sz);
}
static int _ftruncate(void *wfs, int fh, uint32_t sz){
  return sfs_ftruncate((sfs_t *)wfs, fh, sz);
}
static int _close(void *wfs, int fh){
  return sfs_close((sfs_t *)wfs, fh);
}
static int _lseek(void *wfs, int fh, int offs, int whence){
  return sfs_lseek((sfs_t *)wfs, fh, offs, whence);
}
static int _ftell(void *wfs, int fh){
  return sfs_ftell((sfs_t *)wfs, fh);
}
static int _stat(void *wfs, const char *path, struct cfs_stat *buf){
  int res;
  struct sfs_stat s;
  res = sfs_stat((sfs_t *)wfs, path, &s);
  strncpy(buf->name, s.name, sizeof(buf->name));
  buf->size = s.size;
  return res;
}
static int _remove(void *wfs, const char *path){
  return sfs_remove((sfs_t *)wfs, path);
}
static int _rename(void *wfs, const char *old_path, const char *new_path){
  return sfs_rename((sfs_t *)wfs, old_path, new_path);
}
#define DIR_POOL_ENTRIES 8
static sfs_DIR _dpool[DIR_POOL_ENTRIES];
static int _dpool_used[DIR_POOL_ENTRIES] = {0};
static int _opendir(void *wfs, cfs_DIR *d, const char *path) {
  sfs_DIR *sfsd = NULL;
  int i;
  for (i = 0; i < DIR_POOL_ENTRIES; i++){
    if (_dpool_used[i] == 0) {
      sfsd = &_dpool[i];
      _dpool_used[i] = 1;
      break;
    }
  }
  if (i == DIR_POOL_ENTRIES) return -ENOMEM;
  d->user = sfsd;
  return sfs_opendir((sfs_t *)wfs, (sfs_DIR *)d->user, path);
}
static int _readdir(void *wfs, cfs_DIR *d) {
  struct sfs_dirent *s = sfs_readdir((sfs_t *)wfs, (sfs_DIR *)d->user);
  if (s) {
    strncpy(d->de.name, s->name, sizeof(d->de.name));
    d->de.size = s->size;
    return 0;
  }
  return -ENOENT;
}
static int _closedir(void *wfs, cfs_DIR *d) {
  int i;
  for (i = 0; i < DIR_POOL_ENTRIES; i++) {
    if (_dpool_used[i] && &_dpool[i] == d->user) {
      _dpool_used[i] = 0;
      break;
    }
  }
  return 0;
}

static void _free(void *wfs) {
  sfs_destroy((sfs_t *)wfs);
}
static uint32_t _translate_oflags(void *wfs, uint32_t oflags) {
  uint32_t cfs_oflags = 0;
  if ((oflags & CFS_O_APPEND) == CFS_O_APPEND)  cfs_oflags |= SFS_O_APPEND;
  if ((oflags & CFS_O_CREAT)  == CFS_O_CREAT)   cfs_oflags |= SFS_O_CREAT;
  if ((oflags & CFS_O_EXCL)   == CFS_O_EXCL)    cfs_oflags |= SFS_O_EXCL;
  if ((oflags & CFS_O_RDONLY) == CFS_O_RDONLY)  cfs_oflags |= SFS_O_RDONLY;
  if ((oflags & CFS_O_RDWR)   == CFS_O_RDWR)    cfs_oflags |= SFS_O_RDWR;
  if ((oflags & CFS_O_TRUNC)  == CFS_O_TRUNC)   cfs_oflags |= SFS_O_TRUNC;
  if ((oflags & CFS_O_WRONLY) == CFS_O_WRONLY)  cfs_oflags |= SFS_O_WRONLY;
  return cfs_oflags;
}
static uint32_t _translate_whence(void *wfs, uint32_t whence) {
  switch (whence) {
  case CFS_SEEK_CUR: return SFS_SEEK_CUR;
  case CFS_SEEK_SET: return SFS_SEEK_SET;
  case CFS_SEEK_END: return SFS_SEEK_END;
  }
  return 0;
}

static int _translate_err(void *wfs, int err) {
  return err;
}

void csfs_link(cfs_t *cfs, uint32_t max_size) {
  cfs_fs_t *f = malloc(sizeof(cfs_fs_t));
  memset(f, 0, sizeof(cfs_fs_t));
  f->fs = sfs_config(max_size);
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
  cfs_link_fs(cfs, "sillyfs", f);
}
