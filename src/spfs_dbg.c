/*
 * spfs_dbg.c
 *
 *  Created on: Sep 10, 2017
 *      Author: petera
 */

#include "spfs_compile_cfg.h"
#include "spfs.h"
#include "spfs_lowlevel.h"
#include "spfs_dbg.h"
#include "spfs_journal.h"

#if SPFS_ERRSTR
#define ERRCASE(x) case -(x): return #x
#define CASE(x) case x: return #x

const char *spfs_strerror(int err) {
  switch (err) {
  CASE(SPFS_OK);
  ERRCASE(SPFS_ERR_UNCONFIGURED);
  ERRCASE(SPFS_ERR_MOUNT_STATE);
  ERRCASE(SPFS_ERR_LU_PHDR_ID_MISMATCH);
  ERRCASE(SPFS_ERR_LU_PHDR_FLAG_MISMATCH);
  ERRCASE(SPFS_ERR_OUT_OF_PAGES);
  ERRCASE(SPFS_ERR_PAGE_NOT_FOUND);
  ERRCASE(SPFS_ERR_OUT_OF_IDS);
  ERRCASE(SPFS_ERR_CFG_SZ_NOT_REPR);
  ERRCASE(SPFS_ERR_CFG_SZ_NOT_ALIGNED);
  ERRCASE(SPFS_ERR_CFG_LPAGE_SZ);
  ERRCASE(SPFS_ERR_CFG_LBLOCK_SZ);
  ERRCASE(SPFS_ERR_CFG_MEM_NOT_ALIGNED);
  ERRCASE(SPFS_ERR_CFG_MEM_SZ);
  ERRCASE(SPFS_ERR_CFG_MOUNT_MISMATCH);
  ERRCASE(SPFS_ERR_NOT_A_FS);
  ERRCASE(SPFS_ERR_FILE_NOT_FOUND);
  ERRCASE(SPFS_ERR_JOURNAL_BROKEN );
  ERRCASE(SPFS_ERR_JOURNAL_INTERRUPTED);
  ERRCASE(SPFS_ERR_JOURNAL_PENDING);
  ERRCASE(SPFS_ERR_OUT_OF_FILEDESCRIPTORS);
  ERRCASE(SPFS_ERR_BAD_FILEDESCRIPTOR);
  ERRCASE(SPFS_ERR_FILE_CLOSED);
  ERRCASE(SPFS_ERR_EOF);
  ERRCASE(SPFS_ERR_NAME_CONFLICT);
  ERRCASE(SPFS_ERR_FREE_PAGE_NOT_RESERVED);
  CASE(SPFS_VIS_CONT);
  CASE(SPFS_VIS_CONT_LU_RELOAD);
  CASE(SPFS_VIS_STOP);
  ERRCASE(SPFS_ERR_VIS_END);
  ERRCASE(SPFS_ERR_ASSERT);
  default: return "?";
  }

}
#endif


