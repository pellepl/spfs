/*
 * spfs.c
 *
 *  Created on: Sep 25, 2017
 *      Author: petera
 */

#include "spfs.h"
#include "spfs_file.h"
#include "spfs_lowlevel.h"

#undef _SPFS_DBG_PRE
#undef _SPFS_DBG_POST
#define _SPFS_DBG_PRE ANSI_COLOR_PINK"HL "
#define _SPFS_DBG_POST ANSI_COLOR_RESET
#include "spfs_dbg.h"
#undef dbg
#define dbg(_f, ...) dbg_hl(_f, ## __VA_ARGS__)
#include "spfs_dbg.h"

static int check(spfs_t *fs) {
  if (fs->config_state != SPFS_CONFIGURED)
    return -SPFS_ERR_UNCONFIGURED;
  if (fs->mount_state != SPFS_MOUNTED)
    return -SPFS_ERR_MOUNT_STATE;
  return SPFS_OK;
}

int SPFS_stat(spfs_t *fs, const char *path, struct spfs_stat *buf) {
  dbg("name:%s\n", path);
  if (buf == NULL) ERRET(-SPFS_ERR_ARG);
  SPFS_LOCK(fs);
  ERRUNLOCK(fs, check(fs));
  pix_t dpix;
  spfs_pixhdr_t pixhdr;
  int res = spfs_file_find(fs, path, &dpix, &pixhdr);
  buf->dpix = dpix;
  buf->id = pixhdr.phdr.id;
  buf->size = pixhdr.fi.size == SPFS_FILESZ_UNDEF ? 0 : pixhdr.fi.size;
  buf->type = pixhdr.fi.type;
  spfs_strncpy(buf->name, (const char *)pixhdr.name, SPFS_CFG_FILE_NAME_SZ);
#if SPFS_CFG_FILE_META_SZ
  spfs_memcpy(buf->meta, pixhdr.name, SPFS_CFG_FILE_META_SZ);
#endif
  ERRUNLOCK(fs, res);
  SPFS_UNLOCK(fs);
  return res;
}

spfs_file_t SPFS_open(spfs_t *fs, const char *path, int oflags, int mode) {
  dbg("name:%s flags:"_SPIPRIfl"%s%s%s%s%s%s%s%s%s\n", path, oflags,
      oflags & SPFS_O_RDONLY ? " RDONLY":"",
      oflags & SPFS_O_WRONLY ? " WRONLY":"",
      oflags & SPFS_O_APPEND ? " APPEND":"",
      oflags & SPFS_O_CREAT  ? " CREAT":"",
      oflags & SPFS_O_TRUNC  ? " TRUNC":"",
      oflags & SPFS_O_EXCL   ? " EXCL":"",
      oflags & SPFS_O_DIRECT ? " DIRECT":"",
      oflags & SPFS_O_REWR   ? " REWR":"",
      oflags & SPFS_O_SENS   ? " SENS":""
          );
  (void)mode;
  SPFS_LOCK(fs);
  ERRUNLOCK(fs, check(fs));
  spfs_fd_t *fd;
  pix_t dpix;
  spfs_pixhdr_t pixhdr;
  int res = spfs_file_find(fs, path, &dpix, &pixhdr);
  uint8_t notexist = (res == -SPFS_ERR_FILE_NOT_FOUND);
  if (notexist) res = SPFS_OK;
  ERRUNLOCK(fs, res);
  res = _fd_claim(fs, &fd);
  ERRUNLOCK(fs, res);

  if ((oflags & SPFS_O_CREAT) == 0 && notexist) {
    SPFS_UNLOCK(fs);
    _fd_release(fs, fd);
    ERRET(-SPFS_ERR_FILE_NOT_FOUND);
  }

  if ((oflags & (SPFS_O_CREAT | SPFS_O_EXCL)) == (SPFS_O_CREAT | SPFS_O_EXCL)) {
    // fail if exists
    SPFS_UNLOCK(fs);
    _fd_release(fs, fd);
    ERRET(-SPFS_ERR_NAME_CONFLICT);
  }

  if ((oflags & SPFS_O_CREAT) && notexist) {
    // create
    res = spfs_file_create(fs, fd, path);
    if (res) {
      _fd_release(fs, fd);
      ERRUNLOCK(fs, res);
    }
    fd->fd_oflags = oflags;
    oflags &= ~SPFS_O_TRUNC; // no need truncing a newly created file
  }

  if (!notexist) {
    fd->offset = 0;
    fd->dpix_ixhdr = dpix;
    fd->dpix_ix = dpix;
    fd->fd_oflags = oflags;
    fd->fi.id = pixhdr.phdr.id;
    fd->fi.f_flags = pixhdr.fi.f_flags;
    fd->fi.size = pixhdr.fi.size;
    fd->fi.type = pixhdr.fi.type;
    fd->fi.x_size = pixhdr.fi.x_size;
    if ((oflags & (SPFS_O_TRUNC | SPFS_O_WRONLY)) == (SPFS_O_TRUNC | SPFS_O_WRONLY)) {
      res = spfs_file_ftruncate(fs, fd, 0);
      if (res) {
        _fd_release(fs, fd);
        ERRUNLOCK(fs, res);
      }
    }
  }
  dbg("fh:"_SPIPRIi"\n", fd->hdl);
  SPFS_UNLOCK(fs);
  return fd->hdl;
}

