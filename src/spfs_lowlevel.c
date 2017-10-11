/*
 * spfs_lowlevel.c
 *
 *  Created on: Sep 9, 2017
 *      Author: petera
 */

//@REQUIRED_GCOV:85

#include "spfs_compile_cfg.h"
#include "spfs.h"
#include "spfs_lowlevel.h"

#undef _SPFS_DBG_PRE
#undef _SPFS_DBG_POST
#define _SPFS_DBG_PRE ANSI_COLOR_GREEN"LL "
#define _SPFS_DBG_POST ANSI_COLOR_RESET
#include "spfs_dbg.h"
#undef dbg
#define dbg(_f, ...) dbg_ll(_f, ## __VA_ARGS__)

///////////////////////////////////////////////////////////////////////////////
// common arithmetics
///////////////////////////////////////////////////////////////////////////////


// bit 76543210
//     xxxyyyyy
// where x : [1,3,5,7,9,11,13,15]
//       y : 2^(y+6)
// (2*x+1)*2^(y+6)
// packs a 32-bit to an 8-bit
_SPFS_STATIC uint8_t spfs_packnum(uint32_t x) {
  x >>= 6;
  if (x == 0) return 0;
  // ctz
  uint8_t lg2 = 0;
  while ((x & (1<<lg2)) == 0) lg2++;
  return (((x >> (lg2) & 0xf)/2) << 5) | lg2;
}

// unpacks an 8-bit to a 32-bit
_SPFS_STATIC uint32_t spfs_unpacknum(uint8_t x) {
  return (uint32_t)((2*(x >> 5)+1) * (1 << ((x & 0x1f) + 6)));
}

// calculates checksum for data block
_SPFS_STATIC uint16_t _chksum(const uint8_t *data, uint32_t len, uint16_t init_checksum) {
  uint16_t chk = init_checksum;
  uint16_t i;
  for (i = 0; i < len; i++) {
    chk = (chk >> 1) + ((chk & 1) << 15);
    chk += data[i];
  }
  return chk;
}

// calculates checksum for given block header
// ignore_gc_state will make checksum unaffected regardless what
// the gc state byte is set to in block header
_SPFS_STATIC uint16_t spfs_bhdr_chksum(uint8_t *bhdr, uint8_t ignore_gc_state) {
  uint8_t gc_flag = bhdr[8];
  bhdr[8] |=  (ignore_gc_state ? SPFS_BLK_GC_INACTIVE : 0);
  uint16_t chk = _chksum(bhdr, SPFS_BLK_HDR_SZ-2, 0);
  bhdr[8] = gc_flag;
  return chk;
}

// converts a data block index to a log block index
_SPFS_STATIC bix_t _dbix2lbix(spfs_t *fs, bix_t dbix) {
  return barr_get(&fs->run.blk_lu, dbix);
}

// converts a data page index to a log page index
_SPFS_STATIC pix_t _dpix2lpix(spfs_t *fs, pix_t dpix) {
  return
    _dbix2lbix(fs, dpix / SPFS_DPAGES_P_BLK(fs)) * SPFS_LPAGES_P_BLK(fs)
    + SPFS_LUPAGES_P_BLK(fs)
    + (dpix % SPFS_DPAGES_P_BLK(fs));
}

//int spfs_prereqs(spfs *fs) {
//  if (fs->config_state != SPFS_CONFIGURED) return -SPFS_ERR_UNCONFIGURED;
//  if (fs->mount_state != SPFS_MOUNTED) return -SPFS_ERR_MOUNT_STATE;
//  return SPFS_OK;
//}


#define ERA_MSB_MASK (1 << (sizeof(uint16_t)*8-1))
// returns highest erase count, takes care of wrapping
_SPFS_STATIC uint16_t _era_cnt_max(uint16_t a, uint16_t b) {
  if ((a & ERA_MSB_MASK) == (b & ERA_MSB_MASK)) {
    // a=1,b=2 or a=0xfffc,b=0xfffd
    return a > b ? a : b;
  } else if (a & ERA_MSB_MASK) {
    // a=0xfffc,b=2
    return b;
  } else /*if ((b & ERA_MSB_MASK))*/ {
    // a=2,b=0xfffc
    return a;
  }
}
// returns erase count difference, big-small, takes care of wrapping
_SPFS_STATIC uint16_t _era_cnt_diff(uint16_t big, uint16_t small) {
  if (big >= small) {
    return big-small;
  } else {
    return 0xffff-(small-big)+1;
  }
}

///////////////////////////////////////////////////////////////////////////////
// medium / hal access
///////////////////////////////////////////////////////////////////////////////

// erase a block on medium
_SPFS_STATIC int _medium_erase(spfs_t *fs, uint32_t addr, uint32_t len, uint32_t er_flags) {
#if SPFS_DBG_LL_MEDIUM_ER
  if (fs) {
    dbg("ER@"_SPIPRIad" lbix:"_SPIPRIbl " sz:"_SPIPRIi"\n",
        addr, SPFS_ADDR2LPIX(fs, addr), len);
  }
#endif
  int res = SPFS_OK;
  uint32_t blksz = SPFS_CFG_PBLK_SZ(fs);
  while (res == SPFS_OK && len > 0) {
    res = fs->cfg.erase(fs, addr, blksz _SPFS_TEST_ARG(er_flags));
    addr += blksz;
    len -= blksz;
  }
  ERRET(res);
}

