/*
 * spfs_journal.c
 *
 *  Created on: Sep 9, 2017
 *      Author: petera
 */

//@REQUIRED_GCOV:80

#include "spfs_compile_cfg.h"
#include "spfs.h"
#include "spfs_lowlevel.h"
#include "spfs_journal.h"

#undef _SPFS_DBG_PRE
#undef _SPFS_DBG_POST
#define _SPFS_DBG_PRE ANSI_COLOR_MAGENTA"JO "
#define _SPFS_DBG_POST ANSI_COLOR_RESET
#include "spfs_dbg.h"
#undef dbg
#define dbg(_f, ...) dbg_jo(_f, ## __VA_ARGS__)

_SPFS_STATIC int _journal_create(spfs_t *fs) {
  int res;
  pix_t free_dpix = (pix_t)-1;
  res = _page_allocate_free(fs, &free_dpix, SPFS_IDJOUR, SPFS_LU_FL_DATA);
  ERR(res);
  spfs_phdr_t phdr = {.id = SPFS_IDJOUR, .span = 0, .p_flags = 0xff};
  res = spfs_page_hdr_write(fs, free_dpix, &phdr, SPFS_C_UP);
  ERR(res);
  fs->run.journal.dpix = free_dpix;
  dbg("created @ dpix:"_SPIPRIpg"\n", fs->run.journal.dpix);
  ERRET(res);
}

static uint32_t _journal_entry_bitsz(spfs_t *fs, uint8_t jentry_id) {
  uint32_t jentry_len = 0;
  switch (jentry_id) {
  case SPFS_JOUR_ID_FCREAT:
    jentry_len = SPFS_BITS_ID(fs);
    break;
  case SPFS_JOUR_ID_FMOD:
    jentry_len = SPFS_BITS_ID(fs);
    break;
  case SPFS_JOUR_ID_FTRUNC:
    jentry_len = SPFS_BITS_ID(fs) + 32;
    break;
  case SPFS_JOUR_ID_FRM:
    jentry_len = SPFS_BITS_ID(fs);
    break;
  case SPFS_JOUR_ID_FRENAME:
    jentry_len = 2*SPFS_BITS_ID(fs);
    break;
  }
  return 1 + SPFS_JOUR_BITS_ID + jentry_len + 1;
}