spfs_file_t SPFS_creat(spfs_t *fs, const char *path) {
  return SPFS_open(fs, path, SPFS_O_WRONLY|SPFS_O_CREAT|SPFS_O_TRUNC, 0);
}

int SPFS_read(spfs_t *fs, spfs_file_t fh, void *buf, uint32_t len) {
  spfs_fd_t *fd;
  dbg("fh:"_SPIPRIi" len:"_SPIPRIi"\n", fh, len);
  SPFS_LOCK(fs);
  ERRUNLOCK(fs, check(fs));
  int res = _fd_resolve(fs, fh, &fd);
  ERRUNLOCK(fs, res);
  if ((fd->fd_oflags & SPFS_O_RDONLY) == 0) ERRUNLOCK(fs, -SPFS_ERR_NOT_READABLE);
  if (fd->fi.size == SPFS_FILESZ_UNDEF) {
    return 0;
  }
  len = spfs_min(len, fd->fi.size - fd->offset);
  res = spfs_file_read(fs, fd, fd->offset, len, (uint8_t *)buf);
  SPFS_UNLOCK(fs);
  if (res < 0)  ERRET(res);
  else          return res;
}

int SPFS_write(spfs_t *fs, spfs_file_t fh, const void *buf, uint32_t len) {
  spfs_fd_t *fd;
  dbg("fh:"_SPIPRIi" len:"_SPIPRIi"\n", fh, len);
  SPFS_LOCK(fs);
  ERRUNLOCK(fs, check(fs));
  int res = _fd_resolve(fs, fh, &fd);
  ERRUNLOCK(fs, res);
  if ((fd->fd_oflags & SPFS_O_WRONLY) == 0) ERRUNLOCK(fs, -SPFS_ERR_NOT_WRITABLE);

  if (fd->fd_oflags & SPFS_O_APPEND) {
    fd->offset = fd->fi.size;
  }
  res = spfs_file_write(fs, fd, fd->offset, len, (const uint8_t *)buf);
  SPFS_UNLOCK(fs);
  if (res < 0)  ERRET(res);
  else          return res;
}

int SPFS_close(spfs_t *fs, spfs_file_t fh) {
  spfs_fd_t *fd;
  dbg("fh:"_SPIPRIi"\n", fh);
  SPFS_LOCK(fs);
  ERRUNLOCK(fs, check(fs));
  int res = _fd_resolve(fs, fh, &fd);
  ERR(res);
  _fd_release(fs, fd);
  SPFS_UNLOCK(fs);
  return SPFS_OK;
}

int SPFS_remove(spfs_t *fs, const char *path) {
  dbg("path:%s\n", path);
  SPFS_LOCK(fs);
  ERRUNLOCK(fs, check(fs));
  int res = spfs_file_remove(fs, path);
  SPFS_UNLOCK(fs);
  ERRET(res);
}

int SPFS_lseek(spfs_t *fs, spfs_file_t fh, int offs, uint8_t whence) {
  spfs_fd_t *fd;
  dbg("fh:"_SPIPRIi" offs:"_SPIPRIi" whence:%s\n", fh, offs,
      (const char *[4]){"SET", "CUR", "END", "?"}[spfs_min(whence,3)]);
  SPFS_LOCK(fs);
  ERRUNLOCK(fs, check(fs));
  int res = _fd_resolve(fs, fh, &fd);
  ERRUNLOCK(fs, res);
  uint32_t sz = fd->fi.size;
  uint32_t set_offs = fd->offset;
  switch (whence) {
  case SPFS_SEEK_SET: set_offs = offs; break;
  case SPFS_SEEK_CUR: set_offs += offs; break;
  case SPFS_SEEK_END: set_offs = sz + offs; break;
  default: {
    SPFS_UNLOCK(fs);
    ERRET(-SPFS_ERR_ARG);
  }
  }

  if (set_offs > sz) set_offs = sz; // clamp, spfs does not allow empty regions
  fd->offset = set_offs;
  SPFS_UNLOCK(fs);
  return set_offs;
}

