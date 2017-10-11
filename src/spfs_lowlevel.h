/*
 * spfs_lowlevel.h
 *
 *  Created on: Aug 15, 2017
 *      Author: petera
 */

#ifndef _SPFS_LL_H_
#define _SPFS_LL_H_

#include "spfs_compile_cfg.h"
#include "spfs.h"
#include "spfs_common.h"

//
// filesystem constants
//

// MSByte must be same
#define SPFS_BLK_MAGIC_STA        (0x5041)
#define SPFS_BLK_MAGIC_MID        (0x504f)
#define SPFS_BLK_MAGIC_END        (0x5052)
#define SPFS_BLK_MAGIC_NONE       (0xffff)

// block header, indicates that gc is not active for this block
#define SPFS_BLK_GC_INACTIVE      (0x5f)
// block header, indicates that gc is active for this block
// active flags must only reset bits wrt inactive flag
#define SPFS_BLK_GC_ACTIVE        (0x55)

// file system minimum number of logical blocks
#define _SPFS_BLOCK_CNT_MIN       3
// file system minimum number of logical pages per logical block
#define _SPFS_PAGE_CNT_MIN        4

// block header[ 2B:MAGIC 2B:DBIX 2B:ERA_CNT 1B:LBLKSZ 1B:LPAGESZ 1B:GCFLAG 2B:CHK ]
#define SPFS_BLK_HDR_SZ           (2+2+2+1+1+1+2)

#define SPFS_LU_FLAG_BITS         (1)
// lu entry is index
#define SPFS_LU_FL_INDEX          (0)
// lu entry is data
#define SPFS_LU_FL_DATA           (1)

#define SPFS_PHDR_FLAG_BITS       (4)
// page contains index data
#define SPFS_PHDR_FL_IDX          (1<<0)
// page data must be zeroed on deletion
#define SPFS_PHDR_FL_ZER          (1<<1)
//#define SPFS_PHDR_FL_FIN          (1<<2)
//#define SPFS_PHDR_FL_ENC          (1<<3)

#define SPFS_PIXHDR_TYPE_BITS     (2)
// normal file
#define SPFS_PIXHDR_TY_FILE       (0)
// fixed size file
#define SPFS_PIXHDR_TY_FIXFILE    (1)
// todo fixed size rotating file
//#define SPFS_PIXHDR_TY_ROTFILE    (2)
// todo directory entry
//#define SPFS_PIXHDR_TY_DIR        (3)
// todo link entry
//#define SPFS_PIXHDR_TY_LINK       (4)

#define SPFS_PIXHDR_FLAG_BITS     (2)
// todo fixed size rotating file is full, length is now offset
//#define SPFS_PIXHDR_FL_ROT_FULL   (1<<0)
// todo file contains sensitive data
//#define SPFS_PIXHDR_FL_SENS       (1<<1)

// needed extra ids: FREE DELE JOUR
#define SPFS_LU_EXTRA_IDS         (3)
// id indicating not taken
#define SPFS_IDFREE               (0xffffffff)
// id indicating deleted
#define SPFS_IDDELE               (0x00000000)
// id indicating journal
#define SPFS_IDJOUR               (0xfffffffe)

// used in spfs_page_find to indicate that we're finding index pages
#define SPFS_PAGE_FIND_FL_IX      (1<<0)

// used in _ixhdr_update to update name
#define SPFS_IXHDR_UPD_FL_NAME    (1<<0)
// used in _ixhdr_update to update size
#define SPFS_IXHDR_UPD_FL_SIZE    (1<<1)
// used in _ixhdr_update to update extra size
#define SPFS_IXHDR_UPD_FL_X_SIZE  (1<<2)
// used in _ixhdr_update to update flags
#define SPFS_IXHDR_UPD_FL_FLAGS   (1<<3)
// used in _ixhdr_update to update meta data
#define SPFS_IXHDR_UPD_FL_META    (1<<4)

#define SPFS_DBLKIX_FREE          (0xffff)

#define SPFS_FILESZ_UNDEF         (0xffffffff)

#define SPFS_CONFIGURED           (0xc0)
#define SPFS_MOUNTED              (0x4d)
#define SPFS_MOUNTED_DIRTY        (0x4e)