static int _journal_add(spfs_t *fs, spfs_jour_entry *jentry) {
  if (fs->run.journal.pending_op != SPFS_JOUR_ID_FREE) ERR(-SPFS_ERR_JOURNAL_PENDING);
  uint8_t mem[SPFS_JOUR_ENTRY_MAX_SZ];
  spfs_memset(mem, 0xff, SPFS_JOUR_ENTRY_MAX_SZ);

  // check if there is enough room in the journal page
  // save bits for a extra rm journal entry would we need to remove
  // a file if we're fully crammed
  uint32_t bits = _journal_entry_bitsz(fs, jentry->id);
  if (fs->run.journal.bitoffs + bits >=
      SPFS_DPAGE_SZ(fs) * 8 -
      (jentry->id == SPFS_JOUR_ID_FRM ? 0 : _journal_entry_bitsz(fs, SPFS_JOUR_ID_FRM))) {
    // need new page
    int res_page = _resv_free(fs, fs->run.journal.resv_free);
    ERR(res_page < 0 ? res_page : SPFS_OK);
    pix_t free_dpix = (pix_t)res_page;
    dbg("new journal page @ "_SPIPRIpg"\n", free_dpix);

    // TODO
    fs->run.journal.bitoffs = 0;
  }
  uint32_t jour_bit_ix = fs->run.journal.bitoffs;

  // write data to memory only comprising the change on medium
  bstr8 bs;
  bstr8_init(&bs, mem);
  bstr8_setp(&bs, jour_bit_ix % 8);
  bstr8_wr(&bs, 1, 1);
  bstr8_wr(&bs, SPFS_JOUR_BITS_ID, jentry->id);

  switch (jentry->id) {
  case SPFS_JOUR_ID_FCREAT:
    bstr8_wr(&bs, SPFS_BITS_ID(fs), jentry->fcreat.id);
    break;
  case SPFS_JOUR_ID_FMOD:
    bstr8_wr(&bs, SPFS_BITS_ID(fs), jentry->fmod.id);
    break;
  case SPFS_JOUR_ID_FTRUNC:
    bstr8_wr(&bs, SPFS_BITS_ID(fs), jentry->ftrunc.id);
    bstr8_wr(&bs, 32, jentry->ftrunc.sz);
    break;
  case SPFS_JOUR_ID_FRM:
    bstr8_wr(&bs, SPFS_BITS_ID(fs), jentry->frm.id);
    break;
  case SPFS_JOUR_ID_FRENAME:
    bstr8_wr(&bs, SPFS_BITS_ID(fs), jentry->frename.src_id);
    bstr8_wr(&bs, SPFS_BITS_ID(fs), jentry->frename.dst_id);
    break;
  default:
    ERRET(-SPFS_ERR_JOURNAL_BROKEN );
  }

  bstr8_wr(&bs, 1, 0);

  // get address and length of bytes in journal page to actually update
  uint32_t jentry_addr = SPFS_DPIX2ADDR(fs, fs->run.journal.dpix) + SPFS_PHDR_SZ(fs) + jour_bit_ix/8;
  uint32_t jentry_len = spfs_ceil(bstr8_getp(&bs), 8);

  dbg("dpix:"_SPIPRIpg" id:"_SPIPRIi" boffs:"_SPIPRIi"\n", fs->run.journal.dpix, jentry->id, jour_bit_ix);

  // and write
  int res = _medium_write(fs, jentry_addr, mem, jentry_len, SPFS_T_META | SPFS_C_UP |
                        (_SPFS_HAL_WR_FL_OVERWRITE | _SPFS_HAL_WR_FL_IGNORE_BITS));
  ERR(res);
  fs->run.journal.pending_op = jentry->id;
  ERRET(res);
}

_SPFS_STATIC int spfs_journal_add(spfs_t *fs, spfs_jour_entry *jentry) {
  int res = _journal_add(fs, jentry);
  ERRET(res);
}

_SPFS_STATIC int spfs_journal_complete(spfs_t *fs, uint8_t jentry_id) {
  if (fs->run.journal.pending_op != jentry_id) ERR(-SPFS_ERR_JOURNAL_PENDING);
  uint8_t mem[1] = {0xff};
  uint32_t jour_bit_ix = fs->run.journal.bitoffs;
  // write data to memory only comprising the change on medium
  bstr8 bs;
  bstr8_init(&bs, mem);
  bstr8_setp(&bs, jour_bit_ix % 8);
  bstr8_wr(&bs, 1, 0);
  // get address in journal page to actually update
  uint32_t jentry_addr = SPFS_DPIX2ADDR(fs, fs->run.journal.dpix) + SPFS_PHDR_SZ(fs) + jour_bit_ix/8;
  // and write
  int res = _medium_write(fs, jentry_addr, mem, 1, SPFS_T_META | SPFS_C_UP |
      (_SPFS_HAL_WR_FL_OVERWRITE | _SPFS_HAL_WR_FL_IGNORE_BITS));
  ERR(res);
  fs->run.journal.bitoffs += _journal_entry_bitsz(fs, jentry_id);
  dbg("dpix:"_SPIPRIpg" id:"_SPIPRIi" boffs:"_SPIPRIi"\n", fs->run.journal.dpix, jentry_id, fs->run.journal.bitoffs);
  fs->run.journal.pending_op = SPFS_JOUR_ID_FREE;

  ERRET(res);
}