// writes data to medium
_SPFS_STATIC int _medium_write(spfs_t *fs, uint32_t addr, const uint8_t *src, uint32_t len, uint32_t wr_flags) {
#if SPFS_DBG_LL_MEDIUM_WR
  if (fs) {
    dbg("WR@"_SPIPRIad" lpix:"_SPIPRIpg "%s %s,%s sz:"_SPIPRIi"\n",
        addr,
        SPFS_ADDR2LPIX(fs, addr),
        (addr - SPFS_CFG_PADDR_OFFS(fs)) % SPFS_CFG_LBLK_SZ(fs) < SPFS_BLK_HDR_SZ ? "BL" :
            SPFS_LPIXISLU(fs, SPFS_ADDR2LPIX(fs, addr)) ? "LU":"  ",
        wr_flags & SPFS_T_LU ? "LU" :
            wr_flags & SPFS_T_META ? "ME" :
                wr_flags & SPFS_T_DATA ? "DA" : "BL",
        wr_flags & SPFS_C_RM ? "RM" :
            wr_flags & SPFS_C_UP ? "UP" : "WR",
        len);
  }
#endif
  int res = fs->cfg.write(fs, addr, src, len _SPFS_TEST_ARG(wr_flags));
  ERRET(res);
}

// reads data from medium
_SPFS_STATIC int _medium_read(spfs_t *fs, uint32_t addr, uint8_t *dst, uint32_t len, uint32_t rd_flags) {
#if SPFS_DBG_LL_MEDIUM_RD
  if (fs) {
    dbg("RD@"_SPIPRIad" lpix:"_SPIPRIpg "%s %s sz:"_SPIPRIi"\n",
        addr,
        SPFS_ADDR2LPIX(fs, addr),
        (addr - SPFS_CFG_PADDR_OFFS(fs)) % SPFS_CFG_LBLK_SZ(fs) < SPFS_BLK_HDR_SZ ? "BL" :
            SPFS_LPIXISLU(fs, SPFS_ADDR2LPIX(fs, addr)) ? "LU":"  ",
        rd_flags & SPFS_T_LU ? "LU" :
            rd_flags & SPFS_T_META ? "ME" :
                rd_flags & SPFS_T_DATA ? "DA" : "BL",
        len);
  }
#endif
  int res = fs->cfg.read(fs, addr, dst, len _SPFS_TEST_ARG(rd_flags));
  ERRET(res);
}

///////////////////////////////////////////////////////////////////////////////
// block operations
///////////////////////////////////////////////////////////////////////////////


// writes a blockheader with given erase count and whether gc is active or not
_SPFS_STATIC int _bhdr_write(spfs_t *fs, bix_t lbix, bix_t dbix, uint16_t era,
                             uint8_t gc_active, uint32_t wr_flags) {
  uint8_t bhdr[SPFS_BLK_HDR_SZ];
  uint32_t paddr = SPFS_CFG_PADDR_OFFS(fs) + lbix * SPFS_CFG_LBLK_SZ(fs);
  spfs_mwr8(bhdr, 6, spfs_packnum(SPFS_CFG_LBLK_SZ(fs)));
  spfs_mwr8(bhdr, 7, spfs_packnum(SPFS_CFG_LPAGE_SZ(fs)));
  spfs_mwr8(bhdr, 8, gc_active ? SPFS_BLK_GC_ACTIVE : SPFS_BLK_GC_INACTIVE);
  uint16_t magic;
  if (lbix == 0) {
    magic = SPFS_BLK_MAGIC_STA;
  } else if (lbix == SPFS_LBLK_CNT(fs)-1) {
    magic = SPFS_BLK_MAGIC_END;
  } else {
    magic = SPFS_BLK_MAGIC_MID;
  }
  spfs_mwr16(bhdr, 0, magic);
  spfs_mwr16(bhdr, 2, dbix);
  spfs_mwr16(bhdr, 4, era);
  uint16_t chk = dbix == SPFS_DBLKIX_FREE ? 0xffff : spfs_bhdr_chksum(bhdr, 1);
  spfs_mwr16(bhdr, 9, chk);

  int res = _medium_write(fs, paddr, bhdr, SPFS_BLK_HDR_SZ, wr_flags);
  ERRET(res);
}

// parses a blockheader from memory to struct
// ignore_gc_state will make checksum unaffected regardless what
// the gc state byte is set to in block header
_SPFS_STATIC void spfs_bhdr_parse(spfs_bhdr_t *b, uint8_t *bhdr, uint8_t ignore_gc_state) {
  b->magic = spfs_mrd16(bhdr, 0);
  b->dbix = spfs_mrd16(bhdr, 2);
  b->era_cnt = spfs_mrd16(bhdr, 4);
  b->lblk_sz = spfs_unpacknum(spfs_mrd8(bhdr, 6));
  b->lpage_sz = spfs_unpacknum(spfs_mrd8(bhdr, 7));
  b->gc_flag = spfs_mrd8(bhdr, 8);
  b->pchk = spfs_mrd16(bhdr, 9);
  b->lchk = spfs_bhdr_chksum(bhdr, ignore_gc_state);
}

