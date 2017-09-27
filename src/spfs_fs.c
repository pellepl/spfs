/*
 * spfs_fs.c
 *
 *  Created on: Sep 9, 2017
 *      Author: petera
 */

//@REQUIRED_GCOV:80

#include "spfs_compile_cfg.h"
#include "spfs.h"
#include "spfs_lowlevel.h"
#include "spfs_fs.h"
#include "spfs_file.h"
#include "spfs_cache.h"
#include "spfs_journal.h"

#undef _SPFS_DBG_PRE
#undef _SPFS_DBG_POST
#define _SPFS_DBG_PRE ANSI_COLOR_WHITE "FS "
#define _SPFS_DBG_POST ANSI_COLOR_RESET
#include "spfs_dbg.h"
#undef dbg
#define dbg(_f, ...) dbg_fs(_f, ## __VA_ARGS__)


#if SPFS_CFG_DYNAMIC
static uint32_t spfs_log2(uint32_t v) {
  uint32_t r = 0;
  while (v) {
    r++;
    v>>=1;
  }
  return r;
}
#else
#define spfs_log2(v) ( \
  (v)&0x80000000U?32:((v)&0x40000000U?31:((v)&0x20000000U?30:((v)&0x10000000U?29:( \
  (v)&0x08000000U?28:((v)&0x04000000U?27:((v)&0x02000000U?26:((v)&0x01000000U?25:( \
  (v)&0x00800000U?24:((v)&0x00400000U?23:((v)&0x00200000U?22:((v)&0x00100000U?21:( \
  (v)&0x00080000U?20:((v)&0x00040000U?19:((v)&0x00020000U?18:((v)&0x00010000U?17:( \
  (v)&0x00008000U?16:((v)&0x00004000U?15:((v)&0x00002000U?14:((v)&0x00001000U?13:( \
  (v)&0x00000800U?12:((v)&0x00000400U?11:((v)&0x00000200U?10:((v)&0x00000100U? 9:( \
  (v)&0x00000080U? 8:((v)&0x00000040U? 7:((v)&0x00000020U? 6:((v)&0x00000010U? 5:( \
  (v)&0x00000008U? 4:((v)&0x00000004U? 3:((v)&0x00000002U? 2:((v)&0x00000001U? 1:0 \
      ))))))))))))))))))))))))))))))))
#endif



///////////////////////////////////////////////////////////////////////////////
// initialization / formatting / mounting
///////////////////////////////////////////////////////////////////////////////


