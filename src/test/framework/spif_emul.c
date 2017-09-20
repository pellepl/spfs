/*
 * spif_emul.c
 *
 *  Created on: Aug 8, 2017
 *      Author: petera
 */

#include "spif_emul.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifndef SPIF_MAX_INST
#define SPIF_MAX_INST                   (4)
#endif

#define SPIF_EM_BLK_SZ_MASK_MIN_BITS    (8) // SPIF_EM_BLK_SZ_MASK_256

typedef struct {
  char resv;
  spif_em_cfg cfg;
  uint32_t blocks;
  uint8_t *buf;
  uint32_t *era_cnts;
  spif_time trd;
  spif_time twr;
  spif_time ter;
  uint32_t abort_rd;
  uint32_t abort_wr;
  uint32_t abort_er;
  uint32_t abort_any;
  uint32_t err_addr;
} spif_emul;

static spif_emul _hdl[SPIF_MAX_INST];

static int _ctz(uint32_t x) {
  int z = 0;
  while ((x & 1) == 0) {
    z++;
    x>>=1;
  }
  return z;
}

static int _clz(uint32_t x) {
  int z = 0;
  while ((x & 0x80000000) == 0) {
    z++;
    x<<=1;
  }
  return z;
}

static uint32_t _cz(const uint8_t *buf, int len) {
  uint32_t z = 0;
  while (len--) {
    z += 8-((*buf++ * 0x200040008001ULL & 0x111111111111111ULL) % 0xf);
  }
  return z;
}

static uint32_t _co(uint8_t v) {
  return ((v * 0x200040008001ULL & 0x111111111111111ULL) % 0xf);
}

uint16_t spif_em_get_blockmask(uint32_t blocksize) {
  uint8_t i;
  for (i = 0; i < 16; i++) {
    if (blocksize == (uint32_t)(1<<(i+SPIF_EM_BLK_SZ_MASK_MIN_BITS)))
      return 1<<i;
  }
  return 0;
}

int spif_em_create(spif_em_cfg *cfg) {
  if (cfg->blk_sz_mask == 0) return -ESPIF_EM_CFG_NO_BLOCK; // no block size defined
  // get min blk sz mask
  uint32_t min_blk_sz = 1 << (_ctz(cfg->blk_sz_mask) + SPIF_EM_BLK_SZ_MASK_MIN_BITS);
  uint32_t min_blk_sz_mask = min_blk_sz - 1;
  uint32_t max_blk_sz = 1 << (31 - _clz(cfg->blk_sz_mask) + SPIF_EM_BLK_SZ_MASK_MIN_BITS);
  uint32_t max_blk_sz_mask = max_blk_sz - 1;
  if (cfg->size & min_blk_sz_mask) return -ESPIF_EM_CFG_SIZE; // size not even with min block size
  if (cfg->size & max_blk_sz_mask) return -ESPIF_EM_CFG_SIZE; // size not even with max block size
  if (cfg->size < max_blk_sz) return -ESPIF_EM_CFG_SIZE; // size less than max block size

  int hdl = -1;
  int i;
  for (i = 0; i < SPIF_MAX_INST; i++) {
    if (!_hdl[i].resv) {
      hdl = i;
      break;
    }
  }
  if (hdl < 0) return -ENFILE; // no free hdl

  spif_emul *sf = &_hdl[hdl];

  memset(sf, 0, sizeof(spif_emul));
  memcpy(&sf->cfg, cfg, sizeof(spif_em_cfg));

  if (sf->cfg.page_size == 0) sf->cfg.page_size = 1; // default
  if (min_blk_sz % sf->cfg.page_size || sf->cfg.page_size > min_blk_sz)
    return -ESPIF_EM_CFG_PAGE_ALIGN;

  sf->buf = malloc(sf->cfg.size);
  if (!sf->buf) return -ENOMEM;
  sf->era_cnts = malloc(sizeof(uint32_t) * sf->cfg.size / min_blk_sz);
  if (!sf->era_cnts) {
    free(sf->buf);
    return -ENOMEM;
  }

  memset(sf->buf, 0xff, sf->cfg.size);
  memset(sf->era_cnts, 0x00, sizeof(uint32_t) * sf->cfg.size / min_blk_sz);

  sf->blocks = sf->cfg.size / min_blk_sz;
  sf->err_addr = -1;
  sf->resv = 1;
  return hdl;
}