// annotations for medium operations
// used by cache to determine best course of action

// medium operation regarding look up index
#define SPFS_T_LU                 (1<<7)
// medium operation regarding data
#define SPFS_T_DATA               (1<<6)
// medium operation regarding meta data
#define SPFS_T_META               (1<<5)
// medium operation for removing
#define SPFS_C_RM                 (1<<4)
// medium operation for updating
#define SPFS_C_UP                 (1<<3)
// medium operation for clearing
#define SPFS_C_CL                 (1<<2)
// note: bits 0,1 are reservered

//
// filesystem derivations and conversions
//

//////////////////// filesystem structure ////////////////////

#if SPFS_CFG_DYNAMIC
#define SPFS_CFG_PFLASH_SZ(_fs)   (_fs)->cfg.pflash_sz
#define SPFS_CFG_PADDR_OFFS(_fs)  (_fs)->cfg.pflash_addr_offs
#define SPFS_CFG_LBLK_SZ(_fs)     (_fs)->cfg.lblk_sz
#define SPFS_CFG_PBLK_SZ(_fs)     (_fs)->cfg.pblk_sz
#define SPFS_CFG_LPAGE_SZ(_fs)    (_fs)->cfg.lpage_sz
#endif

// number of logical blocks
#if SPFS_CFG_DYNAMIC
#define SPFS_LBLK_CNT(_fs) \
  (_fs)->dyn.lblk_cnt
#else
#define SPFS_LBLK_CNT(_fs) \
  ( SPFS_CFG_PFLASH_SZ(_fs) / SPFS_CFG_LBLK_SZ(_fs) )
#endif

// number of logical pages per block
#if SPFS_CFG_DYNAMIC
#define SPFS_LPAGES_P_BLK(_fs) \
  (_fs)->dyn.lpages_p_blk
#else
#define SPFS_LPAGES_P_BLK(_fs) \
  ( SPFS_CFG_LBLK_SZ(_fs) / SPFS_CFG_LPAGE_SZ(_fs) )
#endif

// number of data pages per block
#if SPFS_CFG_DYNAMIC
#define SPFS_DPAGES_P_BLK(_fs) \
  (_fs)->dyn.dpages_p_blk
#else
#define SPFS_DPAGES_P_BLK(_fs) \
  ( SPFS_CFG_LBLK_SZ(_fs) / SPFS_CFG_LPAGE_SZ(_fs) - SPFS_LUPAGES_P_BLK(_fs))
#endif

// total number of data pages
#define SPFS_DPAGES_MAX(_fs) \
  ( SPFS_DPAGES_P_BLK(_fs) * (SPFS_LBLK_CNT(_fs) - 1) )

// max id
#define SPFS_MAX_ID(_fs) \
  ( (SPFS_CFG_PFLASH_SZ(_fs) - SPFS_CFG_LBLK_SZ(_fs)) / SPFS_CFG_LPAGE_SZ(_fs) \
    + SPFS_LU_EXTRA_IDS - SPFS_LBLK_CNT(_fs) )

// number of bits needed to repr all data pages
#if SPFS_CFG_DYNAMIC
#define SPFS_BITS_ID(_fs) \
  (_fs)->dyn.id_bits
#else
#define SPFS_BITS_ID(_fs) \
  ( spfs_log2(SPFS_MAX_ID(_fs)) )
#endif

// number of bits per lu entry
#define SPFS_LU_BITS(_fs) \
  ( SPFS_BITS_ID(_fs) + SPFS_LU_FLAG_BITS )

// number of bits needed to repr all log blocks
#if SPFS_CFG_DYNAMIC
#define SPFS_BITS_BLK(_fs) \
  (_fs)->dyn.blk_bits
#else
#define SPFS_BITS_BLK(_fs) \
  ( spfs_log2(SPFS_LBLK_CNT(_fs)) )
#endif

// number of lu entries in given block relative lu page
#if SPFS_CFG_DYNAMIC
#define SPFS_LU_ENT_CNT(_fs, _lu_pix) \
  ( (_lu_pix) == 0 ? (_fs)->dyn.lu_ent_cnt0 : (_fs)->dyn.lu_ent_cnt )