_SPFS_STATIC int spfs_config(spfs_t *fs, spfs_cfg_t *cfg, void *user) {
  spfs_memset(fs, 0, sizeof(spfs_t));
  spfs_memcpy(&fs->cfg, cfg, sizeof(spfs_cfg_t));
  if (spfs_unpacknum(spfs_packnum(SPFS_CFG_LPAGE_SZ(fs))) != SPFS_CFG_LPAGE_SZ(fs))
    ERR(-SPFS_ERR_CFG_SZ_NOT_REPR); // logical page size cannot be represented in internal format
  if (spfs_unpacknum(spfs_packnum(SPFS_CFG_LBLK_SZ(fs))) != SPFS_CFG_LBLK_SZ(fs))
    ERR(-SPFS_ERR_CFG_SZ_NOT_REPR); // logical block size cannot be represented in internal format
  if (SPFS_CFG_PFLASH_SZ(fs) % SPFS_CFG_LBLK_SZ(fs))
    ERR(-SPFS_ERR_CFG_SZ_NOT_ALIGNED); // file system size not aligned with logical block size
  if (SPFS_CFG_LBLK_SZ(fs) % SPFS_CFG_LPAGE_SZ(fs))
    ERR(-SPFS_ERR_CFG_SZ_NOT_ALIGNED); // logical block size not aligned with logical page size
  if (SPFS_CFG_LBLK_SZ(fs) / SPFS_CFG_LPAGE_SZ(fs) < _SPFS_PAGE_CNT_MIN)
    ERR(-SPFS_ERR_CFG_LPAGE_SZ); // too few logical pages per logical block
  if (SPFS_CFG_PFLASH_SZ(fs) / SPFS_CFG_LBLK_SZ(fs) < _SPFS_BLOCK_CNT_MIN)
    ERR(-SPFS_ERR_CFG_LBLOCK_SZ); // too few logical blocks in file system

#if SPFS_CFG_DYNAMIC
  // precalculate commonly used stuff
  uint32_t lblk_cnt = SPFS_CFG_PFLASH_SZ(fs) / SPFS_CFG_LBLK_SZ(fs);
  uint32_t lpages_p_blk = SPFS_CFG_LBLK_SZ(fs) / SPFS_CFG_LPAGE_SZ(fs);
  uint32_t id_bits = spfs_log2((SPFS_CFG_PFLASH_SZ(fs) - SPFS_CFG_LBLK_SZ(fs))
                                / SPFS_CFG_LPAGE_SZ(fs) + SPFS_LU_EXTRA_IDS - lblk_cnt);
  uint32_t lu_bits = id_bits + SPFS_LU_FLAG_BITS;
  uint32_t blk_bits = spfs_log2(lblk_cnt);
  uint32_t lu_ent_cnt0 = ( 8 * (SPFS_CFG_LPAGE_SZ(fs) - SPFS_BLK_HDR_SZ) / lu_bits );
  uint32_t lu_ent_cnt = ( 8 * SPFS_CFG_LPAGE_SZ(fs) / lu_bits );
  uint32_t lupages_p_blk = spfs_ceil( lpages_p_blk - lu_ent_cnt0 + lu_ent_cnt, lu_ent_cnt + 1 );;
  fs->dyn.lblk_cnt = lblk_cnt;
  fs->dyn.lpages_p_blk = lpages_p_blk;
  fs->dyn.id_bits = id_bits;
  fs->dyn.blk_bits = blk_bits;
  fs->dyn.lu_ent_cnt0 = lu_ent_cnt0;
  fs->dyn.lu_ent_cnt = lu_ent_cnt;
  fs->dyn.lupages_p_blk = lupages_p_blk;
  fs->dyn.dpages_p_blk = lpages_p_blk - lupages_p_blk;
#endif

  if ((uint32_t)SPFS_CFG_LPAGE_SZ(fs) < (uint32_t)(SPFS_PHDR_SZ(fs) + SPFS_PIXHDR_SZ(fs) + 8))
    ERR(-SPFS_ERR_CFG_LPAGE_SZ); // too small logical page size given metadata and file name

  dbg("CFG_PFLASH_SZ:"_SPIPRIi "\n", SPFS_CFG_PFLASH_SZ(fs));
  dbg("CFG_LBLK_SZ  :"_SPIPRIi "\n", SPFS_CFG_LBLK_SZ(fs));
  dbg("CFG_LPAGE_SZ :"_SPIPRIi "\n", SPFS_CFG_LPAGE_SZ(fs));
  dbg("lblk_cnt     :"_SPIPRIi "\n", SPFS_LBLK_CNT(fs));
  dbg("lpages_p_blk :"_SPIPRIi "\n", SPFS_LPAGES_P_BLK(fs));
  dbg("max_id       :"_SPIPRIi "\n", SPFS_MAX_ID(fs));
  dbg("id_bits      :"_SPIPRIi "(+"_SPIPRIi ")\n", SPFS_BITS_ID(fs), SPFS_LU_FLAG_BITS);
  dbg("blk_bits     :"_SPIPRIi "\n", SPFS_BITS_BLK(fs));
  dbg("lu_ent_cnt0  :"_SPIPRIi "\n", SPFS_LU_ENT_CNT(fs, 0));
  dbg("lu_ent_cnt   :"_SPIPRIi "\n", SPFS_LU_ENT_CNT(fs, 1));
  dbg("lupages_p_blk:"_SPIPRIi "\n", SPFS_LUPAGES_P_BLK(fs));
  dbg("dpages_p_blk :"_SPIPRIi "\n", SPFS_DPAGES_P_BLK(fs));

  fs->user = user;
  fs->mount_state = 0;
  fs->config_state = SPFS_CONFIGURED;

  return SPFS_OK;
}
_SPFS_STATIC int spfs_format(spfs_t *fs) {
  bix_t lbix;
  bix_t lbix_end = SPFS_LBLK_CNT(fs);
  if (fs->config_state != SPFS_CONFIGURED) ERRET(-SPFS_ERR_UNCONFIGURED);
  if (fs->mount_state == SPFS_MOUNTED) ERRET(-SPFS_ERR_MOUNT_STATE);
  dbg("log blocks:"_SPIPRIi"\n", SPFS_LBLK_CNT(fs));
  int res = SPFS_OK;
  for (lbix = 0; res == SPFS_OK && lbix < lbix_end; lbix++) {
    res = _block_erase(fs, lbix, lbix == lbix_end - 1 ? SPFS_DBLKIX_FREE : lbix, 0);
  }
  ERRET(res);
}