int spif_em_read(int spif_emul_hdl, uint32_t addr, uint8_t *buf, uint32_t len, uint32_t rd_flags) {
  if (spif_emul_hdl < 0 || spif_emul_hdl >= SPIF_MAX_INST || !_hdl[spif_emul_hdl].resv)
    return -EINVAL;
  spif_emul *sf = &_hdl[spif_emul_hdl];
  if (addr+len >= sf->cfg.size) {
    sf->err_addr = addr+len;
    return -ESPIF_EM_IO_ADDR_OOB;
  }
  char abort = (rd_flags & SPIF_EM_FL_IO_NO_ABORT) == 0;
  int res = 0;

  if (abort) {
    if (sf->abort_any > 0) {
      if (len > sf->abort_any) len = sf->abort_any;
      sf->abort_any -= len;
      if (sf->abort_any == 0) {
        sf->err_addr = addr + len;
        res = -ESPIF_EM_IO_USER_ABORT;
      }
    }
    if (sf->abort_rd > 0) {
      if (len > sf->abort_rd) len = sf->abort_rd;
      sf->abort_rd -= len;
      if (sf->abort_rd == 0) {
        sf->err_addr = addr + len;
        res = -ESPIF_EM_IO_USER_ABORT;
      }
    }
  }

  memcpy(buf, &sf->buf[addr], len);
  if ((rd_flags & SPIF_EM_FL_IO_NO_TIMING) == 0) {
    sf->trd += sf->cfg.read_timings.setup + sf->cfg.read_timings.per_byte * len;
  }

  return res;
}

int spif_em_write(int spif_emul_hdl, uint32_t addr, const uint8_t *buf, uint32_t len, uint32_t wr_flags) {
  if (spif_emul_hdl < 0 || spif_emul_hdl >= SPIF_MAX_INST || !_hdl[spif_emul_hdl].resv)
    return -EINVAL;
  spif_emul *sf = &_hdl[spif_emul_hdl];
  if (addr+len >= sf->cfg.size) {
    sf->err_addr = addr + len;
    return -ESPIF_EM_IO_ADDR_OOB;
  }

  uint32_t page_size = sf->cfg.page_size;
  char freecheck = (wr_flags & SPIF_EM_FL_WR_NO_FREECHECK) == 0;
  char bitcheck = (wr_flags & SPIF_EM_FL_WR_NO_BITCHECK) == 0;
  char pagewrap = (wr_flags & SPIF_EM_FL_WR_PAGE_WRAP) != 0;
  char failpagewrap = (wr_flags & SPIF_EM_FL_WR_PAGE_WRAP_FAIL) != 0;
  char datawriteand = (wr_flags & SPIF_EM_FL_WR_OR) == 0;
  char datawriteset = (wr_flags & SPIF_EM_FL_WR_SET) == 0;
  char timing = (wr_flags & SPIF_EM_FL_IO_NO_TIMING) == 0;
  char abort = (wr_flags & SPIF_EM_FL_IO_NO_ABORT) == 0;
  uint32_t page_addr = (addr / page_size) * page_size;
  if (failpagewrap && addr + len >= page_addr + page_size) {
    sf->err_addr = addr;
    return -ESPIF_EM_WR_PAGE_WRAP;
  }
  if (timing) sf->twr += sf->cfg.write_timings.setup;
  while (len--) {
    if (abort) {
      if (sf->abort_any > 0) {
        sf->abort_any--;
        if (sf->abort_any == 0) {
          sf->err_addr = addr;
          return -ESPIF_EM_IO_USER_ABORT;
        }
      }
      if (sf->abort_wr > 0) {
        sf->abort_wr--;
        if (sf->abort_wr == 0) {
          sf->err_addr = addr;
          return -ESPIF_EM_IO_USER_ABORT;
        }
      }
    }
    uint8_t s = *buf++;
    uint8_t d = sf->buf[addr];
    // check against writing to written data
    if (freecheck && d != 0xff) {
      sf->err_addr = addr;
      return -ESPIF_EM_WR_NOT_FREE;
    }
    // check against trying to pull zeroes to ones
    if (bitcheck && (~d & s)) {
      sf->err_addr = addr;
      return -ESPIF_EM_WR_ZERO_BIT_TO_ONE;
    }
    if (timing) {
      spif_time bits_needing_zeroing_frac = (spif_time)_co(~s & d) / (spif_time)8;
      sf->twr += sf->cfg.write_timings.per_byte_min +
          bits_needing_zeroing_frac * (sf->cfg.write_timings.per_byte_max - sf->cfg.write_timings.per_byte_min);
    }
    if (datawriteand) {
      sf->buf[addr] = d & s;
    } else if (datawriteset) {
      sf->buf[addr] = d;
    } else {
      sf->buf[addr] = d | s;
    }
    addr++;
    if (pagewrap && addr >= page_addr + page_size) {
      addr = page_addr;
    }
  }
  return 0;
}