// reads block header to memory, and parses it to given struct
_SPFS_STATIC int _bhdr_rd(spfs_t *fs, bix_t lbix, spfs_bhdr_t *bhdr, uint8_t raw[SPFS_BLK_HDR_SZ]) {
  int res = _medium_read(fs, SPFS_LBLK2ADDR(fs, lbix), raw, SPFS_BLK_HDR_SZ, 0);
  ERR(res);
  spfs_bhdr_parse(bhdr, raw, 1);
  return res;
}

// erases a block and writes the block header
_SPFS_STATIC int _block_erase(spfs_t *fs, bix_t lbix, bix_t dbix, uint16_t era) {
  dbg("erase lbix:"_SPIPRIbl" dbix:"_SPIPRIbl"\n", lbix, dbix);
  fs->run.max_era_cnt = _era_cnt_max(era, fs->run.max_era_cnt);
  uint32_t paddr = SPFS_CFG_PADDR_OFFS(fs) + lbix * SPFS_CFG_LBLK_SZ(fs);
  int res = _medium_erase(fs, paddr, SPFS_CFG_LBLK_SZ(fs), 0);
  ERR(res);
  res = _bhdr_write(fs, lbix, dbix, era, 0, 0);
  ERRET(res);
}


///////////////////////////////////////////////////////////////////////////////
// LU operations
///////////////////////////////////////////////////////////////////////////////

// writes an entry to ta LU page for corresponding logical page index
_SPFS_STATIC int _lu_write_lpix(spfs_t *fs, pix_t lpix, uint32_t value, uint32_t wr_flags) {
  dbg("%s lpix:"_SPIPRIpg" val:"_SPIPRIid" (id:"_SPIPRIid" fl:"_SPIPRIfl")\n",
      value >> SPFS_LU_FLAG_BITS ? "SET" : "DEL",
      lpix, value, value >> SPFS_LU_FLAG_BITS, value & ((1<<SPFS_LU_FLAG_BITS)-1));

  spfs_assert(lpix < (uint32_t)(SPFS_LPAGES_P_BLK(fs) * SPFS_LBLK_CNT(fs)));

  uint8_t mem[5] = {0xff, 0xff, 0xff, 0xff, 0xff}; // max 32 bits + 0..7 bits offs
  // get physical address of relevant lu page
  uint32_t lu_page_addr = SPFS_LPIX2ADDR(fs, SPFS_LPIX2LLUPIX(fs, lpix));
  // get bit index of entry in relevant lu page
  uint32_t bitpos_lu_entry = SPFS_LPIX2LUENT(fs, lpix) * (SPFS_BITS_ID(fs) + SPFS_LU_FLAG_BITS);

  // write id to memory only comprising the change on medium
  bstr8 bs;
  bstr8_init(&bs, mem);
  bstr8_setp(&bs, bitpos_lu_entry % 8);
  bstr8_wr(&bs, SPFS_BITS_ID(fs) + SPFS_LU_FLAG_BITS, value);

  // get address and length of bytes in lu page to actually update
  uint32_t lu_entry_addr = lu_page_addr + bitpos_lu_entry/8;
  if (SPFS_LPIX_FIRSTBLKLU(fs, lpix)) {
    lu_entry_addr += SPFS_BLK_HDR_SZ;
  }
  uint32_t lu_entry_len = spfs_ceil(bstr8_getp(&bs), 8);
  // and write
  return _medium_write(fs, lu_entry_addr, mem, lu_entry_len, wr_flags | SPFS_T_LU |
                        (_SPFS_HAL_WR_FL_OVERWRITE | _SPFS_HAL_WR_FL_IGNORE_BITS));
}

// writes an entry to ta LU page for corresponding data page index
static int _lu_write_dpix(spfs_t *fs, pix_t dpix, uint32_t value, uint32_t wr_flags) {
  return _lu_write_lpix(fs, _dpix2lpix(fs, dpix), value, wr_flags);
}

// allocates given data page index in the LU pages with given id
_SPFS_STATIC int _lu_page_allocate(spfs_t *fs, pix_t dpix, id_t id, uint8_t lu_flags) {
  int res = _lu_write_dpix(fs, dpix, (id << SPFS_LU_FLAG_BITS) | lu_flags,
                           SPFS_C_UP);
  ERR(res);
  fs->run.pused++;
  fs->run.pfree--;
  ERRET(res);
}

// delets given data page index in the LU
// if sensitive data is enabled, this function checks whether the data page
// should also be deleted
_SPFS_STATIC int _lu_page_delete(spfs_t *fs, pix_t dpix) {
  spfs_assert(dpix < (pix_t)SPFS_DPAGES_MAX(fs));
  int res = _lu_write_dpix(fs, dpix, 0, SPFS_C_RM);
#if SPFS_CFG_SENSITIVE_DATA
  ERR(res);
  spfs_phdr_t phdr;
  res = _page_hdr_read(fs, dpix, &phdr, 0);
  ERR(res);
  if ((phdr.p_flags & SPFS_PHDR_FL_ZER) == 0) {
    dbg("clear dpix:"_SPIPRIpg"\n", dpix);
    uint8_t buf[SPFS_CFG_COPY_BUF_SZ];
    memset(buf, 0x00, SPFS_CFG_COPY_BUF_SZ);
    uint32_t rem_sz = SPFS_DPAGE_SZ(fs);
    uint32_t addr = SPFS_DPIX2ADDR(fs, dpix);
    while (rem_sz) {
      uint32_t sz = spfs_min(rem_sz, SPFS_CFG_COPY_BUF_SZ);
      res = _medium_write(fs, addr, buf, sz, SPFS_C_CL | SPFS_T_DATA | _SPFS_HAL_WR_FL_OVERWRITE);
      ERR(res);
      addr += sz;
      rem_sz -= sz;
    }
  }
#endif
  fs->run.pdele++;
  ERRET(res);
}


