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
//#define CFS_DBG(f, ...) printf(f, ## __VA_ARGS__)
#define CFS_DBG(f, ...)
#endif
#define CFS_RD_CHUNK_SZ       4096
#define CFS_MAX_NAME_LEN      256
#define dbg(f, ...) CFS_DBG(f, ## __VA_ARGS__)

static uint32_t crc32_tab[] = { 0x00000000L, 0x77073096L, 0xee0e612cL,
    0x990951baL, 0x076dc419L, 0x706af48fL, 0xe963a535L, 0x9e6495a3L,
    0x0edb8832L, 0x79dcb8a4L, 0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL,
    0x7eb17cbdL, 0xe7b82d07L, 0x90bf1d91L, 0x1db71064L, 0x6ab020f2L,
    0xf3b97148L, 0x84be41deL, 0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L,
    0x83d385c7L, 0x136c9856L, 0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL,
    0x14015c4fL, 0x63066cd9L, 0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L,
    0x4c69105eL, 0xd56041e4L, 0xa2677172L, 0x3c03e4d1L, 0x4b04d447L,
    0xd20d85fdL, 0xa50ab56bL, 0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L,
    0xacbcf940L, 0x32d86ce3L, 0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L,
    0x26d930acL, 0x51de003aL, 0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L,
    0x56b3c423L, 0xcfba9599L, 0xb8bda50fL, 0x2802b89eL, 0x5f058808L,
    0xc60cd9b2L, 0xb10be924L, 0x2f6f7c87L, 0x58684c11L, 0xc1611dabL,
    0xb6662d3dL, 0x76dc4190L, 0x01db7106L, 0x98d220bcL, 0xefd5102aL,
    0x71b18589L, 0x06b6b51fL, 0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L,
    0x0f00f934L, 0x9609a88eL, 0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL,
    0x91646c97L, 0xe6635c01L, 0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L,
    0xf262004eL, 0x6c0695edL, 0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L,
    0x65b0d9c6L, 0x12b7e950L, 0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL,
    0x15da2d49L, 0x8cd37cf3L, 0xfbd44c65L, 0x4db26158L, 0x3ab551ceL,
    0xa3bc0074L, 0xd4bb30e2L, 0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL,
    0xd3d6f4fbL, 0x4369e96aL, 0x346ed9fcL, 0xad678846L, 0xda60b8d0L,
    0x44042d73L, 0x33031de5L, 0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL,
    0x270241aaL, 0xbe0b1010L, 0xc90c2086L, 0x5768b525L, 0x206f85b3L,
    0xb966d409L, 0xce61e49fL, 0x5edef90eL, 0x29d9c998L, 0xb0d09822L,
    0xc7d7a8b4L, 0x59b33d17L, 0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL,
    0xedb88320L, 0x9abfb3b6L, 0x03b6e20cL, 0x74b1d29aL, 0xead54739L,
    0x9dd277afL, 0x04db2615L, 0x73dc1683L, 0xe3630b12L, 0x94643b84L,
    0x0d6d6a3eL, 0x7a6a5aa8L, 0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L,
    0x7d079eb1L, 0xf00f9344L, 0x8708a3d2L, 0x1e01f268L, 0x6906c2feL,
    0xf762575dL, 0x806567cbL, 0x196c3671L, 0x6e6b06e7L, 0xfed41b76L,
    0x89d32be0L, 0x10da7a5aL, 0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L,
    0x17b7be43L, 0x60b08ed5L, 0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L,
    0x4fdff252L, 0xd1bb67f1L, 0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL,
    0xd80d2bdaL, 0xaf0a1b4cL, 0x36034af6L, 0x41047a60L, 0xdf60efc3L,
    0xa867df55L, 0x316e8eefL, 0x4669be79L, 0xcb61b38cL, 0xbc66831aL,
    0x256fd2a0L, 0x5268e236L, 0xcc0c7795L, 0xbb0b4703L, 0x220216b9L,
    0x5505262fL, 0xc5ba3bbeL, 0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L,
    0xc2d7ffa7L, 0xb5d0cf31L, 0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L,
    0xec63f226L, 0x756aa39cL, 0x026d930aL, 0x9c0906a9L, 0xeb0e363fL,
    0x72076785L, 0x05005713L, 0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL,
    0x0cb61b38L, 0x92d28e9bL, 0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L,
    0x86d3d2d4L, 0xf1d4e242L, 0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL,
    0xf6b9265bL, 0x6fb077e1L, 0x18b74777L, 0x88085ae6L, 0xff0f6a70L,
    0x66063bcaL, 0x11010b5cL, 0x8f659effL, 0xf862ae69L, 0x616bffd3L,
    0x166ccf45L, 0xa00ae278L, 0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L,
    0xa7672661L, 0xd06016f7L, 0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL,
    0xd9d65adcL, 0x40df0b66L, 0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L,
    0x47b2cf7fL, 0x30b5ffe9L, 0xbdbdf21cL, 0xcabac28aL, 0x53b39330L,
    0x24b4a3a6L, 0xbad03605L, 0xcdd70693L, 0x54de5729L, 0x23d967bfL,
    0xb3667a2eL, 0xc4614ab8L, 0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L,
    0xc30c8ea1L, 0x5a05df1bL, 0x2d02ef8dL };

