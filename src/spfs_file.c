/*
 * spfs_file.c
 *
 *  Created on: Sep 9, 2017
 *      Author: petera
 */

//@REQUIRED_GCOV:85

#include "spfs_compile_cfg.h"
#include "spfs.h"
#include "spfs_lowlevel.h"
#include "spfs_file.h"

#undef _SPFS_DBG_PRE
#undef _SPFS_DBG_POST
#define _SPFS_DBG_PRE ANSI_COLOR_YELLOW"FI "
#define _SPFS_DBG_POST ANSI_COLOR_RESET
#include "spfs_dbg.h"
#undef dbg
#define dbg(_f, ...) dbg_fi(_f, ## __VA_ARGS__)


// TODO prime ix page searches with cached fd info

_SPFS_STATIC void _fd_init(spfs_t *fs, void *mem, uint32_t mem_sz) {
  spfs_memset(mem, 0x00, mem_sz);
  fs->run.fd_cnt = mem_sz / sizeof(spfs_fd_t);
  fs->run.fd_area = mem;
  dbg("filedescs:"_SPIPRIi"\n", fs->run.fd_cnt);
}

_SPFS_STATIC int _fd_claim(spfs_t *fs, spfs_fd_t **fd) {
  uint16_t i;
  int res = SPFS_OK;
  spfs_fd_t *fds = (spfs_fd_t *)fs->run.fd_area;
  for (i = 0; i < fs->run.fd_cnt; i++) {
    if (fds->hdl == 0) {
      *fd = fds;
      fds->hdl = i+1+fs->cfg.filehandle_offset;
      break;
    }
    fds++;
  }
  if (i >= fs->run.fd_cnt) res = -SPFS_ERR_OUT_OF_FILEDESCRIPTORS;
  ERRET(res);
}

_SPFS_STATIC int _fd_resolve(spfs_t *fs, spfs_file_t fh, spfs_fd_t **fd) {
  if (fh <  fs->cfg.filehandle_offset + 1 ||
      fh >= fs->cfg.filehandle_offset + 1 + fs->run.fd_cnt) {
    ERR(-SPFS_ERR_BAD_FILEDESCRIPTOR);
  }
  spfs_fd_t *fds = (spfs_fd_t *)fs->run.fd_area;
  *fd = &fds[fh - (fs->cfg.filehandle_offset + 1)];
  if ((*fd)->hdl == 0) {
    ERR(-SPFS_ERR_FILE_CLOSED);
  }
  ERRET(SPFS_OK);
}

_SPFS_STATIC void _fd_release(spfs_t *fs, spfs_fd_t *fd) {
  if (fd->hdl == 0) {
    dbg("warn: freeing free fd\n");
  }
  fd->hdl = 0;
}

static void _inform(spfs_t *fs, spfs_file_event_t event, id_t id, spfs_file_event_data_t *data) {
  uint16_t i;
  spfs_fd_t * fds = (spfs_fd_t *)fs->run.fd_area;
  dbg("event:%s id:"_SPIPRIid"\n",
      event == SPFS_F_EV_REMOVE_IX ? "REMOVEIX" :
      event == SPFS_F_EV_UPDATE_IX ? "UPDATEIX" :
      event == SPFS_F_EV_NEW_SIZE ?  "NEWSIZE " : "??",
          id);
  switch (event) {
  case SPFS_F_EV_REMOVE_IX:
    for (i = 0; i < fs->run.fd_cnt; i++) {
      if (fds->fi.id == id && data->remove.spix == 0 && fds->hdl > 0) {
        dbg("fd:"_SPIPRIi" id:"_SPIPRIid" removed, closing handle\n",
            fds->hdl, id);
        fds->hdl = 0;
      }
    }
    break;
  case SPFS_F_EV_UPDATE_IX:
    for (i = 0; i < fs->run.fd_cnt; i++) {
      if (fds->fi.id == id) {
        if (data->update.spix == 0) {
          dbg("fd:"_SPIPRIi" id:"_SPIPRIid" updated ix hdr to dpix:"_SPIPRIpg"\n",
              fds->hdl, id, data->update.dpix);
          fds->dpix_ixhdr = data->update.dpix;
        } else if (SPFS_OFFS2SPIX(fs, fds->offset) == data->update.spix) {
          dbg("fd:"_SPIPRIi" id:"_SPIPRIid" updated ix spix:"_SPIPRIsp" to dpix:"_SPIPRIpg"\n",
              fds->hdl, id, data->update.spix, data->update.dpix);
          fds->dpix_ix = data->update.dpix;
        }
      }
      fds++;
    }
    break;
  case SPFS_F_EV_NEW_SIZE:
    for (i = 0; i < fs->run.fd_cnt; i++) {
      if (fds->fi.id == id) {
        dbg("fd:"_SPIPRIi" id:"_SPIPRIid" updated size to "_SPIPRIi"\n",
            fds->hdl, id, data->size);
       fds->fi.size = data->size;
      }
      fds++;
    }
    break;
  }
}

static uint32_t _calc_write_meta_pages(spfs_t *fs, uint32_t cursz, uint32_t offset, uint32_t len) {
  const uint32_t sz_ix_0 = SPFS_IX_ENT_CNT(fs, 0) * SPFS_DPAGE_SZ(fs);
  const uint32_t sz_ix_n = SPFS_IX_ENT_CNT(fs, 1) * SPFS_DPAGE_SZ(fs);
  const uint8_t update_ixhdr_size = (cursz != SPFS_FILESZ_UNDEF) && (offset + len > cursz);
  const uint8_t update_ixhdr_map = (offset < sz_ix_0);
  if (update_ixhdr_map) {
    len -= sz_ix_0 - offset;
    offset = sz_ix_0;
  }
  return ((update_ixhdr_map || update_ixhdr_size) ? 1 : 0) +
      spfs_ceil(len, sz_ix_n);
}

static uint32_t _calc_trunc_pages(spfs_t *fs, uint32_t cursz, uint32_t offset) {
  if (offset == cursz) return 0;
  const uint32_t sz_ix_0 = SPFS_IX_ENT_CNT(fs, 0) * SPFS_DPAGE_SZ(fs);
  const uint8_t data_splice_needed = offset % SPFS_DPAGE_SZ(fs);
  const uint8_t offset_in_ixhdr = offset <= sz_ix_0;

  return
      // ixhdr: needed for the new size
      1 +
      // data: splice needed => new data page
      (data_splice_needed ? 1 : 0) +
      // index page needs update, unless this happens to be the
      // index header page, which is already updated due to size
      (offset_in_ixhdr ? 0 : 1);
}