static int _mount_scan_blocks(spfs_t *fs) {
  int res = SPFS_OK;
  uint16_t max_era_cnt = 0;
  bix_t lbix;
  bix_t lbix_end = SPFS_LBLK_CNT(fs);
  bix_t interrupted_erase_lbix = (bix_t)-1;
  bix_t interrupted_gc_lbix = (bix_t)-1;
  spfs_bhdr_t b;
  uint8_t raw[SPFS_BLK_HDR_SZ];

  fs->run.lbix_gc_free = (bix_t)-1;

  for (lbix = 0; res == SPFS_OK && lbix < lbix_end; lbix++) {
    // read and extract block header
    res = _bhdr_rd(fs, lbix, &b, raw);
    ERR(res);
//    dbg("lbix:"_SPIPRIbl" magic:"_SPIPRIad" dbix:"_SPIPRIbl" era:"_SPIPRIi
//             " lblk_sz:"_SPIPRIi" lpage_sz:"_SPIPRIi" gc:"_SPIPRIfl" chk:"_SPIPRIad"\n",
//        lbix, b.magic, b.dbix, b.era_cnt, b.lblk_sz, b.lpage_sz, b.gc_flag, b.pchk);
    // check block header magic
    if ((lbix == 0 && b.magic != SPFS_BLK_MAGIC_STA) ||
        (lbix == lbix_end-1 && b.magic != SPFS_BLK_MAGIC_END) ||
        (lbix > 0 && lbix < lbix_end-1 && b.magic != SPFS_BLK_MAGIC_MID)) {
      if (b.magic == SPFS_BLK_MAGIC_NONE && interrupted_erase_lbix == (bix_t)-1) {
        // found first interrupted erased block
        dbg("warn: erase interrupt lbix "_SPIPRIbl"\n", lbix);
        interrupted_erase_lbix = lbix;
        continue; // no more checks, we cannot rely on rest of header
      }
      else {
        // found unknown magic or multiple interrupted erased blocks
        dbg("err: unknown magic or multiple interrupted erased blocks lbix:"_SPIPRIbl"\n", lbix);
        ERR(-SPFS_ERR_NOT_A_FS);
      }
    }
    // check logical sizes
    if (b.lblk_sz != SPFS_CFG_LBLK_SZ(fs)) ERR(-SPFS_ERR_CFG_MOUNT_MISMATCH);
    if (b.lpage_sz != SPFS_CFG_LPAGE_SZ(fs)) ERR(-SPFS_ERR_CFG_MOUNT_MISMATCH);
    // check checksums, populate block lu
    if (b.dbix == SPFS_DBLKIX_FREE) {
      dbg("gc lbix:"_SPIPRIbl"\n", lbix);
      if (b.pchk != 0xffff) ERR(-SPFS_ERR_NOT_A_FS);
      fs->run.lbix_gc_free = lbix;
      if (b.gc_flag == SPFS_BLK_GC_INACTIVE) {
        // pass
      } else {
        // found unknown gc flag or free block with gc active
        dbg("err: unknown gc flag or free block with gc active lbix:"_SPIPRIbl"\n", lbix);
        ERR(-SPFS_ERR_NOT_A_FS);
      }
    } else {
      if (b.pchk != b.lchk) ERR(-SPFS_ERR_NOT_A_FS);
      barr_set(&fs->run.blk_lu, b.dbix, lbix);
      if (b.gc_flag == SPFS_BLK_GC_INACTIVE) {
        // pass
      } else if (b.gc_flag == SPFS_BLK_GC_ACTIVE && interrupted_gc_lbix == (bix_t)-1) {
        dbg("warn: gc interrupt lbix "_SPIPRIbl"\n", lbix);
        interrupted_gc_lbix = lbix;
      } else {
        // found unknown gc flag or multiple interrupted gced blocks
        dbg("err: unknown gc flag or multiple interrupted gced blocks lbix:"_SPIPRIbl"\n", lbix);
        ERR(-SPFS_ERR_NOT_A_FS);
      }
    }

    if (lbix == 0) {
      max_era_cnt = b.era_cnt;
    } else {
      max_era_cnt = _era_cnt_max(max_era_cnt, b.era_cnt);
    }
  } // per block
  fs->run.max_era_cnt = max_era_cnt;
  // TODO a lot of checks here
  // unique and consecutive numbers in block lu
  // handle interrupted erase
  // handle interrupted gc
  // check journal
  if (fs->run.lbix_gc_free == (bix_t)-1) {
    // no free gc block found
    dbg("err: no free gc block found\n");
    ERR(-SPFS_ERR_NOT_A_FS);
  }
  ERRET(res);
}