///////////////////////////////////////////////////////////////////////////////
// page operations
///////////////////////////////////////////////////////////////////////////////

// unpacks given page header memory into given struct
_SPFS_STATIC void _phdr_rdmem(spfs_t *fs, uint8_t *mem, spfs_phdr_t *phdr) {
  bstr8 bs;
  const uint8_t bits_id = SPFS_BITS_ID(fs);
  bstr8_init(&bs, mem);
  phdr->id = bstr8_rd(&bs, bits_id);
  phdr->span = bstr8_rd(&bs, bits_id);
  phdr->p_flags= bstr8_rd(&bs, SPFS_PHDR_FLAG_BITS);
}

// packs given page header struct to given memory
_SPFS_STATIC void _phdr_wrmem(spfs_t *fs, uint8_t *mem, spfs_phdr_t *phdr) {
  const uint8_t bits_id = SPFS_BITS_ID(fs);
  bstr8 bs;
  bstr8_init(&bs, mem);
  bstr8_wr(&bs, bits_id, phdr->id);
  bstr8_wr(&bs, bits_id, phdr->span);
  bstr8_wr(&bs, SPFS_PHDR_FLAG_BITS, phdr->p_flags);
}

// unpacks given page index header memory into given struct, excluding page header
static void _pixhdr_rdmem(spfs_t *fs, uint8_t *mem, spfs_pixhdr_t *pixhdr) {
  (void)fs;
  bstr8 bs;
  bstr8_init(&bs, mem);
  pixhdr->fi.size = bstr8_rd(&bs, 32);
  pixhdr->fi.x_size = bstr8_rd(&bs, 32);
  spfs_strncpy((char *)pixhdr->name, (char *)&mem[(32+32)/8], SPFS_CFG_FILE_NAME_SZ);
  bstr8_setp(&bs, 32 + 32 + 8 * SPFS_CFG_FILE_NAME_SZ);
  pixhdr->fi.type = bstr8_rd(&bs, SPFS_PIXHDR_TYPE_BITS);
  pixhdr->fi.f_flags = bstr8_rd(&bs, SPFS_PIXHDR_FLAG_BITS);
#if SPFS_CFG_FILE_META_SZ
  spfs_memcpy(pixhdr->meta, &mem[SPFS_PIXHDR_SZ(fs) - SPFS_CFG_FILE_META_SZ],
              SPFS_CFG_FILE_META_SZ);
#endif
}

// packs given page index header struct to given memory, excluding page header
static void _pixhdr_wrmem(spfs_t *fs, uint8_t *mem, spfs_pixhdr_t *pixhdr) {
  (void)fs;
  bstr8 bs;
  bstr8_init(&bs, mem);
  bstr8_wr(&bs, 32, pixhdr->fi.size);
  bstr8_wr(&bs, 32, pixhdr->fi.x_size);
  spfs_strncpy((char *)&mem[(32+32)/8], (char *)pixhdr->name, SPFS_CFG_FILE_NAME_SZ);
  bstr8_setp(&bs, 32 + 32 + 8 * SPFS_CFG_FILE_NAME_SZ);
  bstr8_wr(&bs, SPFS_PIXHDR_TYPE_BITS, pixhdr->fi.type);
  bstr8_wr(&bs, SPFS_PIXHDR_FLAG_BITS, pixhdr->fi.f_flags);
#if SPFS_CFG_FILE_META_SZ
  spfs_memcpy(&mem[SPFS_PIXHDR_SZ(fs) - SPFS_CFG_FILE_META_SZ],
              pixhdr->meta, SPFS_CFG_FILE_META_SZ);
#endif
}

// returns file size from given packed page index header memory
uint32_t _pixhdr_rdmem_sz(spfs_t *fs, uint8_t *mem) {
  (void)fs;
  bstr8 bs;
  bstr8_init(&bs, mem);
  return bstr8_rd(&bs, 32);
}

// writes file size in given packed page index header memory
void _pixhdr_wrmem_sz(spfs_t *fs, uint8_t *mem, uint32_t sz) {
  (void)fs;
  bstr8 bs;
  bstr8_init(&bs, mem);
  bstr8_wr(&bs, 32, sz);
}

// reads page header from medium for given data page index into given struct
_SPFS_STATIC int _page_hdr_read(spfs_t *fs, pix_t dpix, spfs_phdr_t *phdr, uint32_t rd_flags) {
  int res;
  uint8_t mem[SPFS_PHDR_MAX_SZ];
  uint32_t dpix_addr = SPFS_DPHDR2ADDR(fs, dpix);
  uint32_t hdr_sz = SPFS_PHDR_SZ(fs);
  res = _medium_read(fs, dpix_addr, mem, hdr_sz, SPFS_T_META | rd_flags);
  ERR(res);
  _phdr_rdmem(fs, mem, phdr);
  ERRET(res);
}