typedef struct {
  uint32_t mask;
  const char *name;
  uint32_t size;
  uint32_t x_size;
  uint8_t f_flags;
  uint8_t *meta_data;
} _ixhdr_update_t;
static int _ixhdr_update(spfs_t *fs, _ixhdr_update_t *update,
                              pix_t src_dpix, pix_t dst_dpix) {
  spfs_pixhdr_t pixhdr;
  int res = _page_ixhdr_read(fs, src_dpix, &pixhdr, 0);
  ERR(res);
  if (update->mask & SPFS_IXHDR_UPD_FL_SIZE) {
    pixhdr.fi.size = update->size;
  }
  if (update->mask & SPFS_IXHDR_UPD_FL_X_SIZE) {
    pixhdr.fi.x_size = update->x_size;
  }
  if (update->mask & SPFS_IXHDR_UPD_FL_FLAGS) {
    pixhdr.fi.f_flags = update->f_flags;
  }
  if (update->mask & SPFS_IXHDR_UPD_FL_NAME) {
    spfs_strncpy((char *)pixhdr.name, update->name, SPFS_CFG_FILE_NAME_SZ);
  }
#if SPFS_CFG_FILE_META_SZ
  if (update->mask & SPFS_IXHDR_UPD_FL_META) {
    spfs_memcpy(pixhdr.meta, update->meta_data, SPFS_CFG_FILE_META_SZ);
  }
#endif
  res = spfs_page_ixhdr_write(fs, dst_dpix, &pixhdr, SPFS_C_UP);
  ERR(res);
  res = _page_copy(fs, _dpix2lpix(fs, dst_dpix), _dpix2lpix(fs, src_dpix),
                   1);
  ERRET(res);
}

static int _ix_get_entry(spfs_t *fs, id_t id, spix_t dspix, pix_t *entry_dpix, pix_t *ixdpix) {
  spix_t ixspix = SPFS_DSPIX2IXSPIX(fs, dspix);
  pix_t found_ixdpix;
  int res = spfs_page_find(fs, id, ixspix, SPFS_PAGE_FIND_FL_IX, &found_ixdpix);
  ERR(res);
  if (ixdpix) *ixdpix = found_ixdpix;
  spix_t ix_rel_entry;
  if (dspix < SPFS_IX_ENT_CNT(fs, 0)) {
    ix_rel_entry = dspix;
  } else {
    ix_rel_entry = (dspix - SPFS_IX_ENT_CNT(fs, 0)) % SPFS_IX_ENT_CNT(fs, 1);
  }
  uint32_t bitoffs = ix_rel_entry * SPFS_BITS_ID(fs);
  uint32_t addr = SPFS_DPIX2ADDR(fs, found_ixdpix) + bitoffs / 8;
  uint32_t len = spfs_ceil(bitoffs + SPFS_BITS_ID(fs), 8) - (bitoffs/8);
  uint8_t buf[5];
  res = _medium_read(fs, addr, buf, len, SPFS_T_META);
  ERR(res);
  bstr8 bs;
  bstr8_init(&bs, buf);
  bstr8_setp(&bs, bitoffs % 8);
  *entry_dpix = bstr8_rd(&bs, SPFS_BITS_ID(fs));
  ERRET(res);
}

static int _ixhdr_rewrite_flags(spfs_t *fs, pix_t dpix, uint8_t f_flags) {
  uint8_t buf[1 + spfs_ceil(SPFS_PIXHDR_FLAG_BITS, 8)];
  spfs_memset(buf, 0xff, sizeof(buf));
  bstr8 bs;
  bstr8_init(&bs, buf);
  uint32_t bitpos_flags = 32 + 32 + 8 * SPFS_CFG_FILE_NAME_SZ + SPFS_PIXHDR_TYPE_BITS;
  bstr8_setp(&bs, bitpos_flags % 8);
  bstr8_wr(&bs, SPFS_PIXHDR_FLAG_BITS, f_flags);
  int res;
  res = _medium_write(fs,
                      SPFS_DPIXHDR2ADDR(fs, dpix) + bitpos_flags / 8,
                      buf, sizeof(buf),
                      SPFS_C_UP | SPFS_T_META |
                      _SPFS_HAL_WR_FL_OVERWRITE | _SPFS_HAL_WR_FL_IGNORE_BITS);
  ERRET(res);
}



static int _file_mknod(spfs_t *fs, const char *name, uint8_t type, uint32_t x_sz,
                            const uint8_t *meta, spfs_fd_t *fd) {
  int res;
  dbg("name:\"%s\" type:" _SPIPRIi "\n", name, type);
  pix_t free_dpix = (pix_t)-1;
  id_t id = (id_t)-1;
  res = _id_find_free(fs, &id, name);
  ERR(res);
  res = _page_allocate_free(fs, &free_dpix, id, SPFS_LU_FL_INDEX);
  ERR(res);
  spfs_pixhdr_t ixphdr;
  ixphdr.phdr.id = id;
  ixphdr.phdr.span = 0;
  ixphdr.phdr.p_flags = 0xff & ~SPFS_PHDR_FL_IDX;
  ixphdr.fi.id = id;
  ixphdr.fi.size = SPFS_FILESZ_UNDEF;
  ixphdr.fi.type = type;
  ixphdr.fi.x_size = x_sz;
  ixphdr.fi.f_flags = 0xff;
  spfs_strncpy((char *)&ixphdr.name, name, SPFS_CFG_FILE_NAME_SZ);
#if SPFS_CFG_FILE_META_SZ
  if (meta) {
    spfs_memcpy(&ixphdr.meta, meta, SPFS_CFG_FILE_META_SZ);
  } else {
    spfs_memset(&ixphdr.meta, 0xff, SPFS_CFG_FILE_META_SZ);
  }
#else
  (void)meta;
#endif
  res = spfs_page_ixhdr_write(fs, free_dpix, &ixphdr, SPFS_C_UP);
  ERR(res);
  if (fd) {
    fd->offset = 0;
    fd->dpix_ixhdr = free_dpix;
    spfs_memcpy(&fd->fi, &ixphdr.fi, sizeof(spfs_fi_t));
  }
  ERRET(res);
}