static int _mount_scan_fs_v(spfs_t *fs, uint32_t lu_entry, spfs_vis_info_t *info, void *varg) {
  id_t id = spfs_signext(lu_entry >> SPFS_LU_FLAG_BITS, SPFS_BITS_ID(fs));
  if (id == SPFS_IDDELE) {
    fs->run.pdele++;
  } else if (id == SPFS_IDFREE) {
    fs->run.pfree++;
  } else if (id == SPFS_IDJOUR) {
    fs->run.pused++;
    fs->run.journal.dpix = info->dpix;
  } else {
    fs->run.pused++;
  }
  return SPFS_VIS_CONT;
}
static int _mount_scan_fs(spfs_t *fs) {
  fs->run.pfree = 0;
  fs->run.pdele = 0;
  fs->run.pused = 0;
  fs->run.journal.dpix = -1;
  int res;
  res = spfs_page_visit(fs, 0, 0, NULL, _mount_scan_fs_v, 0);
  if (res == -SPFS_ERR_VIS_END) {
    res = SPFS_OK;
  }
  ERR(res);
  dbg("free pages:"_SPIPRIi"\n",fs->run.pfree);
  dbg("dele pages:"_SPIPRIi"\n",fs->run.pdele);
  dbg("used pages:"_SPIPRIi"\n",fs->run.pused);
  dbg("used:"_SPIPRIi"KB free:"_SPIPRIi"KB\n",
      (fs->run.pused * SPFS_CFG_LPAGE_SZ(fs)) / 1024,
      ((fs->run.pdele + fs->run.pfree) * SPFS_CFG_LPAGE_SZ(fs)) / 1024);
  ERRET(res);
}

