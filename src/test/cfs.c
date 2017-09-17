/*
 * combinedfs.c
 *
 *  Created on: Sep 15, 2017
 *      Author: petera
 */


#include "cfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

static uint32_t map_put(map_t *map, uint32_t key, uint32_t val) {
  uint32_t buckix = key % MAP_BUCKS;
  buckelem_t *cur = map->bucks[buckix];
  while (cur) {
    if (cur->key == key) {
      uint32_t old_val = cur->val;
      cur->val = val;
      return old_val;
    }
    cur = cur->_next;
  }
  buckelem_t *elem = malloc(sizeof(buckelem_t));
  if (elem) {
    elem->key = key;
    elem->val = val;
    elem->_next = map->bucks[buckix];
    map->bucks[buckix] = elem;
    return 0;
  } else {
    return -1;
  }
}

static uint32_t map_get(map_t *map, uint32_t key) {
  uint32_t buckix = key % MAP_BUCKS;
  buckelem_t *cur = map->bucks[buckix];
  while (cur) {
    if (cur->key == key) {
      return cur->val;
    }
    cur = cur->_next;
  }
  return -1;
}

static uint32_t map_remove(map_t *map, uint32_t key) {
  uint32_t buckix = key % MAP_BUCKS;
  buckelem_t *cur = map->bucks[buckix];
  buckelem_t *prev = NULL;
  while (cur) {
    if (cur->key == key) {
      uint32_t val = cur->val;
      if (prev) prev->_next = cur->_next;
      else      map->bucks[buckix] = cur->_next;
      free(cur);
      return val;
    }
    prev = cur;
    cur = cur->_next;
  }
  return -1;
}

static void map_free(map_t *map) {
  uint32_t i;
  for (i = 0; i < MAP_BUCKS; i++) {
    buckelem_t *cur = map->bucks[i];
    while (cur) {
      buckelem_t *next = cur->_next;
      free(cur);
      cur = next;
    }
    map->bucks[i] = NULL;
  }
}

#define iterate_fn(_cfs, _fn, ...) \
  _cfs_entry_t *e = (_cfs)->fs_head; \
  while (e) { \
    void *fs = e->wrap->fs; \
    int fsres = e->wrap->_fn(fs, ## __VA_ARGS__); \
    e->last_res = fsres; \
    e = e->_next; \
  }

#define iterate_fh_fn(_cfs, _fn, _fh, ...) \
  _cfs_entry_t *e = (_cfs)->fs_head; \
  while (e) { \
    void *fs = e->wrap->fs; \
    int localfh = map_get(&e->fd_map, _fh); \
    int fsres = e->wrap->_fn(fs, localfh, ## __VA_ARGS__); \
    if ((void *)e->wrap->_fn == (void *)e->wrap->close) map_remove(&e->fd_map, _fh); \
    e->last_res = fsres; \
    e = e->_next; \
  }


static int handle_res(cfs_t *cfs) {
  _cfs_entry_t *e = cfs->fs_head; \
  while (e) {
    void *fs = e->wrap->fs;
    if (e->last_res < 0) {
      e->last_err = e->wrap->translate_err(fs, e->last_res);
    } else {
      e->last_err = 0;
    }
    e = e->_next;
  }
  return 0;
}

static int handle_res_fd(cfs_t *cfs) {
  _cfs_entry_t *e = cfs->fs_head; \
  while (e) {
    void *fs = e->wrap->fs;
    if (e->last_res < 0) {
      e->last_err = e->wrap->translate_err(fs, e->last_res);
    } else {
      e->last_err = 0;
      map_put(&e->fd_map, cfs->cfs_fh, e->last_res);
    }
    e = e->_next;
  }
  return cfs->cfs_fh;
}

#define ITERATOR_LOCAL(_x) e->wrap->_x

int cfs_mount(cfs_t *cfs) {
  iterate_fn(cfs, mount, ITERATOR_LOCAL(fs_cfg));
  return handle_res(cfs);
}
int cfs_umount(cfs_t *cfs) {
  iterate_fn(cfs, umount);
  return handle_res(cfs);
}
int cfs_creat(cfs_t *cfs, const char *path) {
  cfs->cfs_fh++;
  iterate_fn(cfs, creat, path);
  return handle_res_fd(cfs);
}
int cfs_open(cfs_t *cfs, const char *path, uint32_t oflags) {
  cfs->cfs_fh++;
  iterate_fn(cfs, open, path, ITERATOR_LOCAL(translate_oflags(fs, oflags)));
  return handle_res_fd(cfs);
}
int cfs_read(cfs_t *cfs, int fh, void *buf, uint32_t sz) {
  iterate_fh_fn(cfs, read, fh, buf, sz);
  return handle_res(cfs);
}
int cfs_write(cfs_t *cfs, int fh, const void *buf, uint32_t sz) {
  iterate_fh_fn(cfs, write, fh, buf, sz);
  return handle_res(cfs);
}
int cfs_truncate(cfs_t *cfs, const char *path, uint32_t sz) {
  iterate_fn(cfs, truncate, path, sz);
  return handle_res(cfs);
}
int cfs_ftruncate(cfs_t *cfs, int fh, uint32_t sz) {
  iterate_fh_fn(cfs, ftruncate, fh, sz);
  return handle_res(cfs);
}
int cfs_close(cfs_t *cfs, int fh) {
  iterate_fh_fn(cfs, close, fh);
  return handle_res(cfs);
}
int cfs_lseek(cfs_t *cfs, int fh, int offs, int whence) {
  iterate_fh_fn(cfs, lseek, fh, offs, ITERATOR_LOCAL(translate_whence(fs, whence)));
  return handle_res(cfs);
}
int cfs_ftell(cfs_t *cfs, int fh) {
  iterate_fh_fn(cfs, ftell, fh);
  return handle_res(cfs);
}
int cfs_stat(cfs_t *cfs, const char *path, struct cfs_stat *buf) {
  iterate_fn(cfs, stat, path, buf);
  return handle_res(cfs);
}
int cfs_remove(cfs_t *cfs, const char *path) {
  iterate_fn(cfs, remove, path);
  return handle_res(cfs);
}
int cfs_rename(cfs_t *cfs, const char *old_path, const char *new_path) {
  iterate_fn(cfs, rename, old_path, new_path);
  return handle_res(cfs);
}
//int cfs_opendir(cfs_t *cfs, cfs_DIR *d, const char *path) {
//  iterate_fn(cfs, opendir, path);
//}
//struct cfs_dirent *cfs_readdir(cfs_t *cfs, cfs_DIR *d) {
//  iterate_fn(cfs, readdir, d);
//}


void cfs_free(cfs_t *cfs) {
  _cfs_entry_t *e = cfs->fs_head;
  while (e) {
    _cfs_entry_t *ne = e->_next;
    map_free(&e->fd_map);
    if (e->wrap->free) e->wrap->free(e->wrap->fs);
    free(e->wrap);
    free(e);
    e = ne;
  }
}

void cfs_link_fs(cfs_t *cfs, cfs_fs_t *fs) {
  _cfs_entry_t *e = malloc(sizeof(_cfs_entry_t));
  if (e == NULL) return;
  memset(e, 0, sizeof(_cfs_entry_t));
  e->wrap = fs;
  e->_next = cfs->fs_head;
  cfs->fs_head = e;
}