_SPFS_STATIC int spfs_file_create(spfs_t *fs, spfs_fd_t *fd, const char *name) {
  int res = _file_mknod(fs, name, SPFS_PIXHDR_TY_FILE, -1, NULL, fd);
  ERRET(res);
}

_SPFS_STATIC int spfs_file_create_fix(spfs_t *fs, spfs_fd_t *fd, const char *name, uint32_t fixed_size) {
  int res = _file_mknod(fs, name, SPFS_PIXHDR_TY_FIXFILE, fixed_size, NULL, fd);
  ERRET(res);
}

//_SPFS_STATIC int spfs_file_create_rot(spfs_t *fs, spfs_fd_t *fd, const char *name, uint32_t rot_size) {
//  int res = _file_mknod(fs, name, SPFS_PIXHDR_TY_ROTFILE, rot_size, NULL, fd);
//  ERRET(res);
//}

typedef struct {
  spfs_pixhdr_t *pixhdr;
  const char *name;
  pix_t dpix;
} _file_find_varg_t;
static int _file_find_v(spfs_t *fs, uint32_t lu_entry, spfs_vis_info_t *info, void *varg) {
  _file_find_varg_t *arg = (_file_find_varg_t *)varg;
  id_t id = spfs_signext(lu_entry >> SPFS_LU_FLAG_BITS, SPFS_BITS_ID(fs));
  uint32_t luflags = lu_entry & ((1<<SPFS_LU_FLAG_BITS)-1);
  // only check real ids, with the lu extra flag cleared indicating it is an index
  if (luflags
      || id == SPFS_IDDELE || id == SPFS_IDFREE || id == SPFS_IDJOUR)
    return SPFS_VIS_CONT;
  id = lu_entry >> SPFS_LU_FLAG_BITS;
  int res = _page_ixhdr_read(fs, info->dpix, arg->pixhdr, 0);
  ERR(res);
  // check id redundancy
  if (arg->pixhdr->phdr.id != id) {
    ERR(-SPFS_ERR_LU_PHDR_ID_MISMATCH);
  }
  // check IDX flag redundancy
  if (arg->pixhdr->phdr.p_flags & SPFS_PHDR_FL_IDX) {
    ERR(-SPFS_ERR_LU_PHDR_FLAG_MISMATCH);
  }
  // make sure the index has span 0, meaning index header
  if (arg->pixhdr->phdr.span) {
    return SPFS_VIS_CONT;
  }

  // check name
  if (spfs_strncmp(arg->name, (char *)arg->pixhdr->name, SPFS_CFG_FILE_NAME_SZ) == 0) {
    arg->dpix = info->dpix;
    return SPFS_VIS_STOP;
  }
  return SPFS_VIS_CONT;
}
_SPFS_STATIC int spfs_file_find(spfs_t *fs, const char *name, pix_t *dpix, spfs_pixhdr_t *pixhdr) {
  dbg("name:\"%s\"\n", name);
  spfs_assert(pixhdr);
  _file_find_varg_t arg = {.name = name, .pixhdr = pixhdr};
  int res = spfs_page_visit(fs, fs->run.dpix_find_cursor, fs->run.dpix_find_cursor, &arg,
                   _file_find_v, 0);
  if (res == -SPFS_ERR_VIS_END) res = -SPFS_ERR_FILE_NOT_FOUND;
  ERR(res);
  fs->run.dpix_find_cursor = arg.dpix;
  if (dpix) *dpix = arg.dpix;
  dbg("name:\"%s\" found dpix:"_SPIPRIpg " id:"_SPIPRIid" sz:"_SPIPRIi" type:"_SPIPRIi"\n",
      name, arg.dpix, pixhdr->phdr.id, pixhdr->fi.size, pixhdr->fi.type);

  ERRET(SPFS_OK);
}





_SPFS_STATIC int spfs_file_visit(spfs_t *fs,
                                 spfs_fi_t *fi, pix_t dpix_ixhdr,
                                 uint32_t offset, uint32_t len,
                                 uint8_t create_memory_ix,
                                 void *varg,
                                 spfs_file_visitor_t v, spfs_file_ix_visitor_t vix,
                                 uint32_t v_flags) {
  int res = SPFS_OK;

  // check if this is a fixed file, cap it if so
  if (fi->type == SPFS_PIXHDR_TY_FIXFILE) {
    if (offset >= fi->x_size) {
      ERR(-SPFS_ERR_EOF);
    }
    if (offset + len > fi->x_size) {
      len = fi->x_size - offset;
      dbg("fixed file, capping length to "_SPIPRIi"\n", len);
    }
  }
//  // check if this is a rotating file, adjust offset if so
//  else if (fi->type == SPFS_PIXHDR_TY_ROTFILE) { // TODO
//    if (fi->size > fi->x_size) {
//      // rotating file is filled, adjust offset
//      offset += fi->size - fi->x_size;
//    }
//  }

  spfs_file_vis_info_t info =
    {.offset = offset, .ixspix = -1, .fi = fi,
     .dpix_ix = -1, .dpix_ixhdr = dpix_ixhdr, .v_flags = v_flags};

  while (info.offset < offset+len) {
    if (SPFS_OFFS2IXSPIX(fs, info.offset) != info.ixspix) {
      // need to load a new index page
      if (vix) res = vix(fs, 0, res, &info, varg);
      ERRGO(res);

      // find and load new index page
      info.ixspix = SPFS_OFFS2IXSPIX(fs, info.offset);
      if (info.ixspix > 0
          && fi->size != SPFS_FILESZ_UNDEF
          && info.ixspix > SPFS_OFFS2IXSPIX(fs, fi->size-1)
          && create_memory_ix) {
        // started reading beyond file length, create memory index
        dbg("creating memory index spix "_SPIPRIid"\n", info.ixspix);
        info.ix_constructed = 1;
        info.dpix_ix = -1;
        spfs_phdr_t phdr = {.id = fi->id, .span = info.ixspix, .p_flags = 0xff & ~SPFS_PHDR_FL_IDX};
        spfs_memset(fs->run.work2, 0xff, SPFS_DPAGE_SZ(fs));
        _phdr_wrmem(fs, fs->run.work2 + SPFS_DPHDROFFS(fs), &phdr);
      } else {
        dbg("reading index id:"_SPIPRIid" spix:"_SPIPRIid"\n", info.fi->id, info.ixspix);
        info.ix_constructed = 0;
        res = spfs_page_find(fs, fi->id, info.ixspix, SPFS_PAGE_FIND_FL_IX, &info.dpix_ix);
        ERRGO(res);
        uint32_t addr = SPFS_DPIX2ADDR(fs, info.dpix_ix);
        res = _medium_read(fs, addr, fs->run.work2, SPFS_CFG_LPAGE_SZ(fs), SPFS_T_META);
        ERRGO(res);
      }
      uint8_t *ix = fs->run.work2;
      barr8_init(&info.ixarr, ix, SPFS_BITS_ID(fs));

      if (info.ixspix == 0) info.dpix_ixhdr = info.dpix_ix;
    }
    // get the dpix from index page, and calculate valid length
    info.ixent = (info.offset - SPFS_IXSPIX2OFFS(fs, info.ixspix)) / SPFS_DPAGE_SZ(fs);
    pix_t dpix = barr8_get(&info.ixarr, info.ixent);
    spfs_assert(create_memory_ix || dpix < (pix_t)SPFS_DPAGES_MAX(fs));
    uint32_t dlen = SPFS_DPAGE_SZ(fs) - (info.offset % SPFS_DPAGE_SZ(fs));
    info.len = spfs_min(offset+len-info.offset, dlen);
    // callback
    if (v) res = v(fs, dpix, &info, varg);
    ERRGO(res);
    info.offset += info.len;
  }
  err:
  info.len = 0;

  // check if there are any final changes needing persisting
  int res2 = SPFS_OK;
  if (vix) {
    res2 = vix(fs, 1, res, &info, varg);
  }

  ERRET(res != SPFS_OK ? res : res2);
}