int SPFS_ftruncate(spfs_t *fs, spfs_file_t fh, uint32_t offset) {
  spfs_fd_t *fd;
  dbg("fh:"_SPIPRIi" offs:"_SPIPRIi"\n", fh, offset);
  SPFS_LOCK(fs);
  ERRUNLOCK(fs, check(fs));
  int res = _fd_resolve(fs, fh, &fd);
  ERRUNLOCK(fs, res);
  if ((fd->fd_oflags & SPFS_O_WRONLY) == 0) ERRUNLOCK(fs, -SPFS_ERR_NOT_WRITABLE);
  res = spfs_file_ftruncate(fs, fd, offset);
  SPFS_UNLOCK(fs);
  ERRET(res);
}

int SPFS_truncate(spfs_t *fs, const char *path, uint32_t offset) {
  spfs_fd_t *fd;
  dbg("path:%s offs:"_SPIPRIi"\n", path, offset);
  SPFS_LOCK(fs);
  ERRUNLOCK(fs, check(fs));
  int res = spfs_file_truncate(fs, path, offset);
  SPFS_UNLOCK(fs);
  ERRET(res);
}

int SPFS_opendir(spfs_t *fs, spfs_DIR *d, const char *path) {
  if (d == NULL) ERRET(-SPFS_ERR_ARG);
  (void)fs; (void)path;
  d->dpix = 0;
  return SPFS_OK;
}

static int _spfs_readdir_v(spfs_t *fs, uint32_t lu_entry, spfs_vis_info_t *info, void *varg) {
  spfs_DIR *d = (spfs_DIR *)varg;
  id_t special_id = spfs_signext(lu_entry >> SPFS_LU_FLAG_BITS, SPFS_BITS_ID(fs));
  if (special_id == SPFS_IDFREE || special_id == SPFS_IDDELE) return SPFS_VIS_CONT;
  id_t id = lu_entry >> SPFS_LU_FLAG_BITS;
  uint32_t luflags = lu_entry & ((1<<SPFS_LU_FLAG_BITS)-1);
  if ((luflags)) {
    return SPFS_VIS_CONT;
  }

  spfs_pixhdr_t pixhdr;
  int res = _page_ixhdr_read(fs, info->dpix, &pixhdr, 0);
  ERR(res);

  // check id redundancy
  if (pixhdr.phdr.id != id) {
    ERR(-SPFS_ERR_LU_PHDR_ID_MISMATCH);
  }
  // check IDX flag redundancy
  if ((pixhdr.phdr.p_flags & SPFS_PHDR_FL_IDX)) {
    ERR(-SPFS_ERR_LU_PHDR_FLAG_MISMATCH);
  }
  if (pixhdr.phdr.span == 0) {
    d->dpix = info->dpix+1;
    d->de.s.id = pixhdr.phdr.id;
    d->de.s.dpix = info->dpix;
    d->de.s.size = pixhdr.fi.size;
    d->de.s.type = pixhdr.fi.type;
    spfs_strncpy(d->de.s.name, (const char *)pixhdr.name, SPFS_CFG_FILE_NAME_SZ);
#if SPFS_CFG_FILE_META_SZ
    spfs_memcpy(d->de.s.meta, pixhdr.meta, SPFS_CFG_FILE_META_SZ);
#endif
    return SPFS_VIS_STOP;
  }
  return SPFS_VIS_CONT;
}

struct spfs_dirent *SPFS_readdir(spfs_t *fs, spfs_DIR *d) {
  if (d == NULL) return NULL;
  SPFS_LOCK(fs);
  if (check(fs)) {
    SPFS_UNLOCK(fs);
    return NULL;
  }
  if (d->dpix >= (pix_t)SPFS_DPAGES_MAX(fs)) {
    SPFS_UNLOCK(fs);
    return NULL;
  }
  int res = spfs_page_visit(fs, d->dpix, 0, d, _spfs_readdir_v, 0);
  SPFS_UNLOCK(fs);
  if (res == SPFS_OK) {
    return &d->de;
  } else {
    return NULL;
  }
}

int SPFS_closedir(spfs_t *fs, spfs_DIR *d) {
  (void)fs;
  if (d == NULL) ERRET(-SPFS_ERR_ARG);
  return SPFS_OK;
}