#else
#define SPFS_LU_ENT_CNT(_fs, _lu_pix) \
  ( 8 * (SPFS_CFG_LPAGE_SZ(_fs) - ((_lu_pix == 0 ? SPFS_BLK_HDR_SZ : 0))) / SPFS_LU_BITS(_fs) )
#endif

// number of lu pages per logical block
#if SPFS_CFG_DYNAMIC
#define SPFS_LUPAGES_P_BLK(_fs) \
  (_fs)->dyn.lupages_p_blk
#else
#define SPFS_LUPAGES_P_BLK(_fs) \
  spfs_ceil( SPFS_PPAGES_P_BLK(_fs) - SPFS_LU_ENT_CNT(_fs, 0) + SPFS_LU_ENT_CNT(_fs, 1), \
             SPFS_LU_ENT_CNT(_fs, 1) + 1 )
#endif

//////////////////// address conversions /////////////////////

// gives phys address of logical block
#define SPFS_LBLK2ADDR(_fs, lblk) \
  ( SPFS_CFG_PADDR_OFFS(_fs) + (lblk) * SPFS_CFG_LBLK_SZ(_fs) )

// gives phys address of logical page
#define SPFS_LPIX2ADDR(_fs, lpix) \
  ( SPFS_CFG_PADDR_OFFS(_fs) + (lpix) * SPFS_CFG_LPAGE_SZ(_fs) )

// gives logical page from phys addr
#define SPFS_ADDR2LPIX(_fs, addr) \
  ( ((addr) - SPFS_CFG_PADDR_OFFS(_fs)) / SPFS_CFG_LPAGE_SZ(_fs) )

// gives logical block from phys addr
#define SPFS_ADDR2LBLK(_fs, addr) \
  ( ((addr) - SPFS_CFG_PADDR_OFFS(_fs)) / SPFS_CFG_LBLK_SZ(_fs) )

// returns true if lpix is a lu page
#define SPFS_LPIXISLU(_fs, lpix) \
  ( ((lpix) % SPFS_LPAGES_P_BLK(_fs)) < SPFS_LUPAGES_P_BLK(_fs) )

// gives phys address for logical block and logical relative page
#define SPFS_LBLKLPIX2ADDR(_fs, lblk, lrpix) \
  ( SPFS_CFG_PADDR_OFFS(_fs) + (lblk) * SPFS_CFG_LBLK_SZ(_fs) + (lrpix) * SPFS_CFG_LPAGE_SZ(_fs) )

// gives phys address of data block
#define SPFS_DBLK2ADDR(_fs, dblk) \
  ( SPFS_CFG_PADDR_OFFS(_fs) + _dbix2lbix(_fs, dblk) * SPFS_CFG_LBLK_SZ(_fs) )

// returns physical address of data page
#define SPFS_DPIX2ADDR(_fs, dpix) \
  ( SPFS_LPIX2ADDR(_fs, _dpix2lpix(_fs, dpix)) )

// returns relative offset of data page header in page
#define SPFS_DPHDROFFS(_fs) \
  ( SPFS_CFG_LPAGE_SZ(_fs) - SPFS_PHDR_SZ(_fs))

// returns relative offset of data page header in page
#define SPFS_DPIXHDROFFS(_fs) \
  ( SPFS_CFG_LPAGE_SZ(_fs) - SPFS_PHDR_SZ(_fs) - SPFS_PIXHDR_SZ(_fs))

// returns physical address of data page header
#define SPFS_DPHDR2ADDR(_fs, dpix) \
  ( SPFS_DPIX2ADDR(_fs, dpix) + SPFS_DPHDROFFS(_fs) )

// returns physical address of data page index header
#define SPFS_DPIXHDR2ADDR(_fs, dpix) \
  ( SPFS_DPIX2ADDR(_fs, dpix) + SPFS_DPIXHDROFFS(_fs) )

////////////////// logical unit translations //////////////////

// gives data block index for data page index
#define SPFS_DPIX2DBLK(_fs, dpix) \
  ( (dpix) / SPFS_DPAGES_P_BLK(_fs) )

// gives log block index for log page index
#define SPFS_LPIX2LBLK(_fs, lpix) \
  ( (lpix) / SPFS_LPAGES_P_BLK(_fs) )