typedef struct {
  uint8_t *dst;
  uint32_t bytes_written;
} _file_read_varg_t;
static int _file_read_v(spfs_t *fs, pix_t dpix, spfs_file_vis_info_t *info, void *varg) {
  int res;
  dbg("read dpix:"_SPIPRIpg"\n", dpix);
  _file_read_varg_t *arg = (_file_read_varg_t *)varg;
  uint32_t addr = SPFS_DPIX2ADDR(fs, dpix) + (info->offset % SPFS_DPAGE_SZ(fs));
  res = _medium_read(fs, addr, arg->dst, info->len, SPFS_T_DATA);
  ERR(res);
  arg->dst += info->len;
  arg->bytes_written += info->len;
  ERRET(res);
}
_SPFS_STATIC int spfs_file_read(spfs_t *fs, spfs_fd_t *fd, uint32_t offs, uint32_t len, uint8_t *dst) {
  int res = SPFS_OK;
  _file_read_varg_t arg = {.dst = dst, .bytes_written = 0};
  uint32_t offs_ixdpix = SPFS_OFFS2IXSPIX(fs, offs);

  if (offs_ixdpix == SPFS_OFFS2IXSPIX(fs, fd->offset) || offs_ixdpix == 0) {
    // prime the index page search to start at known ix_dpix, if known that is
    fs->run.dpix_find_cursor = offs_ixdpix ? fd->dpix_ix : fd->dpix_ixhdr;
  }
  res = spfs_file_visit(fs, &fd->fi, fd->dpix_ixhdr, offs, len, 0, &arg,
                        _file_read_v, NULL, fd->fd_oflags);

  fd->offset = offs + arg.bytes_written;

  ERR(res);

  res = arg.bytes_written;
  return res;
}


#define SPFS_FWR_IXDIRTY_NO       (0)
#define SPFS_FWR_IXDIRTY_REWRITE  (1)
#define SPFS_FWR_IXDIRTY_REPLACE  (3)
#define SPFS_FWR_IXDIRTY_NEW      (4)

typedef struct {
  // data to write
  const uint8_t *src;
  // number of bytes currently written to medium
  uint32_t bytes_written;
  // dirty state of index in work memory 2
  uint8_t ixdirty;
} _file_write_varg_t;
/**
 *  Visited each time a new index page is about to be loaded.
 *  The current (modified by _file_write_v) index is in work2 buffer.
 *  Also called when all is written or on error.
 */