int spif_em_erase(int spif_emul_hdl, uint32_t addr, uint32_t len, uint32_t er_flags) {
  if (spif_emul_hdl < 0 || spif_emul_hdl >= SPIF_MAX_INST || !_hdl[spif_emul_hdl].resv)
    return -EINVAL;
  spif_emul *sf = &_hdl[spif_emul_hdl];
  if (addr+len > sf->cfg.size) {
    sf->err_addr = addr + len;
    return -ESPIF_EM_IO_ADDR_OOB;
  }
  // find if block sz is represented
  uint8_t blk_sz_ix;
  for (blk_sz_ix = 0; blk_sz_ix < 16; blk_sz_ix++) {
    if (sf->cfg.blk_sz_mask & (1<<blk_sz_ix)) {
      uint32_t blk_sz = 1<<(blk_sz_ix + SPIF_EM_BLK_SZ_MASK_MIN_BITS);
      if (blk_sz == len) break;
    }
  }
  if (blk_sz_ix == 16) {
    sf->err_addr = addr;
    return -ESPIF_EM_ER_BLOCK_SZ;
  }
  // check that addr is a boundary of block size
  if (addr & (len-1)) {
    sf->err_addr = addr;
    return -ESPIF_EM_ER_ADDR_ALIGN;
  }

  char abort = (er_flags & SPIF_EM_FL_IO_NO_ABORT) == 0;

  uint32_t min_blk_sz = 1 << (_ctz(sf->cfg.blk_sz_mask) + SPIF_EM_BLK_SZ_MASK_MIN_BITS);

  if ((er_flags & SPIF_EM_FL_IO_NO_TIMING) == 0) {
    sf->ter += sf->cfg.erase_timings[blk_sz_ix].min;
    spif_time bits_needing_pulling_frac =
        (spif_time)_cz(&sf->buf[addr], len) / (spif_time)(8*len);
    sf->ter += (sf->cfg.erase_timings[blk_sz_ix].max - sf->cfg.erase_timings[blk_sz_ix].min) *
        bits_needing_pulling_frac;
  }

  for (blk_sz_ix = 0; blk_sz_ix < len/min_blk_sz; blk_sz_ix++) {
    sf->era_cnts[blk_sz_ix + addr/min_blk_sz]++;
    uint32_t j;
    for (j = 0; j < min_blk_sz; j++) {
      uint32_t paddr = addr + blk_sz_ix * min_blk_sz + j;
      if (abort) {
        if (sf->abort_any > 0) {
          sf->abort_any--;
          if (sf->abort_any == 0) {
            sf->err_addr = paddr;
            return -ESPIF_EM_IO_USER_ABORT;
          }
        }
        if (sf->abort_er > 0) {
          sf->abort_er--;
          if (sf->abort_er == 0) {
            sf->err_addr = paddr;
            return -ESPIF_EM_IO_USER_ABORT;
          }
        }
      }
      sf->buf[paddr] = 0xff;
    }
  }
  return 0;
}