// gives relative data page index in data block for data page index
#define SPFS_DPIX2DBLKPIX(_fs, dpix) \
  ( (dpix) % SPFS_DPAGES_P_BLK(_fs) )

// gives relative log page index in log block for log page index
#define SPFS_LPIX2LBLKPIX(_fs, lpix) \
  ( (lpix) % SPFS_LPAGES_P_BLK(_fs) )

// gives logical page index pointing to lu page for given data page index
#define SPFS_DPIX2LLUPIX(_fs, dpix) \
  ( _dbix2lbix((_fs), SPFS_DPIX2DBLK(_fs, dpix)) * SPFS_LPAGES_P_BLK(_fs) + \
    SPFS_DPIX2BLKLUPIX(_fs, dpix) \
  )

// gives logical page index pointing to lu page for given logical page index
#define SPFS_LPIX2LLUPIX(_fs, lpix) \
  ( SPFS_LPIX2LBLK(_fs, lpix) * SPFS_LPAGES_P_BLK(_fs) + ( \
    (SPFS_LPIX2LBLKPIX(_fs, lpix) - SPFS_LUPAGES_P_BLK(_fs)) <  SPFS_LU_ENT_CNT(_fs, 0) \
      ? 0 \
      : (1 + (SPFS_LPIX2LBLKPIX(_fs, lpix) - SPFS_LUPAGES_P_BLK(_fs) - SPFS_LU_ENT_CNT(_fs, 0)) \
             / SPFS_LU_ENT_CNT(_fs, 1) ) \
    ) \
  )

// gives relative lu page index in log block for data page index
#define SPFS_DPIX2BLKLUPIX(_fs, dpix) \
  ( SPFS_DPIX2DBLKPIX(_fs, dpix) < SPFS_LU_ENT_CNT(_fs, 0) ? \
    0 : \
    (1+(SPFS_DPIX2DBLKPIX(_fs, dpix) - SPFS_LU_ENT_CNT(_fs, 0)) / SPFS_LU_ENT_CNT(_fs, 1)) \
  )

// returns if the related lu page is the first in block for given data page index
#define SPFS_DPIX_FIRSTBLKLU(_fs, dpix) \
  ( SPFS_DPIX2DBLKPIX(_fs, dpix) < SPFS_LU_ENT_CNT(_fs, 0) )

// returns if the related lu page is the first in block for given log page index
#define SPFS_LPIX_FIRSTBLKLU(_fs, lpix) \
  ( (SPFS_LPIX2LBLKPIX(_fs, lpix) - SPFS_LUPAGES_P_BLK(fs)) < SPFS_LU_ENT_CNT(_fs, 0) )

// gives relative lu entry index in lu page for data page index
#define SPFS_DPIX2LUENT(_fs, dpix) \
  ( SPFS_DPIX2DBLKPIX(_fs, dpix) < SPFS_LU_ENT_CNT(_fs, 0) ? \
    SPFS_DPIX2DBLKPIX(_fs, dpix) : \
    (SPFS_DPIX2DBLKPIX(_fs, dpix) - SPFS_LU_ENT_CNT(_fs, 0)) % SPFS_LU_ENT_CNT(_fs, 1) \
  )

// gives relative lu entry index in lu page for log page index
#define SPFS_LPIX2LUENT(_fs, lpix) \
  ( (SPFS_LPIX2LBLKPIX(_fs, lpix) - SPFS_LUPAGES_P_BLK(_fs)) <  SPFS_LU_ENT_CNT(_fs, 0) \
      ? (SPFS_LPIX2LBLKPIX(_fs, lpix) - SPFS_LUPAGES_P_BLK(_fs)) \
      : ((SPFS_LPIX2LBLKPIX(_fs, lpix) - SPFS_LUPAGES_P_BLK(_fs) - SPFS_LU_ENT_CNT(_fs, 0)) \
             % SPFS_LU_ENT_CNT(_fs, 1) ) \
  )


// maximum page header size
#define SPFS_PHDR_MAX_SZ \
  spfs_ceil(8*sizeof(uint32_t)*2 + SPFS_PHDR_FLAG_BITS, 8)

// actual page header size
#define SPFS_PHDR_SZ(_fs) \
  spfs_ceil(2 * SPFS_BITS_ID(_fs) + SPFS_PHDR_FLAG_BITS, 8)