static int _file_write_vix(spfs_t *fs, uint8_t final, int respre,
                           spfs_file_vis_info_t *info, void *varg) {
  _file_write_varg_t *arg = (_file_write_varg_t *)varg;
  dbg("index callback, ixspix:"_SPIPRIpg" final:%s ixdirty:"_SPIPRIi" ixhdr_filesz:"_SPIPRIi" file_offs:"_SPIPRIi"\n",
      info->ixspix, final ? "YES":"NO", arg->ixdirty, info->fi->size, info->offset);

  if (info->ixspix == (spix_t)-1) return SPFS_OK; // no ix loaded

  if (info->v_flags & SPFS_O_REWR) return SPFS_OK; // do not touch the inidices

  // if this is a constructed index, it is totally new and have
  // no data page yet
  if (info->ix_constructed) arg->ixdirty = SPFS_FWR_IXDIRTY_NEW;

  // here, we need to update
  // 1) the index page data in memory, and possibly
  // 2) the length in the index header page.
  // 1) and 2) might be the same page.
  // Also, one/both pages may simply be updated or must be fully rewritten.

  int res = SPFS_OK;
  // current filesize as in persisted data pages - not yet in index
  uint32_t data_filesz = info->offset;
  // if the memory image also is the index header
  uint8_t ixhdr_in_mem = info->ixspix == 0;
  // if the file size need to be updated
  uint8_t ixhdr_sz_update =
      (info->fi->size != SPFS_FILESZ_UNDEF && info->fi->size < data_filesz)
      || info->fi->size == SPFS_FILESZ_UNDEF;

  //
  // first write the stuff from memory - this might also be the ix hdr
  //
  if (ixhdr_in_mem) {
    // Check if file size needs to be updated -
    // force replace when length in index header page is defined and the ix header
    // cannot be rewritten
    dbg("ix update, mem is also ix hdr\n");
    spfs_assert(arg->ixdirty != SPFS_FWR_IXDIRTY_NEW);
    if (ixhdr_sz_update) {
      if (info->fi->size != SPFS_FILESZ_UNDEF) arg->ixdirty = SPFS_FWR_IXDIRTY_REPLACE;
      _pixhdr_wrmem_sz(fs, fs->run.work2 + SPFS_DPIXHDROFFS(fs), data_filesz);
    }
  }

  if (arg->ixdirty == SPFS_FWR_IXDIRTY_REPLACE
      || arg->ixdirty == SPFS_FWR_IXDIRTY_NEW) {
    // this is an index page that either replaces an old existing one, or a brand new
    dbg("ix update, %s\n", arg->ixdirty == SPFS_FWR_IXDIRTY_REPLACE ? "replace" : "new");
    pix_t new_dpix_ix;
    res = _page_allocate_free(fs, &new_dpix_ix, info->fi->id, SPFS_LU_FL_INDEX);
    ERR(res);
    // TODO do we need to mark the old first that it is about to be deleted?

    // write updated index page
    dbg("ix update, new ix, dpix:"_SPIPRIpg", from mem\n", new_dpix_ix);
    res = _medium_write(fs, SPFS_DPIX2ADDR(fs, new_dpix_ix),
                        fs->run.work2, SPFS_CFG_LPAGE_SZ(fs), SPFS_T_META | SPFS_C_UP);
    ERR(res);
    if (arg->ixdirty != SPFS_FWR_IXDIRTY_NEW) {
      // have an old existing index page, delete it
      dbg("ix update, deleting old ix dpix:"_SPIPRIpg"\n", info->dpix_ix);
      res = _lu_page_delete(fs, info->dpix_ix);
      fs->run.pused--;
      ERR(res);
    }
    if (ixhdr_in_mem)
      info->dpix_ixhdr = new_dpix_ix;
    else
      info->dpix_ix = new_dpix_ix;
    spfs_file_event_data_t evdata = {.update={.spix = info->ixspix, .dpix = new_dpix_ix}};
    _inform(fs, SPFS_F_EV_UPDATE_IX, info->fi->id, &evdata);
  } else if (arg->ixdirty == SPFS_FWR_IXDIRTY_REWRITE) {
    dbg("ix update, rewrite ix, dpix:"_SPIPRIpg", from mem\n", info->dpix_ix);
    // just overwrite current index page
    res = _medium_write(fs, SPFS_DPIX2ADDR(fs, info->dpix_ix),
                        fs->run.work2, SPFS_CFG_LPAGE_SZ(fs),
                        SPFS_T_META | SPFS_C_UP | _SPFS_HAL_WR_FL_OVERWRITE);
    ERR(res);
  }

  //
  // then, see if we need to update the index header
  //
  if (!ixhdr_in_mem && ixhdr_sz_update) {
    // here, the index header must be updated to the new size - and it is not in memory
    dbg("ixhdr update, size:"_SPIPRIi", dpix:"_SPIPRIpg"\n", data_filesz, info->dpix_ixhdr);
    pix_t new_dpix_ixhdr;
    res = _page_allocate_free(fs, &new_dpix_ixhdr, info->fi->id, SPFS_LU_FL_INDEX);
    ERR(res);
    dbg("ixhdr update, new ixhdr dpix:"_SPIPRIpg"\n", new_dpix_ixhdr);
    // update size in memory
    _ixhdr_update_t update = {.mask = SPFS_IXHDR_UPD_FL_SIZE, .size = data_filesz};
    res = _ixhdr_update(fs, &update, info->dpix_ixhdr, new_dpix_ixhdr);
    ERR(res);
    // delete old index header page
    dbg("ixhdr update, deleting old ixhdr dpix:"_SPIPRIpg"\n", info->dpix_ixhdr);
    res = _lu_page_delete(fs, info->dpix_ixhdr);
    fs->run.pused--;
    ERR(res);
    info->dpix_ixhdr = new_dpix_ixhdr;
    spfs_file_event_data_t evdata = {.update={.spix = 0, .dpix = new_dpix_ixhdr}};
    _inform(fs, SPFS_F_EV_UPDATE_IX, info->fi->id, &evdata);
  }

  if (ixhdr_sz_update) {
    info->fi->size = data_filesz;
    spfs_file_event_data_t evdata = {.size = data_filesz};
    _inform(fs, SPFS_F_EV_NEW_SIZE, info->fi->id, &evdata);
  }
  arg->ixdirty = SPFS_FWR_IXDIRTY_NO;
  ERRET(res);
}
/**
 * Visited for each entry in index lookup.
 */