// writes page header to medium for given data page index from given struct
_SPFS_STATIC int spfs_page_hdr_write(spfs_t *fs, pix_t dpix, spfs_phdr_t *phdr, uint32_t wr_flags) {
  int res;
  uint8_t mem[SPFS_PHDR_MAX_SZ];
  spfs_memset(mem, 0xff, SPFS_PHDR_MAX_SZ);
  _phdr_wrmem(fs, mem, phdr);

  uint32_t dpix_addr = SPFS_DPHDR2ADDR(fs, dpix);
  uint32_t hdr_sz = SPFS_PHDR_SZ(fs);
  res = _medium_write(fs, dpix_addr, mem, hdr_sz, SPFS_T_META | wr_flags);
  ERRET(res);
}

// reads page index header from medium for given data page index into given struct,
// including page header
_SPFS_STATIC int _page_ixhdr_read(spfs_t *fs, pix_t dpix, spfs_pixhdr_t *pixhdr, uint32_t rd_flags) {
  int res;
  uint8_t mem[SPFS_PIXHDR_MAX_SZ + SPFS_PHDR_MAX_SZ];
  uint32_t dpix_addr = SPFS_DPIXHDR2ADDR(fs, dpix);
  uint32_t ixhdr_sz = SPFS_PIXHDR_SZ(fs) + SPFS_PHDR_SZ(fs);
  res = _medium_read(fs, dpix_addr, mem, ixhdr_sz, SPFS_T_META | rd_flags);
  ERR(res);
  _phdr_rdmem(fs, &mem[SPFS_PIXHDR_SZ(fs)], &pixhdr->phdr);
  _pixhdr_rdmem(fs, mem, pixhdr);
  pixhdr->fi.id = pixhdr->phdr.id;
  ERRET(res);
}

// writes page index header to medium for given data page index from given struct,
// including page header
_SPFS_STATIC int spfs_page_ixhdr_write(spfs_t *fs, pix_t dpix, spfs_pixhdr_t *pixhdr, uint32_t wr_flags) {
  int res;
  uint8_t mem[SPFS_PIXHDR_MAX_SZ + SPFS_PHDR_MAX_SZ];
  spfs_memset(mem, 0xff, sizeof(mem));
  _pixhdr_wrmem(fs, mem, pixhdr);
  _phdr_wrmem(fs, &mem[SPFS_PIXHDR_SZ(fs)], &pixhdr->phdr);

  uint32_t dpix_addr = SPFS_DPIXHDR2ADDR(fs, dpix);
  uint32_t ixhdr_sz = SPFS_PIXHDR_SZ(fs) + SPFS_PHDR_SZ(fs);
  res = _medium_write(fs, dpix_addr, mem, ixhdr_sz, SPFS_T_META | wr_flags);
  ERRET(res);
}

// copies a logical page to another logical page
_SPFS_STATIC int _page_copy(spfs_t *fs, pix_t dst_lpix, pix_t src_lpix, uint8_t only_data) {
  dbg("dstlpix:"_SPIPRIpg" srclpix:"_SPIPRIpg"\n", dst_lpix, src_lpix);
  int res = SPFS_OK;
  uint8_t b[SPFS_CFG_COPY_BUF_SZ];
  uint32_t rem_sz = SPFS_CFG_LPAGE_SZ(fs) -
                    (only_data ? (SPFS_PHDR_SZ(fs) + SPFS_PIXHDR_SZ(fs)) : 0);
  uint32_t saddr = SPFS_LPIX2ADDR(fs, src_lpix);
  uint32_t daddr = SPFS_LPIX2ADDR(fs, dst_lpix);
  while (res == SPFS_OK && rem_sz) {
    uint32_t sz = spfs_min(rem_sz, SPFS_CFG_COPY_BUF_SZ);
    res = _medium_read(fs, saddr, b, sz, 0);
    ERR(res);
    res = _medium_write(fs, daddr, b, sz, 0);
    ERR(res);
    saddr += sz;
    daddr += sz;
    rem_sz -= sz;
  }
  ERRET(res);
}

///////////////////////////////////////////////////////////////////////////////
// scanner operations
///////////////////////////////////////////////////////////////////////////////


