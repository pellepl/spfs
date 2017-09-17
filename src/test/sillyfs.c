/*
 * sillyfs.c
 *
 *  Created on: Sep 14, 2017
 *      Author: petera
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "sillyfs.h"

sfs_t *sfs_config(uint32_t max_size) {
  sfs_t *fs = malloc(sizeof(sfs_t));
  if (fs) {
    memset(fs, 0, sizeof(sfs_t));
    fs->size = max_size;
  }
  return fs;
}

int sfs_mount(sfs_t *fs) {
  if (fs == NULL) return -EINVAL;
  return 0;
}

void sfs_format(sfs_t *fs) {
  sfs_fd_t *fd = fs->fdhead;
  while (fd) {
    sfs_fd_t *nfd = fd->_next;
    free(fd);
    fd = nfd;
  }
  fs->fdhead = NULL;

  sfs_file_t *f = fs->fhead;
  while (f) {
    sfs_file_t *nf = f->_next;
    free(f->name);
    free(f->data);
    free(f);
    f = nf;
  }
  fs->fhead = NULL;
}

void sfs_destroy(sfs_t *fs) {
  sfs_format(fs);
  free(fs);
}

static sfs_file_t *_file_create(sfs_t *fs, const char *name) {
  sfs_file_t *f = malloc(sizeof(sfs_file_t));
  char *fname = malloc(strlen(name) + 1);
  if (f && fname) {
    memset(f, 0, sizeof(sfs_file_t));
    strcpy(fname, name);
    f->name = fname;
    if (fs->fhead) fs->fhead->_prev = f;
    f->_next = fs->fhead;
    fs->fhead = f;
  } else {
    free(f);
    f = NULL;
  }
  return f;
}

static void _file_destroy(sfs_t *fs, sfs_file_t *f) {
  if (f->_next) f->_next->_prev = f->_prev;
  if (f->_prev) f->_prev->_next = f->_next;
  else          fs->fhead = f->_next;
  fs->used_size -= f->f_size;
  free(f->name);
  free(f->data);
  free(f);
}

static sfs_file_t *_file_find(sfs_t *fs, const char *name) {
  sfs_file_t *f = fs->fhead;
  while (f) {
    if (strcmp(name, f->name) == 0) return f;
    f = f->_next;
  }
  return NULL;
}

static int _file_sizeify(sfs_t *fs, sfs_file_t *f, uint32_t new_size) {
  uint32_t m_size = ((new_size + (SFS_CHUNK - 1)) / SFS_CHUNK) * SFS_CHUNK;
  if (m_size != f->m_size) {
    void *new_data;
    if (f->data) {
      new_data = realloc(f->data, m_size);
    } else {
      new_data = malloc(m_size);
    }
    if (new_data == NULL) return -ENOMEM;
    f->m_size = m_size;
    f->data = new_data;
  }
  return 0;
}

static sfs_fd_t *_fd_create(sfs_t *fs) {
  sfs_fd_t *fd = malloc(sizeof(sfs_fd_t));
  if (fd) {
    memset(fd, 0, sizeof(sfs_fd_t));
    fs->fh_cnt++;
    fd->fh = fs->fh_cnt;
    if (fs->fdhead) fs->fdhead->_prev = fd;
    fd->_next = fs->fdhead;
    fs->fdhead = fd;
  }
  return fd;
}

static void _fd_destroy(sfs_t *fs, sfs_fd_t *fd) {
  if (fd->_next) fd->_next->_prev = fd->_prev;
  if (fd->_prev) fd->_prev->_next = fd->_next;
  else           fs->fdhead = fd->_next;
  free(fd);
}

static sfs_fd_t *_fd_find(sfs_t *fs, int fh) {
  sfs_fd_t *fd = fs->fdhead;
  while (fd) {
    if (fd->fh == fh) return fd;
    fd = fd->_next;
  }
  return NULL;
}

int sfs_creat(sfs_t *fs, const char *path) {
  sfs_fd_t *fd = _fd_create(fs);
  if (fd == NULL) return -ENOMEM;
  sfs_file_t *f = _file_create(fs, path);
  if (f == NULL) {
    _fd_destroy(fs, fd);
    return -ENOMEM;
  }
  fd->f = f;
  fd->oflags = SFS_O_WRONLY;
  return fd->fh;
}

int sfs_open(sfs_t *fs, const char *path, uint32_t oflags) {
  sfs_file_t *f_exist = _file_find(fs, path);
  sfs_file_t *f = NULL;
  sfs_fd_t *fd = _fd_create(fs);
  if (fd == NULL) return -ENOMEM;
  if ((oflags & SFS_O_CREAT) == 0) {
    if (f_exist == NULL) return -ENOENT;
  }
  if ((oflags & (SFS_O_CREAT | SFS_O_EXCL)) == (SFS_O_CREAT | SFS_O_EXCL)) {
    // fail if exists
    if (f_exist) return -EEXIST;
  } else if (oflags & SFS_O_CREAT) {
    if (f_exist == NULL) {
      f = _file_create(fs, path);
      if (f == NULL) return -ENOMEM;
      oflags &= ~SFS_O_TRUNC; // no need truncing a newly created file
    } else {
      f = f_exist;
    }
  }
  if ((oflags & (SFS_O_TRUNC | SFS_O_WRONLY)) == (SFS_O_TRUNC | SFS_O_WRONLY) && f_exist) {
    f_exist->f_size = 0;
    f = f_exist;
  }
  fd->f = f;
  fd->oflags = oflags;
  return fd->fh;
}

int sfs_read(sfs_t *fs, int fh, void *buf, uint32_t sz) {
  sfs_fd_t *fd = _fd_find(fs, fh);
  if (fd == NULL) return -EBADFD;
  if ((fd->oflags & SFS_O_RDONLY) == 0) return -EINVAL;
  int rd_sz = sz;
  if (fd->offs >= fd->f->f_size) {
    return 0;
  } else if (fd->offs + sz > fd->f->f_size) {
    rd_sz = fd->f->f_size - fd->offs;
  }
  memcpy(buf, &fd->f->data[fd->offs], rd_sz);
  fd->offs += rd_sz;
  return rd_sz;
}

int sfs_write(sfs_t *fs, int fh, const void *buf, uint32_t sz) {
  sfs_fd_t *fd = _fd_find(fs, fh);
  if (fd == NULL) return -EBADFD;
  if ((fd->oflags & SFS_O_WRONLY) == 0) return -EINVAL;
  if (sz == 0) return 0;
  uint32_t offs = fd->offs;
  if (fd->oflags & SFS_O_APPEND) offs = fd->f->f_size;
  if (sz > fs->size - fs->used_size) sz = fs->size - fs->used_size;
  if (sz == 0) return -ENOSPC;
  int res = _file_sizeify(fs, fd->f, offs + sz);
  if (res) return res;
  memcpy(&fd->f->data[offs], buf, sz);
  if (offs + sz > fd->f->f_size) fd->f->f_size = offs + sz;
  fd->offs = offs + sz;
  fs->used_size += sz;
  return sz;
}

int sfs_close(sfs_t *fs, int fh) {
  sfs_fd_t *fd = _fd_find(fs, fh);
  if (fd == NULL) return -EBADFD;
  _fd_destroy(fs, fd);
  return 0;
}

int sfs_lseek(sfs_t *fs, int fh, int offs, int whence) {
  sfs_fd_t *fd = _fd_find(fs, fh);
  if (fd == NULL) return -EBADFD;
  uint32_t new_offs;
  switch (whence) {
  case SFS_SEEK_SET: new_offs = offs; break;
  case SFS_SEEK_CUR: new_offs = fd->offs + offs; break;
  case SFS_SEEK_END: new_offs = fd->f->f_size + offs; break;
  default: return -EINVAL;
  }
  // clamp
  if (new_offs > fd->f->f_size) new_offs = fd->f->f_size;
  fd->offs = new_offs;
  return new_offs;
}

int sfs_ftell(sfs_t *fs, int fh) {
  sfs_fd_t *fd = _fd_find(fs, fh);
  if (fd == NULL) return -EBADFD;
  return fd->offs;
}

int sfs_remove(sfs_t *fs, const char *path) {
  sfs_file_t *f = _file_find(fs, path);
  if (f == NULL) return -ENOENT;
  sfs_fd_t *fd = fs->fdhead;
  while (fd) {
    sfs_fd_t *fdn = fd->_next;
    if (fd->f == f) _fd_destroy(fs, fd);
    fd = fdn;
  }
  _file_destroy(fs, f);
  return 0;
}

int sfs_rename(sfs_t *fs, const char *old_path, const char *new_path) {
  sfs_file_t *f = _file_find(fs, old_path);
  if (f) return -ENOENT;
  sfs_file_t *f_exists = _file_find(fs, new_path);
  if (f_exists) (void)sfs_remove(fs, new_path);
  strcpy(f->name, new_path);
  return 0;
}

int sfs_stat(sfs_t *fs, const char *path, struct sfs_stat *buf) {
  sfs_file_t *f = _file_find(fs, path);
  if (f == NULL) return -ENOENT;
  buf->size = f->f_size;
  strncpy(buf->name, f->name, sizeof(buf->name));
  return 0;
}

static int _trunc(sfs_t *fs, sfs_file_t *f, uint32_t sz) {
  if (sz >= f->f_size) return 0; // non-posix cap
  int res = _file_sizeify(fs, f, sz);
  if (res) return res;
  f->f_size = sz;
  sfs_fd_t *fd = fs->fdhead;
  while (fd) {
    if (fd->offs > sz) fd->offs = sz;
    fd = fd->_next;
  }
  return 0;
}

int sfs_truncate(sfs_t *fs, const char *path, uint32_t sz) {
  sfs_file_t *f = _file_find(fs, path);
  if (f == NULL) return -ENOENT;
  return _trunc(fs, f, sz);
}

int sfs_ftruncate(sfs_t *fs, int fh, uint32_t sz) {
  sfs_fd_t *fd = _fd_find(fs, fh);
  if (fd == NULL) return -EBADFD;
  if ((fd->oflags & SFS_O_WRONLY) == 0) return -EINVAL;
  return _trunc(fs, fd->f, sz);
}

int sfs_opendir(sfs_t *fs, sfs_DIR *d, const char *path) {
  (void)path;
  if (d == NULL) return -EINVAL;
  d->_f = fs->fhead;
  return 0;
}

struct sfs_dirent *sfs_readdir(sfs_t *fs, sfs_DIR *d) {
  if (d == NULL) return NULL;
  if (d->_f == NULL) return NULL;
  d->de.size = d->_f->f_size;
  strncpy(d->de.name, d->_f->name, sizeof(d->de.name));
  d->_f = d->_f->_next;
  return &d->de;
}