int spif_em_masserase(int spif_emul_hdl, uint32_t er_flags) {
  if (spif_emul_hdl < 0 || spif_emul_hdl >= SPIF_MAX_INST || !_hdl[spif_emul_hdl].resv)
    return -EINVAL;
  spif_emul *sf = &_hdl[spif_emul_hdl];

  if ((er_flags & SPIF_EM_FL_IO_NO_TIMING) == 0) {
    sf->ter += sf->cfg.chip_erase_timings.min;
    spif_time bits_needing_pulling_frac =
        (spif_time)_cz(sf->buf, sf->cfg.size) / (spif_time)(8*sf->cfg.size);
    sf->ter += (sf->cfg.chip_erase_timings.max - sf->cfg.chip_erase_timings.min) *
        bits_needing_pulling_frac;
  }

  uint32_t addr = 0;
  uint32_t len = 1 << (_ctz(sf->cfg.blk_sz_mask) + SPIF_EM_BLK_SZ_MASK_MIN_BITS);
  uint32_t i;
  for (i = 0; i < sf->blocks; i++) {
    (void)spif_em_erase(spif_emul_hdl, addr, len, er_flags | SPIF_EM_FL_IO_NO_TIMING);
    addr += len;
  }
  return 0;
}

int spif_em_dbg_set_buffer(int spif_emul_hdl, uint32_t addr, const uint8_t *buf, uint32_t len) {
  if (spif_emul_hdl < 0 || spif_emul_hdl >= SPIF_MAX_INST || !_hdl[spif_emul_hdl].resv)
    return -EINVAL;
  spif_emul *sf = &_hdl[spif_emul_hdl];
  if (addr+len >= sf->cfg.size) return -ESPIF_EM_IO_ADDR_OOB;
  memcpy(&sf->buf[addr], buf, len);
  return 0;
}

int spif_em_dbg_get_buffer(int spif_emul_hdl, uint8_t **buf) {
  if (spif_emul_hdl < 0 || spif_emul_hdl >= SPIF_MAX_INST || !_hdl[spif_emul_hdl].resv ||
      !buf)
    return -EINVAL;
  spif_emul *sf = &_hdl[spif_emul_hdl];
  *buf = sf->buf;
  return 0;
}

int spif_em_dbg_abort_rd(int spif_emul_hdl, uint32_t after_bytes) {
  if (spif_emul_hdl < 0 || spif_emul_hdl >= SPIF_MAX_INST || !_hdl[spif_emul_hdl].resv)
    return -EINVAL;
  spif_emul *sf = &_hdl[spif_emul_hdl];
  sf->abort_rd = after_bytes;
  return 0;
}

int spif_em_dbg_abort_wr(int spif_emul_hdl, uint32_t after_bytes) {
  if (spif_emul_hdl < 0 || spif_emul_hdl >= SPIF_MAX_INST || !_hdl[spif_emul_hdl].resv)
    return -EINVAL;
  spif_emul *sf = &_hdl[spif_emul_hdl];
  sf->abort_wr = after_bytes;
  return 0;
}

int spif_em_dbg_abort_er(int spif_emul_hdl, uint32_t after_bytes) {
  if (spif_emul_hdl < 0 || spif_emul_hdl >= SPIF_MAX_INST || !_hdl[spif_emul_hdl].resv)
    return -EINVAL;
  spif_emul *sf = &_hdl[spif_emul_hdl];
  sf->abort_er = after_bytes;
  return 0;
}

int spif_em_dbg_abort_any(int spif_emul_hdl, uint32_t after_bytes) {
  if (spif_emul_hdl < 0 || spif_emul_hdl >= SPIF_MAX_INST || !_hdl[spif_emul_hdl].resv)
    return -EINVAL;
  spif_emul *sf = &_hdl[spif_emul_hdl];
  sf->abort_any = after_bytes;
  return 0;
}

spif_time spif_em_dbg_get_accumulated_time(int spif_emul_hdl, spif_em_time_type time) {
  if (spif_emul_hdl < 0 || spif_emul_hdl >= SPIF_MAX_INST || !_hdl[spif_emul_hdl].resv)
    return -1;
  spif_emul *sf = &_hdl[spif_emul_hdl];
  switch (time) {
  case SPIF_EM_TIME_READ:
    return sf->trd;
  case SPIF_EM_TIME_WRITE:
    return sf->twr;
  case SPIF_EM_TIME_ERASE:
    return sf->ter;
  case SPIF_EM_TIME_ALL:
    return sf->trd + sf->twr + sf->ter;
  default:
    return -1;
  }
}