// maximum page index header size
#define SPFS_PIXHDR_MAX_SZ \
  ( spfs_ceil(32 + \
              32 + \
              8*SPFS_CFG_FILE_NAME_SZ + \
              SPFS_PIXHDR_TYPE_BITS + \
              SPFS_PIXHDR_FLAG_BITS \
              , 8) \
    + SPFS_CFG_FILE_META_SZ )

// actual page index header size
#define SPFS_PIXHDR_SZ(_fs) \
  SPFS_PIXHDR_MAX_SZ

// returns user data size in a data page
#define SPFS_DPAGE_SZ(_fs) \
  ( SPFS_CFG_LPAGE_SZ(_fs) - SPFS_PHDR_SZ(_fs) )

// returns number of entries in given fileindex spax index
#define SPFS_IX_ENT_CNT(_fs, spix) \
  ( (8*((spix) ? SPFS_DPAGE_SZ(_fs) : (SPFS_DPAGE_SZ(_fs) - SPFS_PIXHDR_SZ(_fs)))) \
    / SPFS_BITS_ID(_fs) \
  )

// returns how many file bytes a fileindex of given span index can address
#define SPFS_IXSPIX2DBYTES(_fs, spix) \
  ( SPFS_IX_ENT_CNT(_fs, spix) * SPFS_DPAGE_SZ(_fs) )


// returns file page span index for given file offset
#define SPFS_OFFS2SPIX(_fs, offset) \
  ( (offset) / SPFS_DPAGE_SZ(fs) )

// returns file indexpage span index for given file offset
#define SPFS_OFFS2IXSPIX(_fs, offset) \
  ( (offset) < SPFS_IXSPIX2DBYTES(_fs, 0) ? 0 \
    : (((offset) - SPFS_IXSPIX2DBYTES(_fs, 0)) / (SPFS_IXSPIX2DBYTES(_fs, 1))+1) \
  )

// returns starting file offset for given indexpage span index
#define SPFS_IXSPIX2OFFS(_fs, ixspix) \
  ( (ixspix)==0 ? 0 : \
    ((SPFS_IX_ENT_CNT(_fs, 0) + \
        ((ixspix)-1) * SPFS_IX_ENT_CNT(_fs, 1)) * SPFS_DPAGE_SZ(_fs)) \
  )

// returns indexpage span index for datapage span index
#define SPFS_DSPIX2IXSPIX(_fs, dspix) \
  ( (dspix) < SPFS_IX_ENT_CNT(fs, 0) ? 0 : \
      (((dspix) - SPFS_IX_ENT_CNT(fs, 0)) / SPFS_IX_ENT_CNT(fs, 1)) \
  )

//
// common operations and arithmetic
//

#define spfs_mwr16(_m, _ix, _v) do { \
  ((uint8_t *)(_m))[(_ix)]=(uint8_t)(_v); \
  ((uint8_t *)(_m))[(_ix)+1]=(uint8_t)((_v)>>8); \
} while(0)
#define spfs_mrd16(_m, _ix) \
  ( ((uint8_t *)(_m))[(_ix)] | (((uint8_t *)(_m))[(_ix)+1]<<8) )
#define spfs_mwr8(_m, _ix, _v) do { \
  ((uint8_t *)(_m))[(_ix)]=(uint8_t)(_v); \
} while(0)
#define spfs_mrd8(_m, _ix) \
  ( ((uint8_t *)(_m))[(_ix)] )

#define spfs_ceil(x,y) \
  ( ((x) + (y) - 1)/(y) )
#define spfs_round(x,y) \
  ( (x) >= 0 ? (((x) + (y) - 1)/(y)) : (((x) - (y) + 1)/(y)) )
#define spfs_align(x,y) \
  ( spfs_ceil(x,y)*(y) )
#define spfs_max(x,y) \
  ( (x)>(y)?(x):(y) )
#define spfs_min(x,y) \
  ( (x)<(y)?(x):(y) )
#define spfs_signext(x,bits) \
  ( (uint32_t)((int32_t)((x) << (32-(bits))) >> (32-(bits))) )
#define spfs_mask(x,bits) \
  ( (uint32_t)((x) & ((1<<(bits))-1)) )