static int _mount_alloc(spfs_t *fs, uint32_t descriptors, uint32_t cache_pages) {
  uint32_t acq_sz;
  uint32_t req_sz;
  void *mem;

  // request working memory
  req_sz = SPFS_CFG_LPAGE_SZ(fs) * 2;
  dbg("mem:"_SPIPRIi" sz:"_SPIPRIi"\n", SPFS_MEM_WORK_BUF, req_sz);
  mem = fs->cfg.malloc(fs, SPFS_MEM_WORK_BUF, req_sz, &acq_sz);
  if ((intptr_t)mem & (SPFS_ALIGN - 1))
    ERR(-SPFS_ERR_CFG_MEM_NOT_ALIGNED); // alignment
  if (mem == NULL || acq_sz != req_sz)
    ERR(-SPFS_ERR_CFG_MEM_SZ); // no mem or not enough
  fs->run.work1 = (uint8_t *)mem;
  fs->run.work2 = &fs->run.work1[SPFS_CFG_LPAGE_SZ(fs)];

  // request file descriptor buffer
  req_sz = sizeof(spfs_fd_t) * descriptors;
  dbg("mem:"_SPIPRIi" sz:"_SPIPRIi"\n", SPFS_MEM_FILEDESCS, req_sz);
  mem = fs->cfg.malloc(fs, SPFS_MEM_FILEDESCS, req_sz, &acq_sz);
  if (mem == NULL || acq_sz < sizeof(spfs_fd_t))
    ERR(-SPFS_ERR_CFG_MEM_SZ); // must have at least one descriptor
  _fd_init(fs, mem, acq_sz);

  // request data block index lu buffer
  req_sz = spfs_align((SPFS_LBLK_CNT(fs) - 1) * SPFS_BITS_BLK(fs),
                       SPFS_ALIGN * 4) / 8;
  dbg("mem:"_SPIPRIi" sz:"_SPIPRIi"\n", SPFS_MEM_BLOCK_LU, req_sz);
  mem = fs->cfg.malloc(fs, SPFS_MEM_BLOCK_LU, req_sz, &acq_sz);
  if ((intptr_t)mem & (SPFS_ALIGN - 1))
    ERR(-SPFS_ERR_CFG_MEM_NOT_ALIGNED); // alignment
  // TODO FEATURE
  // Possible to allow to none or small buffer, with the penalty of needing
  // to look up the logical block index. Use what memory is given us as a cache
  if (mem == NULL || acq_sz != req_sz)
    ERR(-SPFS_ERR_CFG_MEM_SZ); // no mem or not enough
  if (mem == NULL) {
    fs->run.blk_lu_cnt = 0;
  } else {
    fs->run.blk_lu_cnt = acq_sz * 8 / SPFS_BITS_BLK(fs);
    fs->run.blk_lu_cnt = spfs_min(fs->run.blk_lu_cnt, SPFS_LBLK_CNT(fs) - 1);
  }
  if (fs->run.blk_lu_cnt) {
    barr_init(&fs->run.blk_lu, mem, SPFS_BITS_BLK(fs));
  }

  // request cache buffer
  if (cache_pages) {
    req_sz = spfs_align(sizeof(spfs_cache), SPFS_ALIGN) +
        (spfs_align(sizeof(spfs_cache_page), SPFS_ALIGN) + SPFS_CFG_LPAGE_SZ(fs)) * cache_pages;
    dbg("mem:"_SPIPRIi" sz:"_SPIPRIi"\n", SPFS_MEM_CACHE, req_sz);
    mem = fs->cfg.malloc(fs, SPFS_MEM_CACHE, req_sz, &acq_sz);
    if (acq_sz < spfs_align(sizeof(spfs_cache), SPFS_ALIGN) +
        (spfs_align(sizeof(spfs_cache_page), SPFS_ALIGN) + SPFS_CFG_LPAGE_SZ(fs))) {
      dbg("requested "_SPIPRIi" cache pages, but didn't get memory for any", cache_pages);
    } else {
      spfs_cache_init(fs, mem, acq_sz);
    }
  }

  return SPFS_OK;
}