void spif_em_dbg_reset_accumulated_time(int spif_emul_hdl, spif_em_time_type time) {
  if (spif_emul_hdl < 0 || spif_emul_hdl >= SPIF_MAX_INST || !_hdl[spif_emul_hdl].resv)
    return;
  spif_emul *sf = &_hdl[spif_emul_hdl];
  switch (time) {
  case SPIF_EM_TIME_READ:
    sf->trd = 0;
    break;
  case SPIF_EM_TIME_WRITE:
    sf->twr = 0;
    break;
  case SPIF_EM_TIME_ERASE:
    sf->ter = 0;
    break;
  case SPIF_EM_TIME_ALL:
    sf->trd = 0;
    sf->twr = 0;
    sf->ter = 0;
    break;
  default:
    break;
  }
}

uint32_t spif_em_dbg_get_err_addr(int spif_emul_hdl) {
  if (spif_emul_hdl < 0 || spif_emul_hdl >= SPIF_MAX_INST || !_hdl[spif_emul_hdl].resv)
    return -1;
  spif_emul *sf = &_hdl[spif_emul_hdl];
  return sf->err_addr;
}

uint32_t spif_em_dbg_get_block_count(int spif_emul_hdl) {
  if (spif_emul_hdl < 0 || spif_emul_hdl >= SPIF_MAX_INST || !_hdl[spif_emul_hdl].resv)
    return -1;
  spif_emul *sf = &_hdl[spif_emul_hdl];
  return sf->blocks;
}

uint32_t spif_em_dbg_get_block_erase_count(int spif_emul_hdl, uint32_t block) {
  if (spif_emul_hdl < 0 || spif_emul_hdl >= SPIF_MAX_INST || !_hdl[spif_emul_hdl].resv)
    return -1;
  spif_emul *sf = &_hdl[spif_emul_hdl];
  if (block >= sf->blocks) return -1;
  return sf->era_cnts[block];
}

int spif_em_dbg_reset_block_erase_count(int spif_emul_hdl) {
  if (spif_emul_hdl < 0 || spif_emul_hdl >= SPIF_MAX_INST || !_hdl[spif_emul_hdl].resv)
    return -EINVAL;
  spif_emul *sf = &_hdl[spif_emul_hdl];
  uint32_t i;
  for (i = 0; i < sf->blocks; i++) {
    sf->era_cnts[i] = 0;
  }
  return 0;
}


int spif_em_destroy(int spif_emul_hdl) {
  if (spif_emul_hdl < 0 || spif_emul_hdl >= SPIF_MAX_INST || !_hdl[spif_emul_hdl].resv)
    return -EINVAL;
  spif_emul *sf = &_hdl[spif_emul_hdl];
  free(sf->era_cnts);
  free(sf->buf);
  sf->resv = 0;
  return 0;
}

void spif_em_destroy_all(void) {
  int i;
  for (i = 0; i < SPIF_MAX_INST; i++) {
    (void)spif_em_destroy(i);
  }
}

#define _ERRSTR(x, y) case x: return #x": "y

const char *spif_em_strerr(int err) {
  if (err < 0) err = -err;
  switch (err) {
  _ERRSTR(ESPIF_EM_CFG_SIZE,        "bad size set in config, either unaligned or too small");
  _ERRSTR(ESPIF_EM_CFG_NO_BLOCK,    "no block mask set in config");
  _ERRSTR(ESPIF_EM_CFG_PAGE_ALIGN,  "given data page is not aligned with block sizes");
  _ERRSTR(ESPIF_EM_IO_ADDR_OOB,     "address out of bounds");
  _ERRSTR(ESPIF_EM_WR_NOT_FREE,     "writing to a byte already written");
  _ERRSTR(ESPIF_EM_WR_ZERO_BIT_TO_ONE, "writing and trying to pull zeroes to ones");
  _ERRSTR(ESPIF_EM_WR_PAGE_WRAP,    "writing where a page wrap occurred");
  _ERRSTR(ESPIF_EM_ER_BLOCK_SZ,     "erasing with a nonexisting block size");
  _ERRSTR(ESPIF_EM_ER_ADDR_ALIGN,   "erasing where address is not aligned with block size");
  _ERRSTR(ESPIF_EM_IO_USER_ABORT,   "requested user abort");
  case 0: return "OK";
  default: return "ESPIF_EM_???";
  }
}