#ifndef spfs_memset
#define spfs_memset(_m, _x, _n)         memset((_m),(_x),(_n))
#endif
#ifndef spfs_memcpy
#define spfs_memcpy(_d, _s, _n)         memcpy((_d),(_s),(_n))
#endif
#ifndef spfs_strncpy
#define spfs_strncpy(_d, _s, _n)        strncpy((_d),(_s),(_n))
#endif
#ifndef spfs_strncmp
#define spfs_strncmp(_s1, _s2, _n)      strncmp((_s1),(_s2),(_n))
#endif


//
// filesystem structs
//

/**
 * Block header.
 */
typedef struct {
  uint16_t magic;
  uint16_t dbix;
  uint16_t era_cnt;
  uint32_t lblk_sz;
  uint32_t lpage_sz;
  uint8_t gc_flag;
  uint16_t pchk;
  uint16_t lchk;
} spfs_bhdr_t;

/**
 * Page header. Part of all pages except LU pages.
 */
typedef struct {
  /** id to which this page belongs */
  id_t id;
  /** span index of the id */
  spix_t span;
  /** page flags */
  uint8_t p_flags;
} spfs_phdr_t;


/**
 * File info
 */
typedef struct {
  id_t id;
  /**
   * Size of file. In rotating files, size of file
   * when (flags & SPFS_PIXHDR_FL_ROT_FULL) != 0, else
   * wrapping offset.
   */
  uint32_t size;
  /** max file size for fixed and rotating files */
  uint32_t x_size;
  /** file type */
  uint8_t type;
  /** file flags */
  uint8_t f_flags;
} spfs_fi_t;

/**
 * Page index header, file header.
 */
typedef struct {
  /** page header */
  spfs_phdr_t phdr;
  /** file info */
  spfs_fi_t fi;
  /** file name */
  uint8_t name[SPFS_CFG_FILE_NAME_SZ];
#if SPFS_CFG_FILE_META_SZ
  /** user meta data */
  uint8_t meta[SPFS_CFG_FILE_META_SZ];
#endif
} spfs_pixhdr_t;

//
// filesystem visitor
//

// internal visitor return code, keep searching
#define SPFS_VIS_CONT               (_SPFS_ERR_INT+1)
// internal visitor return code, reload lu to work buf and keep searching
#define SPFS_VIS_CONT_LU_RELOAD     (_SPFS_ERR_INT+2)
// internal visitor return code, stop searching
#define SPFS_VIS_STOP               (_SPFS_ERR_INT+3)
// internal visitor return code, reached end
#define SPFS_ERR_VIS_END            (_SPFS_ERR_INT+4)
// internal return code, assert
#define SPFS_ERR_ASSERT             (_SPFS_ERR_INT+5)

/** Visitor context information, passed to visitor func */
typedef struct {
  /** data page index */
  pix_t dpix;
  /** data block index */
  bix_t dbix;
  /** log block index */
  bix_t lbix;
  /** relative lu page index in block */
  pix_t lupix;
} spfs_vis_info_t;


/**
 * Called from spfs_visit for each LU entry.
 * If user returns SPFS_VIS_CONT, the visitor will keep examining next LU entry.
 * If user returns SPFS_VIS_STOP, the visitor will stop traversing the LUs.
 * If user returns SPFS_VS_CONT_LU_RELOAD, it means that the visitor will reload
 * the LU into workbuffer 1 before proceeding (i.e. the visitor function used
 * workbuffer 1 for other stuff)
 * If anything else is returned, the visitor will stop and return same error code.
 */
typedef int (*spfs_visitor_t)(spfs_t *fs, uint32_t lu_entry, spfs_vis_info_t *info, void *varg);

_SPFS_STATIC int spfs_page_visit(spfs_t *fs, pix_t start_dpix, pix_t end_dpix, void *varg, spfs_visitor_t v,
               uint16_t flags);

/**
 * packnum and unpacknum format
 * bit 76543210
 *     xxxyyyyy
 * where x : [1,3,5,7,9,11,13,15]
 *       y : 2^(y+6)
 * (2*x+1)*2^(y+6)
 */
_SPFS_STATIC uint8_t spfs_packnum(uint32_t x);
_SPFS_STATIC uint32_t spfs_unpacknum(uint8_t x);