_SPFS_STATIC int spfs_mount(spfs_t *fs, uint32_t mount_flags, uint32_t descriptors, uint32_t cache_pages) {
  dbg("ver "_SPIPRIi"."_SPIPRIi"."_SPIPRIi"\n",
      (SPFS_VERSION >> 12), (SPFS_VERSION >> 8) & 0xf, SPFS_VERSION & 0xff);
  if (fs->config_state != SPFS_CONFIGURED) ERRET(-SPFS_ERR_UNCONFIGURED);
  int res;
  spfs_memset(fs->run.resv.arr, 0xff, sizeof(fs->run.resv.arr));
  fs->run.journal.pending_op = SPFS_JOUR_ID_FREE;
  res = _mount_alloc(fs, descriptors, cache_pages);
  ERR(res);
  res = _mount_scan_blocks(fs);
  ERR(res);
  res = _mount_scan_fs(fs);
  ERR(res);

  fs->run.dpix_find_cursor = 0;
  fs->run.dpix_free_page_cursor = 0;

  // check journal
  if (fs->run.journal.dpix == (pix_t)-1) {
    dbg("no journal found\n");
    // create one
    res = _journal_create(fs);
    ERR(res);
    dbg("journal created @ dpix:"_SPIPRIpg"\n", fs->run.journal.dpix);
  } else {
   dbg("journal found @ dpix:"_SPIPRIpg"\n", fs->run.journal.dpix);
  }

  // reserve free page for next journal page
  res = _resv_alloc(fs);
  ERR(res < 0 ? res : SPFS_OK);
  fs->run.journal.resv_free = (uint8_t)res;

  fs->mount_state = SPFS_MOUNTED;
  dbg("mounted successfully\n");
  ERRET(res);
}


_SPFS_STATIC int spfs_umount(spfs_t *fs) {
  int res = SPFS_OK;
  // TODO
  fs->mount_state = 0;
  ERRET(res);
}


// TODO consider adding extra field in block header telling number of blocks

#if SPFS_CFG_DYNAMIC

typedef struct {
  spfs_t *fs;
  spfs_cfg_t guess_cfg;
  uint32_t start_addr;
  uint32_t end_addr;
} _prober_t;

// checks if a block header has valid magic and checksum, and that
// the logical page and block sizes are coherent
// returns > 0 if ok, 0 if not
static int _bhdr_valid(spfs_bhdr_t *bhdr) {
  if (bhdr->magic == SPFS_BLK_MAGIC_STA ||
      bhdr->magic == SPFS_BLK_MAGIC_MID ||
      bhdr->magic == SPFS_BLK_MAGIC_END) {
    if (bhdr->gc_flag == SPFS_BLK_GC_ACTIVE ||
        bhdr->gc_flag == SPFS_BLK_GC_INACTIVE) {
      if ((bhdr->dbix == 0xffff && bhdr->pchk == 0xffff) ||
          (bhdr->dbix != 0xffff && bhdr->lchk == bhdr->pchk)) {
        if ((bhdr->lpage_sz*_SPFS_PAGE_CNT_MIN <= bhdr->lblk_sz) &&
            (bhdr->lblk_sz % bhdr->lpage_sz) == 0) {
          return 1;
        }
      }
    }
  }
  return 0;
}

// reads and checks blockheader at given address
// returns > 0 if ok, 0 if not, < 0 on error
static int _is_header(_prober_t *p, uint32_t addr, spfs_bhdr_t *bhdr) {
  uint8_t hdr[SPFS_BLK_HDR_SZ];
  int res = _medium_read(p->fs, addr, hdr, SPFS_BLK_HDR_SZ, 0);
  ERR(res);
  spfs_bhdr_parse(bhdr, hdr, 1);
  if (_bhdr_valid(bhdr)) {
    p->guess_cfg.lpage_sz = bhdr->lpage_sz;
    p->guess_cfg.lblk_sz = bhdr->lblk_sz;
    return 1;
  }
  return 0;
}