static uint32_t crc32(const uint8_t *b, uint32_t len, uint32_t crc) {
  uint32_t i;
  uint32_t crc32val = crc;
  for (i = 0; i < len; i++) {
    crc32val = crc32_tab[(crc32val ^ b[i]) & 0xff] ^ (crc32val >> 8);
  }
  return crc32val;
}

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

#define iterate_fn_xtra(_cfs, _fn, _xtra, ...) \
  _cfs_entry_t *e = (_cfs)->fs_head; \
  while (e) { \
    void *fs = e->wrap->fs; \
    int fsres = e->wrap->_fn(fs, ## __VA_ARGS__); \
    dbg("%s: %s -> res:%d\n", e->wrap->name, str(_fn), fsres); \
    e->last_err = 0; \
    e->last_res = fsres; \
    e = e->_next; \
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
  _cfs_entry_t *e = cfs->fs_head;
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
  _cfs_entry_t *e = cfs->fs_head;
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

#define ITERATOR_LOCAL(_x) e->wrap->_x

int cfs_mount(cfs_t *cfs) {
  iterate_fn(cfs, mount, ITERATOR_LOCAL(fs_cfg));
  return handle_res(cfs);
}
int cfs_mountc(cfs_t *cfs, int *ret) {
  *ret = cfs_mount(cfs);
  check(cfs, *ret);
  return 0;
}
int cfs_umount(cfs_t *cfs) {
  iterate_fn(cfs, umount);
  return handle_res(cfs);
}
int cfs_umountc(cfs_t *cfs, int *ret) {
  *ret = cfs_umount(cfs);
  check(cfs, *ret);
  return 0;
}
int cfs_creat(cfs_t *cfs, const char *path) {
  cfs->cfs_fh++;
  iterate_fn(cfs, creat, path);
  return handle_res_fd(cfs);
}
int cfs_creatc(cfs_t *cfs, int *ret, const char *path) {
  *ret = cfs_creat(cfs, path);
  check(cfs, *ret);
  return 0;
}
int cfs_open(cfs_t *cfs, const char *path, uint32_t oflags) {
  cfs->cfs_fh++;
  iterate_fn(cfs, open, path, ITERATOR_LOCAL(translate_oflags(fs, oflags)));
  return handle_res_fd(cfs);
}
int cfs_openc(cfs_t *cfs, int *ret, const char *path, uint32_t oflags) {
  *ret = cfs_open(cfs, path, oflags);
  check(cfs, *ret);
  return 0;
}
int cfs_read(cfs_t *cfs, int fh, uint8_t **buf, uint32_t sz) {
  iterate_fh_fn(cfs, read, fh, *buf++, sz);
  return handle_res(cfs);
}
int cfs_readc(cfs_t *cfs, int *ret, int fh, uint8_t **buf, uint32_t sz) {
  *ret = cfs_read(cfs, fh, buf, sz);
  check(cfs, *ret);
  return 0;
}
int cfs_write(cfs_t *cfs, int fh, const void *buf, uint32_t sz) {
  iterate_fh_fn(cfs, write, fh, buf, sz);
  return handle_res(cfs);
}
int cfs_writec(cfs_t *cfs, int *ret, int fh, const void *buf, uint32_t sz) {
  *ret = cfs_write(cfs, fh, buf, sz);
  check(cfs, *ret);
  return 0;
}
int cfs_truncate(cfs_t *cfs, const char *path, uint32_t sz) {
  iterate_fn(cfs, truncate, path, sz);
  return handle_res(cfs);
}
int cfs_truncatec(cfs_t *cfs, int *ret, const char *path, uint32_t sz) {
  *ret = cfs_truncate(cfs, path, sz);
  check(cfs, *ret);
  return 0;
}
int cfs_ftruncate(cfs_t *cfs, int fh, uint32_t sz) {
  iterate_fh_fn(cfs, ftruncate, fh, sz);
  return handle_res(cfs);
}
int cfs_ftruncatec(cfs_t *cfs, int *ret, int fh, uint32_t sz) {
  *ret = cfs_ftruncate(cfs, fh, sz);
  check(cfs, *ret);
  return 0;
}
int cfs_close(cfs_t *cfs, int fh) {
  iterate_fh_fn(cfs, close, fh);
  return handle_res(cfs);
}
int cfs_closec(cfs_t *cfs, int *ret, int fh) {
  *ret = cfs_close(cfs, fh);
  check(cfs, *ret);
  return 0;
}
int cfs_lseek(cfs_t *cfs, int fh, int offs, int whence) {
  iterate_fh_fn(cfs, lseek, fh, offs, ITERATOR_LOCAL(translate_whence(fs, whence)));
  return handle_res(cfs);
}
int cfs_lseekc(cfs_t *cfs, int *ret, int fh, int offs, int whence) {
  *ret = cfs_lseek(cfs, fh, offs, whence);
  check(cfs, *ret);
  return 0;
}
int cfs_ftell(cfs_t *cfs, int fh) {
  iterate_fh_fn(cfs, ftell, fh);
  return handle_res(cfs);
}
int cfs_ftellc(cfs_t *cfs, int *ret, int fh) {
  *ret = cfs_ftell(cfs, fh);
  check(cfs, *ret);
  return 0;
}
int cfs_stat(cfs_t *cfs, const char *path, struct cfs_stat **buf) {
  iterate_fn(cfs, stat, path, (*buf)++);
  return handle_res(cfs);
}
int cfs_statc(cfs_t *cfs, int *ret, const char *path, struct cfs_stat **buf) {
  *ret = cfs_stat(cfs, path, buf);
  check(cfs, *ret);
  return 0;
}
int cfs_remove(cfs_t *cfs, const char *path) {
  iterate_fn(cfs, remove, path);
  return handle_res(cfs);
}
int cfs_removec(cfs_t *cfs, int *ret, const char *path) {
  *ret = cfs_remove(cfs, path);
  check(cfs, *ret);
  return 0;
}
int cfs_rename(cfs_t *cfs, const char *old_path, const char *new_path) {
  iterate_fn(cfs, rename, old_path, new_path);
  return handle_res(cfs);
}
int cfs_renamec(cfs_t *cfs, int *ret, const char *old_path, const char *new_path) {
  *ret = cfs_rename(cfs, old_path, new_path);
  check(cfs, *ret);
  return 0;
}
int cfs_opendir(cfs_t *cfs, cfs_DIR **d, const char *path) {
  iterate_fn(cfs, opendir, (*d)++, path);
  return handle_res(cfs);
}
int cfs_opendirc(cfs_t *cfs, int *ret, cfs_DIR **d, const char *path) {
  *ret = cfs_opendir(cfs, d, path);
  check(cfs, *ret);
  return 0;
}
int cfs_readdir(cfs_t *cfs, cfs_DIR **d) {
  _cfs_entry_t *cfse = cfs->fs_head;
  cfs_DIR *d2 = *d;
  while (cfse) {
    memset(d2->de.name, 0, CFS_MAX_NAME_LEN);
    d2->de.size = 0;
    d2++;
    cfse = cfse->_next;
  }
  iterate_fn(cfs, readdir, (*d)++);
  return handle_res(cfs);
}
int cfs_readdirc(cfs_t *cfs, int *ret, cfs_DIR **d) {
  *ret = cfs_readdir(cfs, d);
  check(cfs, *ret);
  return 0;
}
int cfs_closedir(cfs_t *cfs, cfs_DIR **d) {
  iterate_fn(cfs, closedir, (*d)++);
  return handle_res(cfs);
}
int cfs_closedirc(cfs_t *cfs, int *ret, cfs_DIR **d) {
  *ret = cfs_closedir(cfs, d);
  check(cfs, *ret);
  return 0;
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

void *cfs_get_fs_by_name(cfs_t *cfs, const char *name) {
  _cfs_entry_t *e = cfs->fs_head;
  while (e) {
    if (strcmp(name, e->wrap->name) == 0) {
      return e->wrap->fs;
    }
    e = e->_next;
  }
  return NULL;
}

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
    printf("%s: stat.name/path inconsistency for %s != %s\n", path, s[0].name, path);
  }
  int i;
  for (i = 1; i < cfs->fs_cnt; i++) {
    if (s[0].size != s[i].size) {
      printf("%s: stat.size inconsistency %d != %d\n", path, s[0].size, s[i].size);
      return -1;
    }
    if (strncmp(s[0].name, s[i].name, sizeof(s[0].name))) {
      printf("%s: stat.name inconsistency %s != %s\n", path, s[0].name, s[i].name);
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
          printf("%s: file data inconsistency @ offs %08x, %d != %d\n",
              path, offs + j, bufs[0][j], bufs[k][j]);
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
  char name[CFS_MAX_NAME_LEN];
  int size;
} ls_elem_t;


static int comp_name(const void *e1, const void *e2) {
  const ls_elem_t *lse1 = (const ls_elem_t *)e1;
  const ls_elem_t *lse2 = (const ls_elem_t *)e2;
  return strncmp(lse1->name, lse2->name, CFS_MAX_NAME_LEN);
}
static int _validate_ls(cfs_t *cfs, uint8_t check_contents) {
  cfs_DIR d[cfs->fs_cnt];
  cfs_DIR *dp;
  int res;

  // first count entries
  int entries = 0;
  dp = d;
  res = cfs_opendir(cfs, &dp, "/");
  check(cfs, res);
  dp = d;
  while ((res = cfs_readdir(cfs, &dp)) == 0) {
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
      strncpy(heads[i][entries].name, d[i].de.name, CFS_MAX_NAME_LEN);
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

  // check all
  int eix;
  for (eix = 0; res == 0 && eix < entries; eix++) {
    uint32_t crc0 = 0;
    _cfs_entry_t *centry = cfs->fs_head;
    cfs_fs_t *wfs = centry->wrap;
    int fh;
    if (check_contents) {
      // calc crc for file in first fs
      fh = wfs->open(wfs->fs, heads[0][eix].name, wfs->translate_oflags(wfs->fs, CFS_O_RDONLY));
      if (fh > 0) {
        uint8_t buf[CFS_RD_CHUNK_SZ];
        uint32_t sz = heads[0][eix].size;
        while (sz) {
          int rd = wfs->read(wfs->fs, fh, buf, sz > CFS_RD_CHUNK_SZ ? CFS_RD_CHUNK_SZ : sz);
          if (rd > 0) {
            sz -= rd;
            crc0 = crc32(buf, rd, crc0);
          } else {
            break;
          }
        }
        wfs->close(wfs->fs, fh);
      }
    }
    centry = centry->_next;

    int fs;
    for (fs = 1; fs < cfs->fs_cnt; fs++) {
      if (strncmp(heads[0][eix].name, heads[fs][eix].name, CFS_MAX_NAME_LEN)) {
        printf("ls inconsistency, got name %s @ %s\n",
            heads[fs][eix].name, heads[0][eix].name);
        res = -1;
        break;
      }
      if (heads[0][eix].size != heads[fs][eix].size) {
        printf("%s: ls inconsistency, got size %d @ %s with size %d\n",
            heads[0][eix].name, heads[fs][eix].size, heads[fs][eix].name, heads[0][eix].size);
        res = -1;
        break;
      }

      if (check_contents) {
        // calc crc for file in this fs and compare with crc for same file in first fs
        wfs = centry->wrap;
        fh = wfs->open(wfs->fs, heads[0][eix].name, wfs->translate_oflags(wfs->fs, CFS_O_RDONLY));
        if (fh > 0) {
          uint crcn = 0;
          uint8_t buf[CFS_RD_CHUNK_SZ];
          uint32_t sz = heads[0][eix].size;
          while (sz) {
            int rd = wfs->read(wfs->fs, fh, buf, sz > CFS_RD_CHUNK_SZ ? CFS_RD_CHUNK_SZ : sz);
            if (rd > 0) {
              sz -= rd;
              crcn = crc32(buf, rd, crcn);
            } else {
              break;
            }
          }
          wfs->close(wfs->fs, fh);
          if (crcn != crc0) {
            printf("%s/%s: file inconsistency, crc differ: %08x and %08x\n",
                centry->wrap->name, heads[0][eix].name, crc0, crcn);
            res = -1;
            break;
          }
        }
      }
      centry = centry->_next;
    }
  }

  // free all
  for (i = 0; i < cfs->fs_cnt; i++) {
    free(heads[i]);
  }

  return res;
}

int cfs_validate_ls(cfs_t *cfs) {
  return _validate_ls(cfs, 0);
}
int cfs_validate_fs(cfs_t *cfs) {
  return _validate_ls(cfs, 1);
}