_SPFS_STATIC uint16_t _chksum(const uint8_t *data, uint32_t len, uint16_t init_checksum);
_SPFS_STATIC uint16_t spfs_bhdr_chksum(uint8_t *blk_hdr, uint8_t ignore_gc_state);
_SPFS_STATIC void spfs_bhdr_parse(spfs_bhdr_t *b, uint8_t *bhdr, uint8_t ignore_gc_state);


_SPFS_STATIC int spfs_page_find(spfs_t *fs, id_t id, spix_t span, uint8_t flags, pix_t *dpix);
_SPFS_STATIC int spfs_page_hdr_write(spfs_t *fs, pix_t dpix, spfs_phdr_t *phdr, uint32_t wr_flags);
_SPFS_STATIC int spfs_page_ixhdr_write(spfs_t *fs, pix_t dpix, spfs_pixhdr_t *phdrix, uint32_t wr_flags);

_SPFS_STATIC bix_t _dbix2lbix(spfs_t *fs, bix_t dbix);
_SPFS_STATIC pix_t _dpix2lpix(spfs_t *fs, pix_t dpix);
_SPFS_STATIC uint16_t _era_cnt_max(uint16_t a, uint16_t b);
_SPFS_STATIC uint16_t _era_cnt_diff(uint16_t big, uint16_t small);

_SPFS_STATIC int _medium_erase(spfs_t *fs, uint32_t addr, uint32_t len, uint32_t er_flags);
_SPFS_STATIC int _medium_write(spfs_t *fs, uint32_t addr, const uint8_t *src, uint32_t len, uint32_t wr_flags);
_SPFS_STATIC int _medium_read(spfs_t *fs, uint32_t addr, uint8_t *dst, uint32_t len, uint32_t rd_flags);

_SPFS_STATIC int _bhdr_write(spfs_t *fs, bix_t lbix, bix_t dbix, uint16_t era,
                             uint8_t gc_active, uint32_t wr_flags);
_SPFS_STATIC int _bhdr_rd(spfs_t *fs, bix_t lbix, spfs_bhdr_t *b, uint8_t raw[SPFS_BLK_HDR_SZ]);
_SPFS_STATIC int _block_erase(spfs_t *fs, bix_t lbix, bix_t dbix, uint16_t era);

_SPFS_STATIC int _lu_write_lpix(spfs_t *fs, pix_t lpix, uint32_t value, uint32_t wr_flags);
_SPFS_STATIC int _lu_page_allocate(spfs_t *fs, pix_t dpix, id_t id, uint8_t lu_flags);
_SPFS_STATIC int _lu_page_delete(spfs_t *fs, pix_t dpix);

_SPFS_STATIC void _phdr_rdmem(spfs_t *fs, uint8_t *mem, spfs_phdr_t *phdr);
_SPFS_STATIC void _phdr_wrmem(spfs_t *fs, uint8_t *mem, spfs_phdr_t *phdr);
_SPFS_STATIC uint32_t _pixhdr_rdmem_sz(spfs_t *fs, uint8_t *mem);
_SPFS_STATIC void _pixhdr_wrmem_sz(spfs_t *fs, uint8_t *mem, uint32_t sz);
_SPFS_STATIC int _page_hdr_read(spfs_t *fs, pix_t dpix, spfs_phdr_t *phdr, uint32_t rd_flags);
_SPFS_STATIC int _page_ixhdr_read(spfs_t *fs, pix_t dpix, spfs_pixhdr_t *pixhdr, uint32_t rd_flags);
_SPFS_STATIC int _page_copy(spfs_t *fs, pix_t dst_lpix, pix_t src_lpix, uint8_t only_data);
_SPFS_STATIC int _id_find_free(spfs_t *fs, id_t *id, const char *unique_name);
_SPFS_STATIC int _page_find_free(spfs_t *fs, pix_t *dpix);
_SPFS_STATIC int _page_allocate_free(spfs_t *fs, pix_t *dpix, id_t id, uint8_t lu_flag);
_SPFS_STATIC int _resv_alloc(spfs_t *fs);
_SPFS_STATIC int _resv_free(spfs_t *fs, uint8_t rix);

#endif /* _SPFS_LL_H_ */