static int _try_header_span(_prober_t *p, uint32_t addr, spfs_bhdr_t *bhdr) {
  int res;
  uint8_t have_start_block = 0;
  uint8_t have_end_block = 0;
  uint32_t fs_start_addr = 0;
  uint32_t fs_end_addr = 0;
  // total number of encountered interrupted erases
  uint8_t interrupted_erases = 0;
  // continuous number of encountered interrupted erases
  uint8_t cont_interrupted_erases = 0;

  // here, we know that bhdr is populated with what is a valid block header
  if (bhdr->magic == SPFS_BLK_MAGIC_STA) {
    have_start_block = 1;
    fs_start_addr = addr;
    dbg("    start block @ "_SPIPRIad", verified\n", fs_start_addr);
  } else if (bhdr->magic == SPFS_BLK_MAGIC_END) {
    have_end_block = 1;
    fs_end_addr = addr;
    dbg("    end block @ "_SPIPRIad", verified\n", fs_end_addr);
  }

  uint8_t hdr[SPFS_BLK_HDR_SZ];

  // search backward for start block
  uint32_t cur_addr = addr;
  while (!have_start_block && cur_addr >= p->start_addr + p->guess_cfg.lblk_sz) {
    cur_addr -= p->guess_cfg.lblk_sz;
    res = _medium_read(p->fs, cur_addr, hdr, SPFS_BLK_HDR_SZ, 0);
    ERR(res);
    spfs_bhdr_parse(bhdr, hdr, 1);
    if (_bhdr_valid(bhdr) &&
        bhdr->lblk_sz == p->guess_cfg.lblk_sz &&
        bhdr->lpage_sz == p->guess_cfg.lpage_sz) {
      if (bhdr->magic == SPFS_BLK_MAGIC_STA) {
        have_start_block = 1;
        fs_start_addr = cur_addr;
        dbg("    start block @ "_SPIPRIad", verified\n", fs_start_addr);
      } else if (bhdr->magic == SPFS_BLK_MAGIC_END) {
        return 0;
      }
      cont_interrupted_erases = 0;
    } else if (bhdr->magic == 0xffff || cont_interrupted_erases == 1) {
      dbg("    start: erased block @ "_SPIPRIad"\n", cur_addr);
      cont_interrupted_erases++;
      interrupted_erases++;
      if (cont_interrupted_erases == 2) {
        // rewind
        cur_addr += p->guess_cfg.lblk_sz;
        break;
      }
      if (interrupted_erases > 1) return 0;
    } else {
      return 0;
    }
  }
  if (!have_start_block && bhdr->magic == 0xffff && cont_interrupted_erases > 0) {
    // start block erased?
    have_start_block = 1;
    fs_start_addr = cur_addr;
    dbg("    start block @ "_SPIPRIad", assuming erased\n", fs_start_addr);
  }

  // search forward for end block
  cont_interrupted_erases = 0;
  cur_addr = addr;
  while (have_start_block && !have_end_block && cur_addr < p->end_addr - p->guess_cfg.lblk_sz) {
    cur_addr += p->guess_cfg.lblk_sz;
    res = _medium_read(p->fs, cur_addr, hdr, SPFS_BLK_HDR_SZ, 0);
    ERR(res);
    spfs_bhdr_parse(bhdr, hdr, 1);
    if (_bhdr_valid(bhdr) &&
        bhdr->lblk_sz == p->guess_cfg.lblk_sz &&
        bhdr->lpage_sz == p->guess_cfg.lpage_sz) {
      if (bhdr->magic == SPFS_BLK_MAGIC_END) {
        have_end_block = 1;
        fs_end_addr = cur_addr + p->guess_cfg.lblk_sz;
        dbg("    end block @ "_SPIPRIad", verified\n", fs_end_addr);
      } else if (bhdr->magic == SPFS_BLK_MAGIC_STA) {
        return 0;
      }
      cont_interrupted_erases = 0;
    } else if (bhdr->magic == 0xffff) {
      dbg("    end: erased block @ "_SPIPRIad"\n", cur_addr);
      cont_interrupted_erases++;
      interrupted_erases++;
      if (cont_interrupted_erases == 2) {
        // rewind
        cur_addr -= p->guess_cfg.lblk_sz;
        break;
      }
      if (interrupted_erases > 1) return 0;
    } else {
      return 0;
    }
  }
  if (!have_end_block && bhdr->magic == 0xffff && cont_interrupted_erases > 0) {
    // end block erased?
    have_end_block = 1;
    fs_end_addr = cur_addr + p->guess_cfg.lblk_sz;
    dbg("    end block @ "_SPIPRIad", assuming erased\n", fs_end_addr);
  }

  uint8_t fs_identified = have_start_block && have_end_block;
  if (fs_identified) {
    p->guess_cfg.pflash_addr_offs = fs_start_addr;
    p->guess_cfg.pflash_sz = fs_end_addr - fs_start_addr;
  }

  return res < 0 ? res : (fs_identified ? 1 : 0);
}