#if SPFS_DUMP
#define _COLS   (uint32_t)16
static void _dump_data(uint8_t *buf, uint32_t len, uint32_t addr, const char *prefix) {
  uint32_t i;
  while (len) {
    uint8_t col_offs = (addr&(_COLS-1));
    uint8_t entries = spfs_min(_COLS-col_offs, len);
    if (prefix) SPFS_DUMP_PRINTF("%s", prefix);
    SPFS_DUMP_PRINTF("%08x  " , addr & (~(_COLS-1)));
    for (i=0; i < col_offs;i++) SPFS_DUMP_PRINTF("__ ");
    for (i=0; i < entries; i++) SPFS_DUMP_PRINTF("%02x ", buf[i]);
    for (i=0; col_offs==0 && i < _COLS-entries; i++) SPFS_DUMP_PRINTF("__ ");
    SPFS_DUMP_PRINTF("    ");
    for (i=0; i < col_offs;i++) SPFS_DUMP_PRINTF("_");
    for (i=0; i < entries; i++) SPFS_DUMP_PRINTF("%c", buf[i] >= 32 && buf[i] < 127 ? buf[i] : '.');
    for (i=0; col_offs==0 && i < _COLS-entries; i++) SPFS_DUMP_PRINTF("_");
    buf += entries;
    addr += entries;
    len -= entries;
    SPFS_DUMP_PRINTF("\n");
  }
}
typedef struct {
  bix_t last_lbix;
  uint32_t flags;
  uint8_t hdr;
} _dump_varg_t;
static int _dump_page(spfs_t *fs, uint32_t lu_entry, spfs_vis_info_t *info, void *varg) {
  int res;

  _dump_varg_t *arg = (_dump_varg_t *)varg;
  id_t id = lu_entry >> SPFS_LU_FLAG_BITS;
  uint8_t lu_flags = (lu_entry & ((1<<SPFS_LU_FLAG_BITS) -1));
  spfs_pixhdr_t pixhdr;
  res = _page_ixhdr_read(fs, info->dpix, &pixhdr, 0);
  ERR(res);
  if ((arg->flags & SPFS_DUMP_ONLY_SPIX0) && pixhdr.phdr.span > 0) return res;
  SPFS_DUMP_PRINTF("  "_SPIPRIpg "  " _SPIPRIid "  " _SPIPRIfl"    ",
         info->dpix, id, lu_flags);
  SPFS_DUMP_PRINTF(_SPIPRIid"/"_SPIPRIid"  "_SPIPRIfl"   ", pixhdr.phdr.id, pixhdr.phdr.span, pixhdr.phdr.p_flags);
  if (id == SPFS_IDDELE) SPFS_DUMP_PRINTF("deleted ");
  if (spfs_signext(id, SPFS_BITS_ID(fs)) == SPFS_IDFREE) SPFS_DUMP_PRINTF("free ");
  if (lu_flags == 0 && pixhdr.phdr.span == 0 && id != SPFS_IDDELE) {
    char fname[SPFS_CFG_FILE_NAME_SZ + 1];
    spfs_strncpy(fname, (char *)pixhdr.name, SPFS_CFG_FILE_NAME_SZ);
    SPFS_DUMP_PRINTF("type:"_SPIPRIi"  sz:"_SPIPRIi"  xsz:"_SPIPRIi"  flags:"_SPIPRIfl"  name:\"%s\"",
                     pixhdr.fi.type, pixhdr.fi.size, pixhdr.fi.x_size, pixhdr.fi.f_flags, fname);
  } else if (spfs_signext(id, SPFS_BITS_ID(fs)) == SPFS_IDJOUR) {
    SPFS_DUMP_PRINTF("JOURNAL");
    if (arg->flags & SPFS_DUMP_JOURNAL) {
      uint8_t data[SPFS_CFG_LPAGE_SZ(fs)];
      res = _medium_read(fs, SPFS_DPIX2ADDR(fs, info->dpix), data, SPFS_CFG_LPAGE_SZ(fs), 0);
      ERR(res);
      bstr8 bs;
      bstr8_init(&bs, data);
      spfs_jour_entry e;
      while (bstr8_getp(&bs) < SPFS_CFG_LPAGE_SZ(fs) * 8) {
        res = _journal_parse(fs, &e, &bs);
        ERR(res);
        if (e.id == SPFS_JOUR_ID_FREE) break;
        SPFS_DUMP_PRINTF("\n    ");
        switch (e.id) {
        case SPFS_JOUR_ID_FCREAT:
          SPFS_DUMP_PRINTF("CREAT :%s id:"_SPIPRIid,
              e.ongoing ? "RU" : "OK", e.fcreat.id);
          break;
        case SPFS_JOUR_ID_FMOD:
          SPFS_DUMP_PRINTF("MOD   :%s id:"_SPIPRIid,
              e.ongoing ? "RU" : "OK", e.fmod.id);
          break;
        case SPFS_JOUR_ID_FTRUNC:
          SPFS_DUMP_PRINTF("TRUNC :%s id:"_SPIPRIid" l:"_SPIPRIi,
              e.ongoing ? "RU" : "OK",
              e.ftrunc.id, e.ftrunc.sz);
          break;
        case SPFS_JOUR_ID_FRM:
          SPFS_DUMP_PRINTF("RM    :%s id:"_SPIPRIid,
              e.ongoing ? "RU" : "OK", e.frm.id);
          break;
        case SPFS_JOUR_ID_FRENAME:
          SPFS_DUMP_PRINTF("RENAME:%s src_id:"_SPIPRIid" dst_id:"_SPIPRIid,
              e.ongoing ? "RU" : "OK",
              e.frename.src_id, e.frename.dst_id);
          break;
        case SPFS_JOUR_ID_FREE:
          SPFS_DUMP_PRINTF("FREE");
          break;
        default:
          SPFS_DUMP_PRINTF("???");
          break;
        }
      }
    }
  }

  SPFS_DUMP_PRINTF("\n");

  if (arg->flags & SPFS_DUMP_PAGE_DATA) {
    uint8_t data[SPFS_CFG_LPAGE_SZ(fs)];
    res = _medium_read(fs, SPFS_DPIX2ADDR(fs, info->dpix), data, SPFS_CFG_LPAGE_SZ(fs), 0);
    ERR(res);
    _dump_data(data, SPFS_CFG_LPAGE_SZ(fs), SPFS_DPIX2ADDR(fs, info->dpix), "    ");
  }
  return res;
}
static int _dump_v(spfs_t *fs, uint32_t lu_entry, spfs_vis_info_t *info, void *varg) {
  int res;
  _dump_varg_t *arg = (_dump_varg_t *)varg;
  if (info->lbix != arg->last_lbix) {
    spfs_bhdr_t b;
    res = _bhdr_rd(fs, info->lbix, &b);
    ERR(res);
    if ((arg->flags & SPFS_DUMP_NO_BLOCK_HDRS) == 0) {
      SPFS_DUMP_PRINTF("LBIX:"_SPIPRIbl" DBIX:"_SPIPRIbl" ERA:%04x MAGIC:%04x GC:%s CHK P/L:%04x/%04x\n",
          info->lbix, b.dbix,
          b.era_cnt, b.magic,
          b.gc_flag == SPFS_BLK_GC_ACTIVE ? "Y" :
          b.gc_flag == SPFS_BLK_GC_INACTIVE ? "N" : "?",
          b.pchk, b.lchk);
    }
    arg->last_lbix = info->lbix;
    arg->hdr = 0;
  }
  if ((arg->flags & SPFS_DUMP_NO_FREE) &&
      spfs_signext(lu_entry >> SPFS_LU_FLAG_BITS, SPFS_BITS_ID(fs)) == SPFS_IDFREE) {
    return SPFS_VIS_CONT;
  }
  if ((arg->flags & SPFS_DUMP_NO_DELE) &&
      spfs_signext(lu_entry >> SPFS_LU_FLAG_BITS, SPFS_BITS_ID(fs)) == SPFS_IDDELE) {
    return SPFS_VIS_CONT;
  }
  if ((arg->flags & SPFS_DUMP_ONLY_SPIX0) &&
      (lu_entry & ((1<<SPFS_LU_FLAG_BITS)-1))) {
    return SPFS_VIS_CONT;
  }
  if (arg->hdr == 0) {
    if ((arg->flags & SPFS_DUMP_NO_BLOCK_HDRS) == 0) {
      SPFS_DUMP_PRINTF("  DPIX  LU..  F.    ID../SP..  FL\n");
    }
    arg->hdr = 1;
  }

  res = _dump_page(fs, lu_entry, info, varg);
  ERR(res);

  return SPFS_VIS_CONT;
}
void spfs_dump(spfs_t *fs, uint32_t dump_flags) {
  if ((dump_flags & SPFS_DUMP_NO_CFG)==0) {
    SPFS_DUMP_PRINTF("ver "_SPIPRIi"."_SPIPRIi"."_SPIPRIi"\n",
        (SPFS_VERSION >> 12), (SPFS_VERSION >> 8) & 0xf, SPFS_VERSION & 0xff);
    SPFS_DUMP_PRINTF("CFG_PFLASH_SZ:"_SPIPRIi "\n", SPFS_CFG_PFLASH_SZ(fs));
    SPFS_DUMP_PRINTF("CFG_LBLK_SZ  :"_SPIPRIi "\n", SPFS_CFG_LBLK_SZ(fs));
    SPFS_DUMP_PRINTF("CFG_LPAGE_SZ :"_SPIPRIi "\n", SPFS_CFG_LPAGE_SZ(fs));
    SPFS_DUMP_PRINTF("TYPEDEF_PAGE :"_SPIPRIi "\n", (uint32_t)sizeof(SPFS_TYPEDEF_PAGE));
    SPFS_DUMP_PRINTF("TYPEDEF_SPAN :"_SPIPRIi "\n", (uint32_t)sizeof(SPFS_TYPEDEF_SPAN));
    SPFS_DUMP_PRINTF("TYPEDEF_ID   :"_SPIPRIi "\n", (uint32_t)sizeof(SPFS_TYPEDEF_ID));
    SPFS_DUMP_PRINTF("TYPEDEF_BLOCK:"_SPIPRIi "\n", (uint32_t)sizeof(SPFS_TYPEDEF_BLOCK));
    SPFS_DUMP_PRINTF("ALIGNMENT    :"_SPIPRIi "\n", (uint32_t)SPFS_ALIGN);
    SPFS_DUMP_PRINTF("CFG_DYNAMIC          :"_SPIPRIi "\n", SPFS_CFG_DYNAMIC);
    SPFS_DUMP_PRINTF("CFG_FILE_NAME_SZ     :"_SPIPRIi "\n", SPFS_CFG_FILE_NAME_SZ);
    SPFS_DUMP_PRINTF("CFG_FILE_META_SZ     :"_SPIPRIi "\n", SPFS_CFG_FILE_META_SZ);
    SPFS_DUMP_PRINTF("CFG_COPY_BUF_SZ      :"_SPIPRIi "\n", SPFS_CFG_COPY_BUF_SZ);
    SPFS_DUMP_PRINTF("CFG_SENSITIVE_DATA   :"_SPIPRIi "\n", SPFS_CFG_SENSITIVE_DATA);
    SPFS_DUMP_PRINTF("CFG_GC_WEIGHT_ERA_CNT:"_SPIPRIi "\n", SPFS_CFG_GC_WEIGHT_ERA_CNT(fs));
    SPFS_DUMP_PRINTF("CFG_GC_WEIGHT_DELE   :"_SPIPRIi "\n", SPFS_CFG_GC_WEIGHT_DELE(fs));
    SPFS_DUMP_PRINTF("CFG_GC_WEIGHT_FREE   :"_SPIPRIi "\n", SPFS_CFG_GC_WEIGHT_FREE(fs));
    SPFS_DUMP_PRINTF("CFG_GC_WEIGHT_USED   :"_SPIPRIi "\n", SPFS_CFG_GC_WEIGHT_USED(fs));
    SPFS_DUMP_PRINTF("ERRSTR               :"_SPIPRIi "\n", SPFS_ERRSTR);
}

  if ((dump_flags & SPFS_DUMP_NO_STATE)==0) {
    SPFS_DUMP_PRINTF("config       :"_SPIPRIi "\n", fs->config_state);
    SPFS_DUMP_PRINTF("mount        :"_SPIPRIi "\n", fs->mount_state);
    SPFS_DUMP_PRINTF("blk lu cnt   :"_SPIPRIi "\n", fs->run.blk_lu_cnt);
    {
      bix_t i;
      for (i = 0; i < fs->run.blk_lu_cnt; i++) {
        SPFS_DUMP_PRINTF("  dbix:"_SPIPRIbl" lbix:"_SPIPRIbl"\n", i, barr_get(&fs->run.blk_lu, i));
      }
    }
    SPFS_DUMP_PRINTF("cache cnt    :"_SPIPRIi "\n", fs->run.cache_cnt);
    SPFS_DUMP_PRINTF("dpix find cur:"_SPIPRIpg"\n", fs->run.dpix_find_cursor);
    SPFS_DUMP_PRINTF("dpix free cur:"_SPIPRIpg"\n", fs->run.dpix_free_page_cursor);
    SPFS_DUMP_PRINTF("filedesc cnt :"_SPIPRIi "\n", fs->run.fd_cnt);
    SPFS_DUMP_PRINTF("jour.bitoffs :"_SPIPRIi "\n", fs->run.journal.bitoffs);
    SPFS_DUMP_PRINTF("jour.dpix    :"_SPIPRIpg"\n", fs->run.journal.dpix);
    SPFS_DUMP_PRINTF("jour.pending :"_SPIPRIi "\n", fs->run.journal.pending_op);
    SPFS_DUMP_PRINTF("jour.resvfree:"_SPIPRIi "\n", fs->run.journal.resv_free);
    SPFS_DUMP_PRINTF("lbix gc free :"_SPIPRIbl"\n", fs->run.lbix_gc_free);
    SPFS_DUMP_PRINTF("max era cnt  :"_SPIPRIi "\n", fs->run.max_era_cnt);
    SPFS_DUMP_PRINTF("pdele        :"_SPIPRIi "\n", fs->run.pdele);
    SPFS_DUMP_PRINTF("pfree        :"_SPIPRIi "\n", fs->run.pfree);
    SPFS_DUMP_PRINTF("pused        :"_SPIPRIi "\n", fs->run.pused);
    SPFS_DUMP_PRINTF("resv.ptaken  :"_SPIPRIi "\n", fs->run.resv.ptaken);
    {
      SPFS_DUMP_PRINTF("resv.vec     :[ ");
      uint32_t i;
      for (i = 0; i < SPFS_PFREE_RESV; i++) SPFS_DUMP_PRINTF(_SPIPRIpg" ", fs->run.resv.arr[i]);
      SPFS_DUMP_PRINTF("]\n");
    }
  }

  if ((dump_flags & SPFS_DUMP_NO_FS) == 0) {
    _dump_varg_t arg = {.last_lbix = -1, .flags = dump_flags, .hdr = 0};
    spfs_page_visit(fs, 0,0,&arg,_dump_v,0);
  }
}
#endif