static int _file_write_v(spfs_t *fs, pix_t dpix,
                         spfs_file_vis_info_t *info, void *varg) {
  int res;
  dbg("dpix:"_SPIPRIpg" id:"_SPIPRIid" ixdpix:"_SPIPRIpg" ent:"_SPIPRIi" ixspix:"_SPIPRIi" len:"_SPIPRIi" offs:"_SPIPRIi"\n",
      dpix, info->fi->id, info->dpix_ix, info->ixent, info->ixspix, info->len, info->offset);
  _file_write_varg_t *arg = (_file_write_varg_t *)varg;

  // check if the entry we are writing to has a page or not
  uint8_t existing_ixentry = spfs_signext(dpix, SPFS_BITS_ID(fs)) != SPFS_IDFREE;
  // this is the new data page and also index entry
  pix_t new_dpix_ixentry = (pix_t)-1;
  // offset in data page for written data
  uint32_t page_offset = info->offset % SPFS_DPAGE_SZ(fs);

  // tell index visitor whether it is ok to just rewrite or if we must replace the index
  uint8_t ixdirty = 0;

  spfs_assert(page_offset + info->len <= SPFS_DPAGE_SZ(fs));
  spfs_assert(page_offset < SPFS_DPAGE_SZ(fs));
  spfs_assert(info->len > 0);

  uint8_t *work = fs->run.work1;

  spfs_phdr_t phdr = {.id = info->fi->id, .span = SPFS_OFFS2SPIX(fs, info->offset), .p_flags = ~0};
#if SPFS_CFG_SENSITIVE_DATA
  if (info->v_flags & SPFS_O_SENS  ) phdr.p_flags &= ~SPFS_PHDR_FL_ZER;
#endif

  if (info->v_flags & SPFS_O_REWR) {

    // *** this is a rewrite of existing page

    dbg("rewriting existing page\n");
    res = _medium_write(fs, SPFS_DPIX2ADDR(fs, dpix) + page_offset,
        arg->src, info->len,
        SPFS_T_DATA | SPFS_C_UP | _SPFS_HAL_WR_FL_OVERWRITE | _SPFS_HAL_WR_FL_IGNORE_BITS);
    ERR(res);
#if SPFS_CFG_SENSITIVE_DATA
    if (info->v_flags & SPFS_O_SENS) {
      res = spfs_page_hdr_write(fs, dpix, &phdr, SPFS_C_UP | _SPFS_HAL_WR_FL_OVERWRITE);
      ERR(res);
    }
#endif
    ixdirty = SPFS_FWR_IXDIRTY_NO;

  } else if (!existing_ixentry) {

    // *** this is a brand new data page

    dbg("appending new page\n");
    res = _page_allocate_free(fs, &new_dpix_ixentry, info->fi->id, SPFS_LU_FL_DATA);
    ERR(res);
    // create new page with data
    spfs_memset(work, 0xff, SPFS_CFG_LPAGE_SZ(fs));
    _phdr_wrmem(fs, work + SPFS_DPHDROFFS(fs), &phdr);
    spfs_memcpy(work + page_offset, arg->src, info->len);
    // and write it
    res = _medium_write(fs, SPFS_DPIX2ADDR(fs, new_dpix_ixentry), work,
              SPFS_CFG_LPAGE_SZ(fs), SPFS_T_DATA | SPFS_C_UP);
    ERR(res);
    ixdirty = SPFS_FWR_IXDIRTY_REWRITE; // as the existing index entry is 0xff.., we can just rewrite it

  } else {

    // *** this represents either rewriting end of an existing data page filled
    //     with 0xff (no new data page needed, no index header update), or
    //     updating an existing datapage with data overwriting the previous

    uint8_t new_datapage = 1;
    if (info->len == SPFS_DPAGE_SZ(fs)) {
      // update existing full page, no need to merge with existing
      dbg("overwriting full page\n");
      spfs_assert(page_offset == 0);
      res = _page_allocate_free(fs, &new_dpix_ixentry, info->fi->id, SPFS_LU_FL_DATA);
      ERR(res);
      spfs_memset(work, 0xff, SPFS_CFG_LPAGE_SZ(fs));
      _phdr_wrmem(fs, work + SPFS_DPHDROFFS(fs), &phdr);
      spfs_memcpy(work, arg->src, info->len);
    } else {
      // partial rewrite of page, either at end (the 0xffs) or amidst existing data
      if (info->fi->size == SPFS_FILESZ_UNDEF || info->offset >= info->fi->size) {
        // .. just at the end of page where no data is yet written -> simply rewrite
        dbg("appending existing page\n");
        res = _medium_write(fs, SPFS_DPIX2ADDR(fs, dpix) + page_offset,
            arg->src, info->len, SPFS_T_DATA | SPFS_C_UP);
        ERR(res);
#if SPFS_CFG_SENSITIVE_DATA
        if (info->v_flags & SPFS_O_SENS  ) {
          res = spfs_page_hdr_write(fs, dpix, &phdr, SPFS_C_UP | _SPFS_HAL_WR_FL_OVERWRITE);
          ERR(res);
        }
#endif
        new_datapage = 0;
      } else {
        // .. else it is overwriting actual data -> load, merge and replace
        dbg("overwrite partial page offs:"_SPIPRIi", len:"_SPIPRIi"\n",
            page_offset, info->len);
        res = _page_allocate_free(fs, &new_dpix_ixentry, info->fi->id, SPFS_LU_FL_DATA);
        ERR(res);
        // read existing data
        res = _medium_read(fs, SPFS_DPIX2ADDR(fs, dpix), work, SPFS_CFG_LPAGE_SZ(fs),
                           SPFS_T_DATA | SPFS_T_META);
        ERR(res);
#if SPFS_CFG_SENSITIVE_DATA
        spfs_phdr_t phdr_existing;
        _phdr_rdmem(fs, work + SPFS_DPHDROFFS(fs), &phdr_existing);
        phdr.p_flags &= (phdr_existing.p_flags & SPFS_PHDR_FL_ZER) == 0 ? ~SPFS_PHDR_FL_ZER : ~0;
        _phdr_wrmem(fs, work + SPFS_DPHDROFFS(fs), &phdr);
#endif
        // overwrite with new data
        spfs_memcpy(work + page_offset, arg->src, info->len);
      }
    }

    if (new_datapage) {
      // got us a new data page, store it
      spfs_assert(new_dpix_ixentry != (pix_t)-1);
      ixdirty = SPFS_FWR_IXDIRTY_REPLACE; // new data page was needed, must update index
      dbg("replace old data dpix:"_SPIPRIpg" with dpix:"_SPIPRIpg"\n", dpix, new_dpix_ixentry);
      res = _medium_write(fs, SPFS_DPIX2ADDR(fs, new_dpix_ixentry), work,
                SPFS_CFG_LPAGE_SZ(fs), SPFS_T_DATA | SPFS_T_META | SPFS_C_UP);
      ERR(res);
    } else {
      // we've just tampered with an existing data page
      dbg("rewritten data dpix:"_SPIPRIpg"\n", dpix);
      new_dpix_ixentry = dpix;
      ixdirty = SPFS_FWR_IXDIRTY_NO; // no new data page was needed, so keep the index as it is
    }

    if (new_datapage) {
      // again, got us a new data page, delete old
      res = _lu_page_delete(fs, dpix);
      fs->run.pused--;
      ERR(res);
    }

  } // append/overwrite

  // update index dirty status
  arg->ixdirty |= ixdirty;
  // advance counters
  arg->src += info->len;
  arg->bytes_written += info->len;
  // update entry in memory index page
  barr8_set(&info->ixarr, info->ixent, new_dpix_ixentry);
  ERRET(res);
}
_SPFS_STATIC int spfs_file_write(spfs_t *fs, spfs_fd_t *fd, uint32_t offs, uint32_t len, const uint8_t *src) {
  int res = SPFS_OK;
  pix_t offs_ixdpix = SPFS_OFFS2IXSPIX(fs, offs);

  if (offs_ixdpix == SPFS_OFFS2IXSPIX(fs, fd->offset)
      || offs_ixdpix == 0) {
    // prime the index page search to start at known ix_dpix, if known that is
    fs->run.dpix_find_cursor = offs_ixdpix ? fd->dpix_ix : fd->dpix_ixhdr;
  }

//  if (fd->fi.type == SPFS_PIXHDR_TY_ROTFILE) {
//    if (len > fd->fi.x_size) {
//      // ignore data that will be overwritten by rotation
//      src += len - fd->fi.x_size;
//      len = fd->fi.x_size;
//    }
//  }

  // check if this is a rewrite, cap it if needed or error
  if (fd->fd_oflags & SPFS_O_REWR) {
    if (fd->fi.size == SPFS_FILESZ_UNDEF || offs >= fd->fi.size) {
      ERR(-SPFS_ERR_EOF);
    }
    if (offs + len > fd->fi.size) {
      len = fd->fi.size - offs;
      dbg("rewrite, capping length to "_SPIPRIi"\n", len);
    }
  }

  dbg("write id:"_SPIPRIid", size "_SPIPRIi", @ offset "_SPIPRIi", "_SPIPRIi" bytes\n",
      fd->fi.id, fd->fi.size, offs, len);
  _file_write_varg_t arg = {.src = src, .bytes_written = 0};
  res = spfs_file_visit(fs, &fd->fi, fd->dpix_ixhdr, offs, len, 1, &arg,
                        _file_write_v, _file_write_vix, fd->fd_oflags);

  fd->offset = offs + arg.bytes_written;

  ERR(res);

  res = arg.bytes_written;
  return res;
}

