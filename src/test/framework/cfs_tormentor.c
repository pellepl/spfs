/*
 * cfs_tormentor.c
 *
 *  Created on: Sep 22, 2017
 *      Author: petera
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "cfs.h"
#include "cfs_tormentor.h"
#include "prand.h"

typedef struct fname_elem {
  char *name;
  struct fname_elem *_next;
} fname_elem_t;

typedef struct priv {
  double honor_remove_when_full;
  double honor_create_when_empty;
  double keep_large_files;
  double keep_small_files;
  double modify_large_files;
  double modify_small_files;
  uint32_t max_files;
  uint32_t max_fds;
  uint32_t max_file_size;
  prnd_t rnd;
  int *fds;
  cfs_t *cfs;

  int open_fds;
  int files;
  fname_elem_t *fname_head;
  char cur_fname[4096];
} priv_t;

static void memrnd(priv_t *p, uint8_t *dst, uint32_t len) {
  while (len--) {
    *dst++=prndbyte(&p->rnd);
  }
}

int torment_create_session(uint32_t seed, torment_session_t *sess, cfs_t *cfs,
                           uint32_t max_fds, uint32_t max_files, uint32_t max_file_size) {
  priv_t *p = malloc(sizeof(priv_t));
  if (p == NULL) return ENOMEM;
  memset(p, 0, sizeof(priv_t));
  sess->_priv = p;
  prnd_seed(&p->rnd, seed);
  p->cfs = cfs;
  p->honor_remove_when_full = prndrngd(&p->rnd, 0, 1);
  p->honor_create_when_empty = prndrngd(&p->rnd, 0, 1);
  p->keep_large_files = prndrngd(&p->rnd, 0, 1);
  p->keep_small_files = prndrngd(&p->rnd, 0, 1);
  p->modify_large_files = prndrngd(&p->rnd, 0, 1);
  p->modify_small_files = prndrngd(&p->rnd, 0, 1);
  p->max_fds = prndrngi(&p->rnd, 1, max_fds+1);
  p->max_files = prndrngi(&p->rnd, 1, max_files+1);
  p->max_file_size = max_file_size;

  p->fds = malloc(p->max_fds*sizeof(int));
  if (p->fds == NULL) {
    free(p);
    return ENOMEM;
  }
  memset(p, 0xff, p->max_fds*sizeof(int));

  return 0;
}

static int fname_register(priv_t *p, const char *fname) {
  char *fe = strdup(fname);
  if (fe == NULL) return -ENOMEM;
  fname_elem_t *e = malloc(sizeof(fname_elem_t));
  if (e == NULL) {
    free(fe);
    return -ENOMEM;
  }
  e->name = fe;
  e->_next = p->fname_head;
  p->fname_head = e;
  return 0;
}

static int fname_unregister(priv_t *p, const char *name) {
  fname_elem_t *e = p->fname_head;
  fname_elem_t *pe = NULL;
  while (e) {
    if (strcmp(name, e->name) == 0) {
      if (pe == NULL) {
        p->fname_head = e->_next;
      } else {
        pe->_next = e->_next;
      }
      free(e->name);
      free(e);
      return 0;
    }
    pe = e;
    e = e->_next;
  }
  return -1;
}

static int fname_find(priv_t *p, const char *name) {
  fname_elem_t *e = p->fname_head;
  fname_elem_t *pe = NULL;
  while (e) {
    if (strcmp(name, e->name) == 0) {
      return 0;
    }
    e = e->_next;
  }
  return -1;
}

static int fetch_opened_fd(priv_t *p) {
  if (p->open_fds == 0) return -1;
  uint32_t which = prndrngi(&p->rnd, 1, p->open_fds);
  int fd = -1;
  int i = 0;
  do {
    if (p->fds[i] != -1) {
      fd = p->fds[i];
      which--;
    }
    i++;
  } while (which);
  return fd;
}

static const char *fetch_filename(priv_t *p) {
  sprintf(p->cur_fname, "%s%i", "file", prndrngi(&p->rnd, 0, p->max_files));
  return p->cur_fname;
}

static int fetch_oflags(priv_t *p) {
  return
  (prndrngd(&p->rnd, 0, 1) >= 0.5 ? CFS_O_APPEND : 0) |
  (prndrngd(&p->rnd, 0, 1) >= 0.5 ? CFS_O_CREAT : 0) |
  (prndrngd(&p->rnd, 0, 1) >= 0.5 ? CFS_O_EXCL : 0) |
  (prndrngd(&p->rnd, 0, 1) >= 0.5 ? CFS_O_RDONLY : 0) |
  (prndrngd(&p->rnd, 0, 1) >= 0.5 ? CFS_O_WRONLY : 0) |
  (prndrngd(&p->rnd, 0, 1) >= 0.5 ? CFS_O_RDWR : 0) |
  (prndrngd(&p->rnd, 0, 1) >= 0.5 ? CFS_O_TRUNC : 0);
}

static uint32_t fetch_file_size(priv_t *p) {
  return prndrngi(&p->rnd, 1, p->max_file_size);
}

static int fetch_whence(priv_t *p) {
  return (int[3]){CFS_SEEK_SET, CFS_SEEK_CUR, CFS_SEEK_END}[prndrngi(&p->rnd, 0, 3)];
}

static uint32_t fetch_file_offs(priv_t *p) {
  return prndrngi(&p->rnd, -p->max_file_size, p->max_file_size);
}

enum task {
  OPEN = 0,
  STAT,
  CLOSE,
  CREAT,
  READ,
  WRITE,
  TRUNC,
  FTRUNC,
  SEEK,
  REMOVE,
  RENAME,
  LS,
  _TASKS
};

static int test_open(priv_t *p,
                     const char *path, int oflags) {
  int ret;
  int res = cfs_openc(p->cfs, &ret, path, oflags);
  if (res == 0 && ret > 0) {
    p->open_fds++;
    uint32_t i;
    for (i = 0; i < p->max_fds; i++) {
      if (p->fds[i] == -1) {
        p->fds[i] = ret;
        break;
      }
    }
  }
  if (res == 0 && ret > 0 && (oflags & CFS_O_CREAT)) {
    if ((oflags & CFS_O_EXCL) == 0) {
      if (fname_find(p, path) == 0) {
        // name already existing
      } else {
        fname_register(p, path);
        p->files++;
      }
    } else {
      fname_register(p, path);
      p->files++;
    }
  }
  return res;
}
static int test_stat(priv_t *p,
                     const char *path) {
  struct cfs_stat buf[p->cfs->fs_cnt];
  struct cfs_stat *pbuf = buf;
  int ret;
  int res = cfs_statc(p->cfs, &ret, path, &pbuf);
  if (res == 0 && ret == 0) {
    int i;
    for (i = 1; i < p->cfs->fs_cnt; i++) {
      if (strcmp(buf[0].name, buf[1].name)) {
        return -1;
      }
      if (buf[0].size != buf[i].size) {
        return -1;
      }
    }
  }
  return res;
}
static int test_close(priv_t *p,
                     int fd) {
  int ret;
  int res = cfs_closec(p->cfs, &ret, fd);
  p->open_fds--;
  uint32_t i;
  for (i = 0; i < p->max_fds; i++) {
    if (p->fds[i] == fd) {
      p->fds[i] = -1;
      break;
    }
  }
  return res;
}
static int test_creat(priv_t *p,
                      const char *path) {
  int ret;
  int res = cfs_creatc(p->cfs, &ret, path);
  if (res == 0 && ret > 0) {
    p->open_fds++;
    uint32_t i;
    for (i = 0; i < p->max_fds; i++) {
      if (p->fds[i] == -1) {
        p->fds[i] = ret;
        break;
      }
    }
  }
  if (res == 0 && ret > 0) {
    if (fname_find(p, path) == 0) {
      // name already existing
    } else {
      fname_register(p, path);
      p->files++;
    }
  }
  return res;
}
static int test_read(priv_t *p,
                     int fd, uint32_t size) {
  int ret;
  uint8_t *buf = malloc(size * p->cfs->fs_cnt);
  if (buf == NULL) return -ENOMEM;
  uint8_t *bufp = buf;
  int res = cfs_readc(p->cfs, &ret, fd, &bufp, size);
  if (res == 0 && ret >= 0) {
    int i;
    for (i = 1; i < p->cfs->fs_cnt; i++) {
      if (memcmp(&buf[0*size], &buf[i*size], size)) {
        return -1;
      }
    }
  }
  free(buf);
  return res;
}
static int test_write(priv_t *p,
                      int fd, uint32_t size) {
  int ret;
  uint8_t *buf = malloc(size);
  if (buf == NULL) return -ENOMEM;
  memrnd(p, buf, size);
  int res = cfs_writec(p->cfs, &ret, fd, buf, size);
  free(buf);
  return res;
}
static int test_trunc(priv_t *p,
                      const char *path, uint32_t sz) {
  int ret;
  int res = cfs_truncate(p->cfs, path, sz);
  return res;
}
static int test_ftrunc(priv_t *p,
                     int fd, uint32_t sz) {
  int ret;
  int res = cfs_ftruncate(p->cfs, fd, sz);
  return res;
}
static int test_seek(priv_t *p,
                     int fd, int offs, int whence) {
  int ret;
  int res = cfs_lseekc(p->cfs, &ret, fd, offs, whence);
  return res;
}
static int test_remove(priv_t *p,
                       const char *path) {
  int ret;
  int res = cfs_removec(p->cfs, &ret, path);
  if (res == 0 && ret == 0) {
    fname_unregister(p, path);
    p->files--;
  }
  return res;
}
static int test_rename(priv_t *p,
                       const char *old_path, const char *new_path) {
  int ret;
  int old_path_exists = fname_find(p, old_path) == 0;
  int res = cfs_renamec(p->cfs, &ret, old_path, new_path);
  if (res == 0 && ret == 0) {
    if (old_path_exists) {
      p->files--;
    }
    fname_unregister(p, old_path);
    fname_register(p, new_path);
  }

  return res;
}
static int test_ls(priv_t *p) {
  return cfs_validate_ls(p->cfs);
}

static void _step(priv_t *p) {
  int res = -1;

  // decide what to do this step
  enum task t = prndrngi(&p->rnd, 0, _TASKS);
  switch (t) {
  case OPEN:
    res = test_open(p, fetch_filename(p), fetch_oflags(p));
    break;
  case STAT:
    res = test_stat(p, fetch_filename(p));
    break;
  case CLOSE:
    res = test_close(p, fetch_opened_fd(p));
    break;
  case CREAT:
    res = test_creat(p, fetch_filename(p));
    break;
  case READ:
    res = test_read(p, fetch_opened_fd(p), fetch_file_size(p));
    break;
  case WRITE:
    res = test_write(p, fetch_opened_fd(p), fetch_file_size(p));
    break;
  case TRUNC:
    res = test_trunc(p, fetch_filename(p), fetch_file_size(p));
    break;
  case FTRUNC:
    res = test_ftrunc(p, fetch_opened_fd(p), fetch_file_size(p));
    break;
  case SEEK:
    res = test_seek(p, fetch_opened_fd(p), fetch_file_offs(p), fetch_whence(p));
    break;
  case REMOVE:
    res = test_remove(p, fetch_filename(p));
    break;
  case RENAME:
    res = test_rename(p, fetch_filename(p), fetch_filename(p));
    break;
  case LS:
    res = test_ls(p);
    break;
  default:
    break;
  }
}



//int torment_step(torment_session_t *sess);
//int torment_run(torment_session_t *sess);
void torment_cleanup_session(torment_session_t *sess) {
  priv_t *p = (priv_t *)sess->_priv;
  free(p->fds);
  fname_elem_t *e = p->fname_head;
  while (e) {
    fname_elem_t *ne = e->_next;
    free(e->name);
    free(e);
    e = ne;
  }
  cfs_free(p->cfs);
  free(p);
}