// Traverses all lu in given range.
// Uses work1 buffer for reading the lu pages.
// If visit function returns SPFS_VIS_STOP, this function will return SPFS_OK.
// If visitor reaches end page, this function will return -SPFS_ERR_VIS_END.
_SPFS_STATIC int spfs_page_visit(spfs_t *fs, pix_t start_dpix, pix_t end_dpix, void *varg, spfs_visitor_t v,
               uint16_t visit_flags) {
  (void)visit_flags;
  int res = SPFS_OK;
  spfs_assert(start_dpix < (pix_t)SPFS_DPAGES_MAX(fs));
  spfs_assert(end_dpix < (pix_t)SPFS_DPAGES_MAX(fs));
  spfs_vis_info_t info;
  info.dpix = start_dpix;
  info.dbix = -1;
  info.lbix = -1;
  info.lupix = -1;
  do {
    // new block?
    if (SPFS_DPIX2DBLK(fs, info.dpix) != info.dbix) {
      info.dbix = SPFS_DPIX2DBLK(fs, info.dpix);
      info.lbix = _dbix2lbix(fs, info.dbix);
      info.lupix = -1;
    }
    // new lu page?
    if (SPFS_DPIX2BLKLUPIX(fs, info.dpix) != info.lupix) {
      info.lupix = SPFS_DPIX2BLKLUPIX(fs, info.dpix);
      uint32_t addr = SPFS_LBLKLPIX2ADDR(fs, info.lbix, info.lupix);
      uint32_t bhdr_sz = (info.lupix == 0 ? SPFS_BLK_HDR_SZ : 0);
      res = _medium_read(fs, addr + bhdr_sz, fs->run.work1, SPFS_CFG_LPAGE_SZ(fs) - bhdr_sz,
                         SPFS_T_LU);
      ERR(res);
      barr8_init(&fs->run.lu, fs->run.work1, SPFS_LU_BITS(fs));
    }
    // readout the lu entry and callback
    uint32_t ent_lu = barr8_get(&fs->run.lu, SPFS_DPIX2LUENT(fs, info.dpix));
    if (v) res = v(fs, ent_lu, &info, varg);
    if (res == SPFS_VIS_CONT) res = SPFS_OK;
    if (res == SPFS_VIS_CONT_LU_RELOAD) {
      info.dbix = -1; // indicate that we need to load the LU again
      res = SPFS_OK;
    }
    if (res == SPFS_VIS_STOP) break;
    ERR(res);

    // go on
    info.dpix++;
    if (info.dpix >= (pix_t)SPFS_DPAGES_MAX(fs)) {
      info.dpix = 0; // reached end of all data pages, wrap
    }
  } while (res == SPFS_OK && info.dpix != end_dpix);
  if (res == SPFS_OK && info.dpix == end_dpix) res = -SPFS_ERR_VIS_END;
  if (res == SPFS_VIS_STOP) res = SPFS_OK;
  if (res == -SPFS_ERR_VIS_END) {
    return res;
  }
  ERRET(res);
}

typedef struct {
  id_t id_offs;
  id_t bucket_range;
  id_t buckets_cnt;
  barr buckets;
  const char *unique_name;
} _find_free_id_varg_t;
static int _id_find_free_v(spfs_t *fs, uint32_t lu_entry, spfs_vis_info_t *info, void *varg) {
  _find_free_id_varg_t *arg = (_find_free_id_varg_t *)varg;
  id_t id = spfs_signext(lu_entry >> SPFS_LU_FLAG_BITS, SPFS_BITS_ID(fs));
  uint32_t luflags = lu_entry & ((1<<SPFS_LU_FLAG_BITS)-1);
  // only check real ids, with the lu extra flag cleared indicating it is an index
  if (luflags || id == SPFS_IDDELE || id == SPFS_IDFREE || id == SPFS_IDJOUR)
    return SPFS_VIS_CONT;
  id = lu_entry >> SPFS_LU_FLAG_BITS;
  if (id-1 >= arg->id_offs) {
    spfs_pixhdr_t pixhdr;
    int res;
    if (arg->unique_name) {
      res = _page_ixhdr_read(fs, info->dpix, &pixhdr, 0);
    } else {
      res = _page_hdr_read(fs, info->dpix, &pixhdr.phdr, 0);
    }
    ERR(res);
    // check id redundancy
    if (pixhdr.phdr.id != id) {
      dbg("error @ dpix:"_SPIPRIpg", luid:"_SPIPRIid" pgid:"_SPIPRIid"\n", info->dpix, id, pixhdr.phdr.id);
      ERR(-SPFS_ERR_LU_PHDR_ID_MISMATCH);
    }
    // check IDX flag redundancy
    if (pixhdr.phdr.p_flags & SPFS_PHDR_FL_IDX) {
      ERR(-SPFS_ERR_LU_PHDR_FLAG_MISMATCH);
    }
    // make sure the index has span 0, meaning index header
    if (pixhdr.phdr.span) {
      return SPFS_VIS_CONT;
    }

    if (arg->unique_name) {
      if (spfs_strncmp(arg->unique_name, (char *)pixhdr.name, SPFS_CFG_FILE_NAME_SZ) == 0) {
        ERR(-SPFS_ERR_NAME_CONFLICT);
      }
    }

    // add to bucket
    id_t bucket_ix = (id - 1 - arg->id_offs) / arg->bucket_range;
    if (bucket_ix <= arg->buckets_cnt) {
      id_t cnt = barr_get(&arg->buckets, bucket_ix);
      if (cnt < arg->bucket_range) barr_set(&arg->buckets, bucket_ix, cnt + 1);
    }
  }
  return SPFS_VIS_CONT;
}

