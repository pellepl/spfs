/*
 * spfs_gc.c
 *
 *  Created on: Sep 10, 2017
 *      Author: petera
 */

//@REQUIRED_GCOV:85

/* GC journalling info is in the block headers instead
 * so we don't need to clutter the journal page. The main
 * reason is that GC can be called many times during a
 * journalled operation such as e.g. write.
 *
 * when GCing e.g. lblk 4, evacuating to lblk 0:
 *   1. write GC flag in lblk 4 blk hdr, as GC active
 *        recover by erase lblk0 and start GC again
 *   2. move all pages from lblk 4 to lblk 0
 *        recover by erase lblk0 and start GC again
 *   3. write blk hdr to lblk 0
 *        recover, goto 4: lblk 0 hdr is valid and lblk 4 hdr is GC active
 *   4. erase lblk 4
 *        recover, goto 4: lblk 0 hdr is valid and lblk 4 hdr is GC active
 *   5. write header lblk 4, now GC block
 *
 */

#include "spfs_compile_cfg.h"
#include "spfs.h"
#include "spfs_lowlevel.h"
#include "spfs_gc.h"

#undef _SPFS_DBG_PRE
#undef _SPFS_DBG_POST
#define _SPFS_DBG_PRE ANSI_COLOR_BLUE"GC "
#define _SPFS_DBG_POST ANSI_COLOR_RESET
#include "spfs_dbg.h"
#undef dbg
#define dbg(_f, ...) dbg_ll(_f, ## __VA_ARGS__)

static int _gc_pick(spfs_t *fs, bix_t *dbix);

typedef struct {
  bix_t dst_lbix;
  uint16_t pused;
  uint16_t pdele;
  uint16_t pfree;
} _gc_evacuate_varg_t;
static int _gc_evacuate_v(spfs_t *fs, uint32_t lu_entry, spfs_vis_info_t *info, void *varg) {
  _gc_evacuate_varg_t *arg = (_gc_evacuate_varg_t *)varg;
  id_t id = spfs_signext(lu_entry >> SPFS_LU_FLAG_BITS, SPFS_BITS_ID(fs));
  // only copy real ids
  if (id == SPFS_IDDELE) {
    arg->pdele++;
    return SPFS_VIS_CONT;
  }
  else if (id == SPFS_IDFREE) {
    arg->pfree++;
    return SPFS_VIS_CONT;
  }
  else if (id == SPFS_IDJOUR) {
    // TODO handle journal - if found, keep only the unfinished gc entry in dst
    //arg->pused++;
    //return SPFS_VIS_CONT;
  }
  dbg("evac id:"_SPIPRIid" @ dpix:"_SPIPRIpg"\n", id,info->dpix);
  arg->pused++;
  pix_t dst_lpix = arg->dst_lbix * SPFS_LPAGES_P_BLK(fs) + SPFS_LUPAGES_P_BLK(fs) +
      SPFS_DPIX2DBLKPIX(fs, info->dpix);
  pix_t src_lpix = _dpix2lpix(fs, info->dpix);
  // copy page
  int res = _page_copy(fs, dst_lpix, src_lpix, 0);
  ERR(res);
  // write lu entry
  res = _lu_write_lpix(fs, dst_lpix, lu_entry, SPFS_C_UP);
  ERR(res);

  return SPFS_VIS_CONT;
}
_SPFS_STATIC int spfs_gc_evacuate(spfs_t *fs, bix_t src_dbix) {
  bix_t dst_lbix = fs->run.lbix_gc_free;
  int res = SPFS_OK;
  bix_t src_lbix = _dbix2lbix(fs, src_dbix);
  dbg("dstlbix:"_SPIPRIbl" srcdbix:"_SPIPRIbl" srclbix:"_SPIPRIbl"\n", dst_lbix, src_dbix, src_lbix);
  dbg("fs pre  free:"_SPIPRIi" used:"_SPIPRIi" dele:"_SPIPRIi"\n", fs->run.pfree, fs->run.pused, fs->run.pdele);

  spfs_bhdr_t src_bhdr;
  spfs_bhdr_t dst_bhdr;
  res = _bhdr_rd(fs, src_lbix, &src_bhdr);
  ERR(res);
  res = _bhdr_rd(fs, dst_lbix, &dst_bhdr);
  ERR(res);

  // 1. write src block header as GC active
  res = _bhdr_write(fs, src_lbix, src_dbix, src_bhdr.era_cnt, 1, _SPFS_HAL_WR_FL_OVERWRITE);
  ERR(res);

  pix_t src_dpix_start = src_bhdr.dbix * SPFS_DPAGES_P_BLK(fs);
  pix_t src_dpix_end = src_dpix_start + SPFS_DPAGES_P_BLK(fs)-1;

  // 2. copy all data pages from src blk to dst blk
  _gc_evacuate_varg_t varg = {.dst_lbix = dst_lbix,
                              .pused = 0, .pdele = 0, .pfree = 0};
  res = spfs_page_visit(fs, src_dpix_start, src_dpix_end, &varg, _gc_evacuate_v, 0);
  if (res == -SPFS_ERR_VIS_END) res = SPFS_OK;
  ERR(res);
  dbg("src blk free:"_SPIPRIi" used:"_SPIPRIi" dele:"_SPIPRIi"\n", varg.pfree, varg.pused, varg.pdele);

  fs->run.pdele -= varg.pdele;
  fs->run.pfree += varg.pdele;

  // 3. write dst block header
  res = _bhdr_write(fs, dst_lbix, src_dbix, dst_bhdr.era_cnt, 0, _SPFS_HAL_WR_FL_OVERWRITE);
  ERRET(res);

  dbg("fs post free:"_SPIPRIi" used:"_SPIPRIi" dele:"_SPIPRIi"\n", fs->run.pfree, fs->run.pused, fs->run.pdele);

  // 4&5. erase src block, is now free block
  res = _block_erase(fs, src_lbix, SPFS_DBLKIX_FREE, src_bhdr.era_cnt+1);
  ERR(res);

  // update blk lu
  dbg("free "_SPIPRIi" bytes, new gc page lpix:"_SPIPRIbl"\n", varg.pdele * SPFS_DPAGE_SZ(fs), src_lbix);
  fs->run.lbix_gc_free = src_lbix;
  barr_set(&fs->run.blk_lu, src_dbix, dst_lbix);

}