_SPFS_STATIC int spfs_probe(spfs_cfg_t *cfg, uint32_t start_addr, uint32_t end_addr, void *user) {
  int res;
  uint8_t mem[SPFS_CFG_COPY_BUF_SZ];
  spfs_t dummy_fs;
  spfs_memcpy(&dummy_fs.cfg, cfg, sizeof(spfs_cfg_t));
  dummy_fs.user = user;

  dbg("start:"_SPIPRIad" end:"_SPIPRIad"\n", start_addr, end_addr);

  _prober_t prober = {.fs = &dummy_fs, .start_addr = start_addr, .end_addr = end_addr};

  uint32_t addr = start_addr + 1;
  const uint8_t magic_id = (SPFS_BLK_MAGIC_STA) >> 8;
  while (addr < end_addr - SPFS_BLK_HDR_SZ - 64) {
    uint32_t sz = spfs_min(end_addr - addr, SPFS_CFG_COPY_BUF_SZ);
    res = _medium_read(&dummy_fs, addr, mem, sz, 0);
    ERR(res);
    uint32_t i;
    spfs_bhdr_t bhdr;
    for (i = 0; i < sz; i++) {
      if (mem[i] == magic_id) {
        res = _is_header(&prober, addr + i - 1, &bhdr);
        ERR(res < 0 ? res : 0);

        if (res == 1) {
          dbg("  found id @ "_SPIPRIad", hdr:%04x\n", addr+i, bhdr.magic);
          res = _try_header_span(&prober, addr + i - 1, &bhdr);
          ERR(res < 0 ? res : 0);
          if (res == 1) {
            cfg->lblk_sz = prober.guess_cfg.lblk_sz;
            cfg->lpage_sz = prober.guess_cfg.lpage_sz;
            cfg->pflash_addr_offs = prober.guess_cfg.pflash_addr_offs;
            cfg->pflash_sz = prober.guess_cfg.pflash_sz;
            dbg("  found fs @ "_SPIPRIad", size:"_SPIPRIi", lblk_sz:"_SPIPRIi", lpage_sz:"_SPIPRIi"\n",
                cfg->pflash_addr_offs, cfg->pflash_sz, cfg->lblk_sz, cfg->lpage_sz);
            res = SPFS_OK;
            goto end;
          } else {
            dbg("  span failed\n");
          }
        }
      }
    }
    addr += sz;
  }
  res = -SPFS_ERR_NOT_A_FS;
end:
  ERRET(res);
}

#endif // SPFS_CFG_DYNAMIC