// Finds free id.
// 1. id_range = all possible ids
// 2. divide work page into X-bit counters, or buckets. X is selected so all
//    ids in id_range will fit
// 3. scan all media for all ids and count up corresponding counter
// 4. if a counter is zero, we have a free id: return id
// 5. else set id_range to ids covered by counter with lowest count, goto 2
_SPFS_STATIC int _id_find_free(spfs_t *fs, id_t *id, const char *unique_name) {
  // Find free id amongst unsorted id range I, having memory M.
  // Divide M into n x-bit bucket counters. Each bucket covers id range
  // i*I/n to (i+1)*I/n-1, where i = {0..n-1}.
  //
  // Number of buffer bits = 8*M.
  //
  // Given x sized bit bucket counters, we have 8*M/x number of counters.
  //    n = 8*M/x, n*x <= 8*M                         (1)
  // These counters must cover all I. Each counter can comprise (2^(x-1))-1 ids.
  //    n*(2^(x-1)-1) >= I, n >= I/(2^(x-1) - 1)      (2)
  //    (1) and (2) =>
  //    I / M <= 8*(2^(x-1) - 1) / x                  (3)
  //
  // Let t(x) = 8*(2^(x-1) - 1) / x. x can only be positive integers from 1 to 32.
  // Table T for ceil(t(x)) for x = {1..32} is below:
  static const uint32_t T[32] = {
      0x0,0x4,0x8,0xe,0x18,0x2a,0x48,0x7f,
      0xe3,0x199,0x2e8,0x555,0x9d8,0x1249,0x2222,0x4000,
      0x7878,0xe38e,0x1af28,0x33333,0x61862,0xba2e9,0x1642c8,0x2aaaab,
      0x51eb85,0x9d89d9,0x12f684c,0x2492492,0x469ee58,0x8888889,0x10842108,
      0x20000000};
  int res = SPFS_OK;

  uint32_t bucket_mem = SPFS_CFG_LPAGE_SZ(fs);
  uint32_t *mem = (uint32_t *)fs->run.work2;
  const id_t max_id = SPFS_MAX_ID(fs);
  id_t id_cnt = max_id;
  _find_free_id_varg_t arg = {.id_offs = 0, .unique_name = unique_name};

  int id_found = 0;
  while (res == SPFS_OK && !id_found) {
    uint32_t bits,bucket;
    for (bits = 0; bits < 32 && T[bits] < id_cnt/bucket_mem; bits++);
    bits++; // T repr bits 1..32 but is indexed 0..31
    arg.buckets_cnt = 8*bucket_mem / bits;
    arg.bucket_range = (1<<bits) - 1;
    spfs_memset(mem, 0x00, SPFS_CFG_LPAGE_SZ(fs));
    barr_init(&arg.buckets, mem, bits);
    dbg("bits:"_SPIPRIi" range:"_SPIPRIi"-"_SPIPRIi" buckets:"_SPIPRIi"\n",
        bits, arg.id_offs, arg.id_offs + arg.bucket_range, arg.buckets_cnt);

    // place all ids in corresponding bucket
    res = spfs_page_visit(fs, 0, 0, &arg, _id_find_free_v, 0);
    if (res == -SPFS_ERR_VIS_END) res = SPFS_OK;
    ERR(res);

    // find bucket which is empty or with least ids
    id_t min_cnt = arg.bucket_range;
    id_t cand_i = (id_t)-1;
    for (bucket = 0; bucket < arg.buckets_cnt; bucket++) {
      id_t cnt = barr_get(&arg.buckets, bucket);
      if (arg.id_offs + bucket * arg.bucket_range >= max_id) break;
      if (arg.id_offs + (bucket+1) * arg.bucket_range > max_id) {
        // this bucket contains out of bounds ids, adjust count by number
        // of invalid ids
        cnt += (arg.id_offs + (bucket+1) * arg.bucket_range) - max_id;
      }
      if (cnt == 0) {
        // found one
        *id = (arg.id_offs + bucket*arg.bucket_range) + 1;
        id_found = 1;
        break;
      } else if (cnt < min_cnt) {
        min_cnt = cnt;
        cand_i = bucket;
      }
    } // check all buckets

    if (!id_found ) {
      if (cand_i == (id_t)-1) {
        // fatal: no candidate found
        break;
      }
      id_t cnt = barr_get(&arg.buckets, cand_i);
      if (cnt == arg.bucket_range) {
        spfs_assert(0);
        // fatal: selected a full counter
        break;
      }
      arg.id_offs += cand_i * arg.bucket_range;
      id_cnt = arg.bucket_range;
    }
    // no need scanning for the unique name more than once
    arg.unique_name = NULL;
  } // while id not found
  if (!id_found) ERR(-SPFS_ERR_OUT_OF_IDS);
  dbg("found id:" _SPIPRIid "\n", *id);
  ERRET(SPFS_OK);
}

typedef struct {
  pix_t free_dpix;
} _page_find_free_varg_t;
static int _page_find_free_v(spfs_t *fs, uint32_t lu_entry, spfs_vis_info_t *info, void *varg) {
  id_t id = spfs_signext(lu_entry >> SPFS_LU_FLAG_BITS, SPFS_BITS_ID(fs));
  if (id == SPFS_IDFREE) {
    uint32_t rix;
    for (rix = 0; rix < SPFS_PFREE_RESV; rix++) {
      if (fs->run.resv.arr[rix] == info->dpix) return SPFS_VIS_CONT;
    }
    _page_find_free_varg_t *arg = (_page_find_free_varg_t *)varg;
    arg->free_dpix = info->dpix;
    return SPFS_VIS_STOP;
  }
  return SPFS_VIS_CONT;
}
// finds a free page
_SPFS_STATIC int _page_find_free(spfs_t *fs, pix_t *dpix) {
  int res = SPFS_OK;
  _page_find_free_varg_t arg;
  res = spfs_page_visit(fs, fs->run.dpix_free_page_cursor, fs->run.dpix_free_page_cursor, &arg,
                   _page_find_free_v, 0);
  if (res == -SPFS_ERR_VIS_END) res = -SPFS_ERR_OUT_OF_PAGES;
  ERR(res);
  dbg("found, cursor@"_SPIPRIpg" dpix:" _SPIPRIpg "\n", fs->run.dpix_free_page_cursor, arg.free_dpix);
  if (dpix) *dpix = arg.free_dpix;
  fs->run.dpix_free_page_cursor = arg.free_dpix;
  return SPFS_OK;
}