static int _file_remove_vix(spfs_t *fs, uint8_t final, int respre,
                           spfs_file_vis_info_t *info, void *varg) {
  if (info->ixspix == (spix_t)-1) return SPFS_OK; // no ix loaded
  if (info->ixspix == 0) return SPFS_OK; // wait with removing the ix hdr
  dbg("delete index dpix:"_SPIPRIpg" spix:"_SPIPRIsp"\n", info->dpix_ix, info->ixspix);
  int res = _lu_page_delete(fs, info->dpix_ix);
  ERR(res);
  spfs_file_event_data_t evdata = {.remove={.spix = info->ixspix}};
  _inform(fs, SPFS_F_EV_REMOVE_IX, info->fi->id, &evdata);

  ERRET(res);
}
static int _file_remove_v(spfs_t *fs, pix_t dpix,
                         spfs_file_vis_info_t *info, void *varg) {
  dbg("delete data dpix:"_SPIPRIpg" spix:"_SPIPRIsp"\n", dpix, SPFS_OFFS2SPIX(fs, info->offset));
  int res = _lu_page_delete(fs, dpix);
  ERRET(res);
}
_SPFS_STATIC int spfs_file_fremove(spfs_t *fs, spfs_fd_t *fd) {
  int res;
  dbg("remove id:"_SPIPRIid", size "_SPIPRIi"\n", fd->fi.id, fd->fi.size);
  pix_t dpix_ixhdr = fd->dpix_ixhdr;
  if (fd->fi.size != SPFS_FILESZ_UNDEF) {
    res = spfs_file_visit(fs, &fd->fi, dpix_ixhdr, 0, fd->fi.size, 0, NULL,
                          _file_remove_v, _file_remove_vix, fd->fd_oflags);
    ERR(res);
  }
  dbg("delete index header dpix:"_SPIPRIpg"\n", dpix_ixhdr);
  res = _lu_page_delete(fs, dpix_ixhdr);
  ERR(res);
  spfs_file_event_data_t evdata = {.remove={.spix = 0}};
  _inform(fs, SPFS_F_EV_REMOVE_IX, fd->fi.id, &evdata);

  ERRET(res);
}
_SPFS_STATIC int spfs_file_remove(spfs_t *fs, const char *path) {
  pix_t dpix_ixhdr;
  spfs_pixhdr_t pixhdr;
  int res = spfs_file_find(fs, path, &dpix_ixhdr, &pixhdr);
  ERR(res);
  if (pixhdr.fi.size != SPFS_FILESZ_UNDEF) {
    res = spfs_file_visit(fs, &pixhdr.fi, dpix_ixhdr, 0, pixhdr.fi.size, 0, NULL,
                          _file_remove_v, _file_remove_vix, 0);
    ERR(res);
  }
  dbg("delete index header dpix:"_SPIPRIpg"\n", dpix_ixhdr);
  res = _lu_page_delete(fs, dpix_ixhdr);
  ERR(res);
  spfs_file_event_data_t evdata = {.remove={.spix = 0}};
  _inform(fs, SPFS_F_EV_REMOVE_IX, pixhdr.fi.id, &evdata);

  ERRET(res);
}