typedef struct {
  bix_t visiting_dbix;
  bix_t visited_dbix;
  uint16_t pfree;
  uint16_t pdele;
  uint16_t pused;
  uint16_t era_cnt;
  bix_t cand_dbix;
  int32_t cand_score;
  uint16_t cand_pdele;
} _gc_pick_varg_t;
static void _gc_pick_calc_score(spfs_t *fs, _gc_pick_varg_t *arg, bix_t dbix) {
  int32_t score = 0;
  const int32_t norm = (const int32_t)SPFS_DPAGES_P_BLK(fs);

  int32_t score_era_cnt = SPFS_CFG_GC_WEIGHT_ERA_CNT(fs) *
      _era_cnt_diff(fs->run.max_era_cnt, arg->era_cnt);
  int32_t wscore_dele = SPFS_CFG_GC_WEIGHT_DELE(fs) * arg->pdele * 256;
  int32_t score_dele = spfs_round(wscore_dele, norm);
  int32_t wscore_free = SPFS_CFG_GC_WEIGHT_FREE(fs) * arg->pfree * 256;
  int32_t score_free = spfs_round(wscore_free, norm);
  int32_t wscore_used = SPFS_CFG_GC_WEIGHT_USED(fs) * arg->pused * 256;
  int32_t score_used = spfs_round(wscore_used, norm);
  score = score_era_cnt + score_dele + score_free + score_used;
  dbg("dbix:"_SPIPRIbl
      " era_diff:"_SPIPRIi"*"_SPIPRIi"("_SPIPRIi")"
      " dele:"_SPIPRIi"*"_SPIPRIi"("_SPIPRIi")"
      " free:"_SPIPRIi"*"_SPIPRIi"("_SPIPRIi")"
      " used:"_SPIPRIi"*"_SPIPRIi"("_SPIPRIi")"
      " score:"_SPIPRIi"\n",
      arg->visited_dbix,
      _era_cnt_diff(fs->run.max_era_cnt, arg->era_cnt), SPFS_CFG_GC_WEIGHT_ERA_CNT(fs), score_era_cnt,
      arg->pdele, SPFS_CFG_GC_WEIGHT_DELE(fs), score_dele,
      arg->pfree, SPFS_CFG_GC_WEIGHT_FREE(fs), score_free,
      arg->pused, SPFS_CFG_GC_WEIGHT_USED(fs), score_used,
      score);
  // due to rounding errors (if there are more than 256 pages per block), we
  // also look at number of deleted pages when there score is the same
  if (score > arg->cand_score ||
      (score == arg->cand_score && arg->pdele > arg->cand_pdele)) {
    arg->cand_score = score;
    arg->cand_pdele = arg->pdele;
    arg->cand_dbix = dbix;
  }
}
static int _gc_pick_v(spfs_t *fs, uint32_t lu_entry, spfs_vis_info_t *info,
                      void *varg) {
  int res;
  _gc_pick_varg_t *arg = (_gc_pick_varg_t *)varg;
  arg->visiting_dbix = info->dbix;
//  dbg("visiting:%d visited:%d dbix:%d lbix:%d fre:%d use:%d del:%d era:%d\n",
//      arg->visiting_dbix, arg->visited_dbix, info->dbix, info->lbix,
//      arg->pfree, arg->pused, arg->pdele, arg->era_cnt
//      );
  if (arg->visiting_dbix != arg->visited_dbix) {
    if (arg->visited_dbix != (bix_t)-1 ) {
      // new block, calc score
      _gc_pick_calc_score(fs, arg, arg->visited_dbix);
    }
    arg->visited_dbix = arg->visiting_dbix;
    arg->pfree = 0;
    arg->pdele = 0;
    arg->pused = 0;

    spfs_bhdr_t bhdr;
    res = _bhdr_rd(fs, info->lbix, &bhdr);
    ERR(res);
    arg->era_cnt = bhdr.era_cnt;
  }

  id_t id = spfs_signext(lu_entry >> SPFS_LU_FLAG_BITS, SPFS_BITS_ID(fs));
  if (id == SPFS_IDDELE) {
    arg->pdele++;
  } else if (id == SPFS_IDFREE) {
    arg->pfree++;
  } else {
    arg->pused++;
  }

  return SPFS_VIS_CONT;
}
static int _gc_pick(spfs_t *fs, bix_t *dbix) {
  _gc_pick_varg_t varg = { .visited_dbix = (bix_t)-1, .cand_score = -0x7fffffff,
                           .cand_dbix = (bix_t)-1, .cand_pdele = 0};
  dbg("norm:"_SPIPRIi"\n",SPFS_DPAGES_P_BLK(fs));
  int res = spfs_page_visit(fs, 0, 0, &varg, _gc_pick_v, 0);
  if (res == -SPFS_ERR_VIS_END) res = SPFS_OK;
  ERR(res);
  // process the last data block as the visit function is not called for the wrapped page
  _gc_pick_calc_score(fs, &varg, varg.visiting_dbix);
  dbg("dbix:"_SPIPRIbl" is gc candidate block, score:"_SPIPRIi"\n", varg.cand_dbix, varg.cand_score);
  if (dbix) *dbix = varg.cand_dbix;
  ERRET(res);
}


_SPFS_STATIC int spfs_gc(spfs_t *fs) {
  bix_t dbix = -1;
  int res = _gc_pick(fs, &dbix);
  ERR(res);
  if (dbix == (bix_t)-1) {
    dbg("no candidate\n");
    ERRET(SPFS_OK);
  }
  res = spfs_gc_evacuate(fs, dbix);
  ERRET(res);
}