// allocates a free page with given id, returns the data page index in dpix
_SPFS_STATIC int _page_allocate_free(spfs_t *fs, pix_t *dpix, id_t id, uint8_t lu_flag) {
  spfs_assert(dpix);
  int res = _page_find_free(fs, dpix);
  ERR(res);
  res = _lu_page_allocate(fs, *dpix, id, lu_flag);
  ERRET(res);
}

typedef struct {
  id_t id;
  spix_t span;
  uint8_t find_flags;
  pix_t dpix;
} _page_find_varg_t;
static int _page_find_v(spfs_t *fs, uint32_t lu_entry, spfs_vis_info_t *info, void *varg) {
  _page_find_varg_t *arg = (_page_find_varg_t *)varg;
  id_t special_id = spfs_signext(lu_entry >> SPFS_LU_FLAG_BITS, SPFS_BITS_ID(fs));
  if (special_id == SPFS_IDFREE || special_id == SPFS_IDDELE) return SPFS_VIS_CONT;
  id_t id = lu_entry >> SPFS_LU_FLAG_BITS;
  uint32_t luflags = lu_entry & ((1<<SPFS_LU_FLAG_BITS)-1);
  if (id == arg->id) {
    if ((arg->find_flags & SPFS_PAGE_FIND_FL_IX) && luflags) return SPFS_VIS_CONT;
    spfs_phdr_t phdr;
    int res = _page_hdr_read(fs, info->dpix, &phdr, 0);
    ERR(res);
    // check id redundancy
    if (phdr.id != id) {
      dbg("error @ dpix:"_SPIPRIpg", luid:"_SPIPRIid" pgid:"_SPIPRIid"\n", info->dpix, id, phdr.id);
      ERR(-SPFS_ERR_LU_PHDR_ID_MISMATCH);
    }
    // check IDX flag redundancy
    if ((arg->find_flags & SPFS_PAGE_FIND_FL_IX)
        && (phdr.p_flags & SPFS_PHDR_FL_IDX)) {
      ERR(-SPFS_ERR_LU_PHDR_FLAG_MISMATCH);
    }
    if (phdr.span == arg->span) {
      arg->dpix = info->dpix;
      return SPFS_VIS_STOP;
    }
  }
  return SPFS_VIS_CONT;
}
// finds a page with given id and span index, ixhdr or not, returns the data page index in dpix
_SPFS_STATIC int spfs_page_find(spfs_t *fs, id_t id, spix_t span, uint8_t find_flags, pix_t *dpix) {
  int res = SPFS_OK;
  _page_find_varg_t arg = {.id = id, .span = span, .find_flags = find_flags};
  res = spfs_page_visit(fs, fs->run.dpix_find_cursor, fs->run.dpix_find_cursor, &arg,
                   _page_find_v, 0);
  dbg("id:" _SPIPRIid " span:" _SPIPRIsp " flags:" _SPIPRIfl " cursor@"_SPIPRIpg" found@"_SPIPRIpg"\n",
      id, span, find_flags, fs->run.dpix_find_cursor, arg.dpix);
  if (res == -SPFS_ERR_VIS_END) res = -SPFS_ERR_PAGE_NOT_FOUND;
  ERR(res);
  if (dpix) *dpix = arg.dpix;
  fs->run.dpix_find_cursor = arg.dpix;
  return SPFS_OK;
}

// reserves a free page, returns a handle to reserved page
// this free page is physically free, but will not be found by find_free
// as long as it is reserved
_SPFS_STATIC int _resv_alloc(spfs_t *fs) {
  if (fs->run.pdele + fs->run.pfree - fs->run.resv.ptaken == 0)
    ERR(-SPFS_ERR_OUT_OF_PAGES);
  uint8_t rix;
  for (rix = 0; rix < SPFS_PFREE_RESV; rix++) {
    if (fs->run.resv.arr[rix] == (pix_t)-1) {
      break;
    }
  }
  spfs_assert(rix != SPFS_PFREE_RESV);

  if (fs->run.pfree == 0) {
    // TODO GC
  }

  pix_t free_dpix;
  int res = _page_find_free(fs, &free_dpix);
  ERR(res);
  dbg("dpix:"_SPIPRIpg" resv @ "_SPIPRIi"\n", free_dpix, rix);
  fs->run.resv.arr[rix] = free_dpix;
  fs->run.resv.ptaken++;
  return (int)rix;
}

// unreserves a free page given handle, returns the data page index or error
// the page is now found by find_free
_SPFS_STATIC int _resv_free(spfs_t *fs, uint8_t rix) {
  spfs_assert(rix < SPFS_PFREE_RESV);
  int dpix = -SPFS_ERR_FREE_PAGE_NOT_RESERVED;
  if (fs->run.resv.arr[rix] != (pix_t)-1) {
    dpix = (int)fs->run.resv.arr[rix];
    fs->run.resv.arr[rix] = (pix_t)-1;
    fs->run.resv.ptaken--;
  }
  return dpix;
}

// bitmanio implementation

#define BITMANIO_STORAGE_BITS 32
#include "bitmanio.h"
#define BITMANIO_STORAGE_BITS BYTE
#include "bitmanio.h"
