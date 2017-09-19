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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>

#define str(s) xstr(s)
#define xstr(s) #s
#ifndef CFS_DBG
#define CFS_DBG(f, ...) printf(f, ## __VA_ARGS__)
#endif
#define CFS_RD_CHUNK_SZ       4096
#define MAX_NAME_LEN          256
#define dbg(f, ...) CFS_DBG(f, ## __VA_ARGS__)

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
    dbg("%s: %s -> res:%d\n", e->wrap->name, str(_fn), fsres); \
    e->last_err = 0; \
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
    dbg("%s: %s -> res:%d\n", e->wrap->name, str(_fn), fsres); \
    e->last_err = 0; \
    e->last_res = fsres; \
    e = e->_next; \
  }


static int handle_res(cfs_t *cfs) {
  _cfs_entry_t *e = cfs->fs_head; \
  while (e) {
    void *fs = e->wrap->fs;
    if (e->last_res < 0) {
      e->last_err = e->wrap->translate_err(fs, e->last_res);
    }
    e = e->_next;
  }
  return cfs->fs_head ? cfs->fs_head->last_res : 0;
}

static int handle_res_fd(cfs_t *cfs) {
  _cfs_entry_t *e = cfs->fs_head; \
  int __err = 0;
  while (e) {
    void *fs = e->wrap->fs;
    if (e->last_res < 0) {
      e->last_err = e->wrap->translate_err(fs, e->last_res);
      __err = e->last_res;
    } else {
      map_put(&e->fd_map, cfs->cfs_fh, e->last_res);
    }
    e = e->_next;
  }
  return __err ? __err : cfs->cfs_fh;
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
int cfs_read(cfs_t *cfs, int fh, uint8_t **buf, uint32_t sz) {
  iterate_fh_fn(cfs, read, fh, *buf++, sz);
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
int cfs_stat(cfs_t *cfs, const char *path, struct cfs_stat **buf) {
  iterate_fn(cfs, stat, path, (*buf)++);
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
int cfs_opendir(cfs_t *cfs, cfs_DIR **d, const char *path) {
  iterate_fn(cfs, opendir, (*d)++, path);
  return handle_res(cfs);
}
int cfs_readdir(cfs_t *cfs, cfs_DIR **d) {
  iterate_fn(cfs, readdir, (*d)++);
  return handle_res(cfs);
}
int cfs_closedir(cfs_t *cfs, cfs_DIR **d) {
  iterate_fn(cfs, closedir, (*d)++);
  return handle_res(cfs);
}


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

void cfs_link_fs(cfs_t *cfs, const char *name, cfs_fs_t *fs) {
  _cfs_entry_t *e = malloc(sizeof(_cfs_entry_t));
  if (e == NULL) return;
  memset(e, 0, sizeof(_cfs_entry_t));
  strncpy(fs->name, name, sizeof(fs->name));
  e->wrap = fs;
  e->_next = cfs->fs_head;
  cfs->fs_head = e;
  cfs->fs_cnt++;
}

static int _check_err_consistency(cfs_t *cfs) {
  _cfs_entry_t *e = cfs->fs_head;
  int last_err = e ? e->last_err : 0;
  while (e) {
    if (e->last_err != last_err) {
      printf("errcode inconsistency in %s %d != %d\n", e->wrap->name, e->last_err, last_err);
      return -1;
    }
    last_err = e->last_err;
    e = e->_next;
  }
  //dbg("consistent errcode: %d (%s)\n", last_err, strerror(last_err < 0 ? -last_err : last_err));
  return 0;
}

static int _check_res_consistency(cfs_t *cfs) {
  _cfs_entry_t *e = cfs->fs_head;
  int last_res = e ? e->last_res : 0;
  while (e) {
    if (e->last_res != last_res) {
      printf("resultcode inconsistency in %s %d != %d\n", e->wrap->name, e->last_res, last_res);
      return -1;
    }
    last_res = e->last_res;
    e = e->_next;
  }
  //dbg("consistent result: %d\n", last_res);
  return 0;
}

#define check_err(cfs, x) do{ \
  if ((x)<0) {\
    int _res; \
    if ((_res = _check_err_consistency(cfs))) { \
      return _res; \
    } \
    return 0; \
  } \
} while (0)

#define check_res(cfs, x) do{ \
  {\
    int _res; \
    if ((_res = _check_res_consistency(cfs))) { \
      return _res; \
    } \
  } \
} while (0)

#define check(cfs, x) \
  check_err(cfs, x); \
  check_res(cfs, x);

int cfs_validate_file(cfs_t *cfs, const char *path) {
  int res = 0;
  // try open
  int fh = cfs_open(cfs, path, CFS_O_RDONLY);
  check_err(cfs, fh);
  struct cfs_stat s[cfs->fs_cnt];
  struct cfs_stat *stats = s;

  // stat and check
  res = cfs_stat(cfs, path, &stats);
  check(cfs, res);
  if (strncmp(s[0].name, path, sizeof(s[0].name))) {
    printf("stat.name/path inconsistency %s != %s\n", s[0].name, path);
  }
  int i;
  for (i = 1; i < cfs->fs_cnt; i++) {
    if (s[0].size != s[i].size) {
      printf("stat.size inconsistency %d != %d\n", s[0].size, s[i].size);
      return -1;
    }
    if (strncmp(s[0].name, s[i].name, sizeof(s[0].name))) {
      printf("stat.name inconsistency %s != %s\n", s[0].name, s[i].name);
      return -1;
    }
  }

  // read
  uint8_t *bufs[CFS_RD_CHUNK_SZ];
  uint8_t **bufs_p = bufs;
  for (i = 0; i < cfs->fs_cnt; i++) bufs[i] = (uint8_t *)malloc(CFS_RD_CHUNK_SZ);

  uint32_t offs = 0;
  while (offs < s[0].size) {
    res = cfs_read(cfs, fh, bufs_p, CFS_RD_CHUNK_SZ);
    if (res < 0) for (i = 0; i < cfs->fs_cnt; i++) free(bufs[i]);
    if (_check_res_consistency(cfs)) for (i = 0; i < cfs->fs_cnt; i++) free(bufs[i]);
    check(cfs, res);

    // data verify
    int j,k;
    for (k = 1; k < cfs->fs_cnt; k++) {
      for (j = 0; j < res; j++) {
        if (bufs[0][j] != bufs[k][j]) {
          printf("file data inconsistency @ offs %08x, %d != %d\n", offs + j, bufs[0][j], bufs[k][j]);
          for (i = 0; i < cfs->fs_cnt; i++) free(bufs[i]);
          return -1;
        }
      }
    }
    offs += res;
  }
  for (i = 0; i < cfs->fs_cnt; i++) free(bufs[i]);

  // close
  res = cfs_close(cfs, fh);
  check(cfs, res);

  return res;
}

typedef struct ls_elem_s {
  char name[MAX_NAME_LEN];
  int size;
} ls_elem_t;


static int comp_name(const void *e1, const void *e2) {
  const ls_elem_t *lse1 = (const ls_elem_t *)e1;
  const ls_elem_t *lse2 = (const ls_elem_t *)e2;
  return strncmp(lse1->name, lse2->name, MAX_NAME_LEN);
}
int cfs_validate_ls(cfs_t *cfs) {
  cfs_DIR d[cfs->fs_cnt];
  cfs_DIR *dp = d;
  int res;

  // first count entries
  int entries = 0;
  res = cfs_opendir(cfs, &dp, "/");
  check(cfs, res);
  dp = d;
  while ((res = cfs_readdir(cfs, &dp)) == 0) {
//    int i;
//    for (i = 0; i < cfs->fs_cnt; i++) {
//      printf("%s  %d\n", d[i].de.name, d[i].de.size);
//    }
    entries++;
    dp = d;
  }
  if (res == -ENOENT) res = 0;
  check(cfs, res);
  dp = d;
  res = cfs_closedir(cfs, &dp);
  check(cfs, res);

  // place all entries in mem
  ls_elem_t *heads[cfs->fs_cnt];
  int i;
  for (i = 0; i < cfs->fs_cnt; i++) {
    heads[i] = malloc(sizeof(ls_elem_t) * entries);
  }
  dp = d;
  res = cfs_opendir(cfs, &dp, "/");
  check(cfs, res);
  dp = d;
  entries = 0;
  while ((res = cfs_readdir(cfs, &dp)) == 0) {
    for (i = 0; i < cfs->fs_cnt; i++) {
      strncpy(heads[i][entries].name, d[i].de.name, MAX_NAME_LEN);
      heads[i][entries].size = d[i].de.size;
    }
    entries++;
    dp = d;
  }
  if (res == -ENOENT) res = 0;
  check(cfs, res);
  dp = d;
  res = cfs_closedir(cfs, &dp);
  check(cfs, res);

  // sort all entries
  for (i = 0; i < cfs->fs_cnt; i++) {
    qsort(heads[i], entries, sizeof(ls_elem_t), comp_name);
  }

  // TODO check all
  for (i = 0; i < entries; i++) {
    int j;
    for (j = 0; j < cfs->fs_cnt; j++) {
      printf("%d:%s %d\n", i, heads[j][i].name, heads[j][i].size);
    }
  }

  // free all
  for (i = 0; i < cfs->fs_cnt; i++) {
    free(heads[i]);
  }

  return 0;
}