_SPFS_STATIC int _journal_parse(spfs_t *fs, spfs_jour_entry *jentry, bstr8 *bs) {
  jentry->ongoing = bstr8_rd(bs, 1);
  jentry->id = bstr8_rd(bs, SPFS_JOUR_BITS_ID);
  switch (jentry->id) {
  case SPFS_JOUR_ID_FCREAT:
    jentry->fcreat.id = bstr8_rd(bs, SPFS_BITS_ID(fs));
//    dbg("CREAT :%s id:"_SPIPRIid"\n",
//        jentry->ongoing ? "RU" : "OK", jentry->fcreat.id);
    break;
  case SPFS_JOUR_ID_FMOD:
    jentry->fmod.id = bstr8_rd(bs, SPFS_BITS_ID(fs));
//    dbg("MOD   :%s id:"_SPIPRIid"\n",
//        jentry->ongoing ? "RU" : "OK", jentry->fmod.id);
    break;
  case SPFS_JOUR_ID_FTRUNC:
    jentry->ftrunc.id = bstr8_rd(bs, SPFS_BITS_ID(fs));
    jentry->ftrunc.sz = bstr8_rd(bs, 32);
//    dbg("TRUNC :%s id:"_SPIPRIid" l:"_SPIPRIi"\n",
//        jentry->ongoing ? "RU" : "OK",
//        jentry->ftrunc.id, jentry->ftrunc.len);
    break;
  case SPFS_JOUR_ID_FRM:
    jentry->frm.id = bstr8_rd(bs, SPFS_BITS_ID(fs));
//    dbg("RM    :%s id:"_SPIPRIid"\n",
//        jentry->ongoing ? "RU" : "OK", jentry->frm.id);
    break;
  case SPFS_JOUR_ID_FRENAME:
    jentry->frename.src_id = bstr8_rd(bs, SPFS_BITS_ID(fs));
    jentry->frename.dst_id = bstr8_rd(bs, SPFS_BITS_ID(fs));
//    dbg("RENAME:%s src_id:"_SPIPRIid" dst_id:"_SPIPRIid"\n",
//        jentry->ongoing ? "RU" : "OK",
//        jentry->frename.src_id, jentry->frename.dst_id);
    break;
  case SPFS_JOUR_ID_FREE:
    break;
  default:
    ERRET(-SPFS_ERR_JOURNAL_BROKEN);
  }
  jentry->unwritten = bstr8_rd(bs, 1);
  return SPFS_OK;
}

_SPFS_STATIC int spfs_journal_read(spfs_t *fs) {
  int res;
  dbg("dpix:"_SPIPRIpg" bitoffs:"_SPIPRIi"\n", fs->run.journal.dpix, fs->run.journal.bitoffs);
  res = _medium_read(fs, SPFS_DPIX2ADDR(fs, fs->run.journal.dpix) + SPFS_PHDR_SZ(fs),
      fs->run.work1, SPFS_DPAGE_SZ(fs), 0);
  ERR(res);
  bstr8 bs;
  bstr8_init(&bs, fs->run.work1);
  spfs_jour_entry e;
  while (res == SPFS_OK && fs->run.journal.bitoffs < SPFS_DPAGE_SZ(fs) * 8) {
    bstr8_setp(&bs, fs->run.journal.bitoffs);
    res = _journal_parse(fs, &e, &bs);
    ERR(res);
    if (e.id == SPFS_JOUR_ID_FREE) {
      if (e.ongoing && e.unwritten) {
        // clean end
        break;
      } else {
        // a free entry must have ongoing and unwritten bits set
        res = -SPFS_ERR_JOURNAL_BROKEN;
        break;
      }
    }
    if (e.unwritten) {
      res = -SPFS_ERR_JOURNAL_BROKEN;
      break;
    }
    if (e.ongoing) {
      res = -SPFS_ERR_JOURNAL_INTERRUPTED;
      break;
    }
    fs->run.journal.bitoffs += _journal_entry_bitsz(fs, e.id);
  }
  ERRET(res);
}