#define SPFS_FTR_IXDIRTY_UNDEFINED  (0)
#define SPFS_FTR_IXDIRTY_DELETE     (1)
#define SPFS_FTR_IXDIRTY_REPLACE    (2)
typedef struct {
  // target size
  uint32_t target_size;
  // what to do with this index
  uint8_t ixaction;
  // if index header is updated or not
  uint8_t ixhdr_updated;
} _file_trunc_varg_t;
static int _file_trunc_vix(spfs_t *fs, uint8_t final, int respre,
                           spfs_file_vis_info_t *info, void *varg) {
  _file_trunc_varg_t * arg = (_file_trunc_varg_t *)varg;
  int res = SPFS_OK;
  if (info->ixspix == (spix_t)-1 || respre < 0) return SPFS_OK; // no ix loaded, or error
  if (final && arg->ixhdr_updated == 0) {
    // we're done, and the index header is not yet updated
    pix_t new_dpix_ixhdr;
    res = _page_allocate_free(fs, &new_dpix_ixhdr, info->fi->id, SPFS_LU_FL_INDEX);
    ERR(res);
    _ixhdr_update_t ixup = {.mask = SPFS_IXHDR_UPD_FL_SIZE, .size = arg->target_size};
    res = _ixhdr_update(fs, &ixup, info->dpix_ixhdr, new_dpix_ixhdr);
    ERR(res);
    res = _lu_page_delete(fs, info->dpix_ixhdr);
    ERR(res);
    spfs_file_event_data_t evdata_movement = {.update={.spix = 0, .dpix = new_dpix_ixhdr}};
    _inform(fs, SPFS_F_EV_UPDATE_IX, info->fi->id, &evdata_movement);
    spfs_file_event_data_t evdata_size = {.size = arg->target_size};
    _inform(fs, SPFS_F_EV_NEW_SIZE, info->fi->id, &evdata_size);
    // NOTE: RETURN
    ERRET(res);
  }

  if (info->ixspix == 0) {
    // this is the ix header, set size also
    dbg("update index hdr, set size "_SPIPRIi"\n", arg->target_size);
    _pixhdr_wrmem_sz(fs, fs->run.work2 + SPFS_DPIXHDROFFS(fs), arg->target_size);
  }
  if (arg->ixaction == SPFS_FTR_IXDIRTY_DELETE) {
    dbg("delete index dpix:"_SPIPRIpg" spix:"_SPIPRIsp"\n", info->dpix_ix, info->ixspix);
    res = _lu_page_delete(fs, info->dpix_ix);
    ERR(res);
    spfs_file_event_data_t evdata = {.remove={.spix = info->ixspix}};
    _inform(fs, SPFS_F_EV_REMOVE_IX, info->fi->id, &evdata);
  } else if (arg->ixaction == SPFS_FTR_IXDIRTY_REPLACE) {
    dbg("update index dpix:"_SPIPRIpg" spix:"_SPIPRIsp"\n", info->dpix_ix, info->ixspix);
    uint32_t new_dpix_ix;
    res = _page_allocate_free(fs, &new_dpix_ix, info->fi->id, SPFS_LU_FL_INDEX);
    ERR(res);
    dbg("replace old index dpix:"_SPIPRIpg" with dpix:"_SPIPRIpg"\n", info->dpix_ix, new_dpix_ix);

    res = _medium_write(fs, SPFS_DPIX2ADDR(fs, new_dpix_ix), fs->run.work2,
              SPFS_CFG_LPAGE_SZ(fs), SPFS_T_META | SPFS_C_UP);
    ERR(res);
    res = _lu_page_delete(fs, info->dpix_ix);
    ERR(res);
    spfs_file_event_data_t evdata = {.update={.spix = info->ixspix, .dpix = new_dpix_ix}};
    _inform(fs, SPFS_F_EV_UPDATE_IX, info->fi->id, &evdata);
    if (info->ixspix == 0) {
      spfs_file_event_data_t evdata_size = {.size = arg->target_size};
      _inform(fs, SPFS_F_EV_NEW_SIZE, info->fi->id, &evdata_size);
    }
  } else {
    spfs_assert(0);
  }
  if (!final && info->ixspix == 0) {
    // this has updated the ix hdr
    arg->ixhdr_updated = 1;
  }
  arg->ixaction = SPFS_FTR_IXDIRTY_UNDEFINED;
  ERRET(res);
}
static int _file_trunc_v(spfs_t *fs, pix_t dpix,
                         spfs_file_vis_info_t *info, void *varg) {
  int res;
  uint8_t *work = fs->run.work1;
  _file_trunc_varg_t * arg = (_file_trunc_varg_t *)varg;
  pix_t new_dpix_ixentry = (pix_t)-1; // default to set index to unused page
  if (arg->ixaction == SPFS_FTR_IXDIRTY_UNDEFINED) {
    // delete index page iff it is not the header,
    // and all entries in the index are deleted
    arg->ixaction =
        info->ixspix != 0 && info->ixent == 0  ? SPFS_FTR_IXDIRTY_DELETE : SPFS_FTR_IXDIRTY_REPLACE;
  }
  uint32_t page_offset = info->offset % SPFS_DPAGE_SZ(fs);
  if (page_offset == 0) {
    // remove full page
    dbg("delete full data dpix:"_SPIPRIpg" spix:"_SPIPRIsp"\n", dpix, SPFS_OFFS2SPIX(fs, info->offset));
    res = _lu_page_delete(fs, dpix);
    ERR(res);
    fs->run.pused--;
  } else {
    // remove partial page
    res = _page_allocate_free(fs, &new_dpix_ixentry, info->fi->id, SPFS_LU_FL_DATA);
    ERR(res);
    // offset in data page for written data
    dbg("delete "_SPIPRIi" bytes from dpix:"_SPIPRIpg" spix:"_SPIPRIsp"\n",
        SPFS_DPAGE_SZ(fs) - page_offset, dpix, SPFS_OFFS2SPIX(fs, info->offset));
    // read existing phdr + data
    res = _medium_read(fs, SPFS_DPIX2ADDR(fs, dpix), work,
                       SPFS_CFG_LPAGE_SZ(fs), SPFS_T_DATA);
    ERR(res);
    // reset rest
    spfs_memset(work + page_offset, 0xff, SPFS_DPAGE_SZ(fs) - page_offset);
    // store it
    dbg("replace old data dpix:"_SPIPRIpg" with dpix:"_SPIPRIpg"\n", dpix, new_dpix_ixentry);
    res = _medium_write(fs, SPFS_DPIX2ADDR(fs, new_dpix_ixentry), work,
              SPFS_CFG_LPAGE_SZ(fs), SPFS_T_DATA | SPFS_C_UP);
    ERR(res);
    // delete old
    res = _lu_page_delete(fs, dpix);
    ERR(res);
    fs->run.pused--;
  }
  barr8_set(&info->ixarr, info->ixent, new_dpix_ixentry);
  ERRET(res);
}
_SPFS_STATIC int spfs_file_ftruncate(spfs_t *fs, spfs_fd_t *fd, uint32_t target_size) {
  int res;
  pix_t dpix_ixhdr = fd->dpix_ixhdr;
  uint32_t current_size = fd->fi.size;
  dbg("ftruncate id:"_SPIPRIid" to size "_SPIPRIi" from "_SPIPRIi"\n", fd->fi.id, target_size, current_size);
  if (current_size == SPFS_FILESZ_UNDEF || current_size <= target_size) {
    ERRET(SPFS_OK);
  }
  _file_trunc_varg_t arg = {.target_size = target_size,
                            .ixaction = SPFS_FTR_IXDIRTY_UNDEFINED,
                            .ixhdr_updated = 0 };
  res = spfs_file_visit(fs, &fd->fi, dpix_ixhdr, target_size,
                        current_size - target_size, 0, &arg,
                        _file_trunc_v, _file_trunc_vix, fd->fd_oflags);
  ERRET(res);
}
_SPFS_STATIC int spfs_file_truncate(spfs_t *fs, const char *path, uint32_t target_size) {
  int res;
  pix_t dpix_ixhdr;
  spfs_pixhdr_t pixhdr;
  res = spfs_file_find(fs, path, &dpix_ixhdr, &pixhdr);
  ERR(res);
  uint32_t current_size = pixhdr.fi.size;
  dbg("truncate path:%s to size "_SPIPRIi" from "_SPIPRIi"\n", path, target_size, current_size);
  if (current_size == SPFS_FILESZ_UNDEF || current_size <= target_size) {
    ERRET(SPFS_OK);
  }
  _file_trunc_varg_t arg = {.target_size = target_size,
                            .ixaction = SPFS_FTR_IXDIRTY_UNDEFINED,
                            .ixhdr_updated = 0 };
  res = spfs_file_visit(fs, &pixhdr.fi, dpix_ixhdr, target_size,
                        current_size - target_size, 0, &arg,
                        _file_trunc_v, _file_trunc_vix, 0);
  ERRET(res);
}
