/*
 * spif_emul.h
 *
 *  Created on: Aug 8, 2017
 *      Author: petera
 */

#ifndef _SPIF_EMUL_H_
#define _SPIF_EMUL_H_

#include <stdint.h>

#define SPIF_EM_BLK_SZ_256        (0)
#define SPIF_EM_BLK_SZ_512        (1)
#define SPIF_EM_BLK_SZ_1K         (2)
#define SPIF_EM_BLK_SZ_2K         (3)
#define SPIF_EM_BLK_SZ_4K         (4)
#define SPIF_EM_BLK_SZ_8K         (5)
#define SPIF_EM_BLK_SZ_16K        (6)
#define SPIF_EM_BLK_SZ_32K        (7)
#define SPIF_EM_BLK_SZ_64K        (8)
#define SPIF_EM_BLK_SZ_128K       (9)
#define SPIF_EM_BLK_SZ_256K       (10)
#define SPIF_EM_BLK_SZ_512K       (11)
#define SPIF_EM_BLK_SZ_1M         (12)
#define SPIF_EM_BLK_SZ_2M         (13)
#define SPIF_EM_BLK_SZ_4M         (14)
#define SPIF_EM_BLK_SZ_8M         (15)

#define SPIF_EM_BLK_SZ_MASK_256   (1<<SPIF_EM_BLK_SZ_256)
#define SPIF_EM_BLK_SZ_MASK_512   (1<<SPIF_EM_BLK_SZ_512)
#define SPIF_EM_BLK_SZ_MASK_1K    (1<<SPIF_EM_BLK_SZ_1K)
#define SPIF_EM_BLK_SZ_MASK_2K    (1<<SPIF_EM_BLK_SZ_2K)
#define SPIF_EM_BLK_SZ_MASK_4K    (1<<SPIF_EM_BLK_SZ_4K)
#define SPIF_EM_BLK_SZ_MASK_8K    (1<<SPIF_EM_BLK_SZ_8K)
#define SPIF_EM_BLK_SZ_MASK_16K   (1<<SPIF_EM_BLK_SZ_16K)
#define SPIF_EM_BLK_SZ_MASK_32K   (1<<SPIF_EM_BLK_SZ_32K)
#define SPIF_EM_BLK_SZ_MASK_64K   (1<<SPIF_EM_BLK_SZ_64K)
#define SPIF_EM_BLK_SZ_MASK_128K  (1<<SPIF_EM_BLK_SZ_128K)
#define SPIF_EM_BLK_SZ_MASK_256K  (1<<SPIF_EM_BLK_SZ_256K)
#define SPIF_EM_BLK_SZ_MASK_512K  (1<<SPIF_EM_BLK_SZ_512K)
#define SPIF_EM_BLK_SZ_MASK_1M    (1<<SPIF_EM_BLK_SZ_1M)
#define SPIF_EM_BLK_SZ_MASK_2M    (1<<SPIF_EM_BLK_SZ_2M)
#define SPIF_EM_BLK_SZ_MASK_4M    (1<<SPIF_EM_BLK_SZ_4M)
#define SPIF_EM_BLK_SZ_MASK_8M    (1<<SPIF_EM_BLK_SZ_8M)

/* write: do not fail when writing to data being other than 0xff */
#define SPIF_EM_FL_WR_NO_FREECHECK    (1<<0)
/* write: do not file when trying to pull zeroes to ones */
#define SPIF_EM_FL_WR_NO_BITCHECK     (1<<1)
/* write: emulate page wrap */
#define SPIF_EM_FL_WR_PAGE_WRAP       (1<<2)
/* write: do not allow page wraps */
#define SPIF_EM_FL_WR_PAGE_WRAP_FAIL  (1<<3)
/* write: OR the data instead of AND */
#define SPIF_EM_FL_WR_OR              (1<<4)
/* write: set the data instead of AND */
#define SPIF_EM_FL_WR_SET             (1<<5)
/* do not add to timings for this operation */
#define SPIF_EM_FL_IO_NO_TIMING       (1<<6)
/* do not count these bytes into user abort */
#define SPIF_EM_FL_IO_NO_ABORT        (1<<7)

#ifndef ESPIF_EM_BASE
#define ESPIF_EM_BASE                 0x1000
#endif
#define ESPIF_EM_CFG_SIZE             (1+ESPIF_EM_BASE)
#define ESPIF_EM_CFG_NO_BLOCK         (2+ESPIF_EM_BASE)
#define ESPIF_EM_CFG_PAGE_ALIGN       (3+ESPIF_EM_BASE)
#define ESPIF_EM_IO_ADDR_OOB          (4+ESPIF_EM_BASE)
#define ESPIF_EM_WR_NOT_FREE          (5+ESPIF_EM_BASE)
#define ESPIF_EM_WR_ZERO_BIT_TO_ONE   (6+ESPIF_EM_BASE)
#define ESPIF_EM_WR_PAGE_WRAP         (7+ESPIF_EM_BASE)
#define ESPIF_EM_ER_BLOCK_SZ          (8+ESPIF_EM_BASE)
#define ESPIF_EM_ER_ADDR_ALIGN        (9+ESPIF_EM_BASE)
#define ESPIF_EM_IO_USER_ABORT        (10+ESPIF_EM_BASE)

#ifndef spif_time
#define spif_time double
#endif

typedef struct {
  uint32_t size;        /* total size of spi flash */
  uint16_t blk_sz_mask; /* or:ed combinations of SPIF_EM_BLK_SZ_MASK_ */
  uint16_t page_size;   /* may be zero */
  struct {
    spif_time setup;    /* read operation setup time */
    spif_time per_byte; /* read operation time per byte*/
  } read_timings;
  struct {
    spif_time setup;        /* write operation setup time */
    spif_time per_byte_min; /* mininum write operation byte time */
    spif_time per_byte_max; /* maximum write operation byte time */
  } write_timings;
  struct {
    spif_time min;      /* mininum erase operation time */
    spif_time max;      /* maximum erase operation time */
  } erase_timings[16]; /* per SPIF_EM_BLK_SZ_ */
  struct {
    spif_time min;      /* mininum chip erase operation time */
    spif_time max;      /* maximum chip erase operation time */
  } chip_erase_timings;
} spif_em_cfg;

typedef enum {
  SPIF_EM_TIME_READ = 0,
  SPIF_EM_TIME_WRITE,
  SPIF_EM_TIME_ERASE,
  SPIF_EM_TIME_ALL
} spif_em_time_type;

uint16_t spif_em_get_blockmask(uint32_t blocksize);

/**
 * Creates a spi flash emulations using given config.
 * Returns handle.
 */
int spif_em_create(spif_em_cfg *cfg);

/**
 * Emulates a spi flash read operation.
 * Returns error code or 0.
 */
int spif_em_read(int spif_emul_hdl, uint32_t addr, uint8_t *buf, uint32_t len, uint32_t rd_flags);

/**
 * Emulates a spi flash write operation.
 * Returns error code or 0.
 */
int spif_em_write(int spif_emul_hdl, uint32_t addr, const uint8_t *buf, uint32_t len, uint32_t wr_flags);

/**
 * Emulates a spi flash erase operation.
 * Returns error code or 0.
 */
int spif_em_erase(int spif_emul_hdl, uint32_t addr, uint32_t len, uint32_t er_flags);

/**
 * Emulates a spi flash mass erase operation.
 * Returns error code or 0.
 */
int spif_em_masserase(int spif_emul_hdl, uint32_t er_flags);

/**
 * Directly manipulates memory buffer representing spi flash contents.
 * Returns error code or 0.
 */
int spif_em_dbg_set_buffer(int spif_emul_hdl, uint32_t addr, const uint8_t *buf, uint32_t len);

/**
 * Sets given pointer to memory buffer representing spi flash contents.
 * Returns error code or 0.
 */
int spif_em_dbg_get_buffer(int spif_emul_hdl, uint8_t **buf);

/**
 * If set to other than 0, a spi flash operation will fail after given number of
 * read bytes.
 * Returns error code or 0.
 */
int spif_em_dbg_abort_rd(int spif_emul_hdl, uint32_t after_bytes);

/**
 * If set to other than 0, a spi flash operation will fail after given number of
 * written bytes.
 * Returns error code or 0.
 */
int spif_em_dbg_abort_wr(int spif_emul_hdl, uint32_t after_bytes);

/**
 * If set to other than 0, a spi flash operation will fail after given number of
 * erased bytes.
 * Returns error code or 0.
 */
int spif_em_dbg_abort_er(int spif_emul_hdl, uint32_t after_bytes);

/**
 * If set to other than 0, a spi flash operation will fail after given number of
 * read/write/erase accesses to spi flash bytes.
 * Returns error code or 0.
 */
int spif_em_dbg_abort_any(int spif_emul_hdl, uint32_t after_bytes);

/**
 * Returns accumulated time for given operation type.
 */
spif_time spif_em_dbg_get_accumulated_time(int spif_emul_hdl, spif_em_time_type time);

/**
 * Resets accumulated time for given operation type.
 */
void spif_em_dbg_reset_accumulated_time(int spif_emul_hdl, spif_em_time_type time);

/**
 * Returns address of last failed operation.
 */
uint32_t spif_em_dbg_get_err_addr(int spif_emul_hdl);

/**
 * Returns number of blocks.
 */
uint32_t spif_em_dbg_get_block_count(int spif_emul_hdl);

/**
 * Returns erase count for given block..
 */
uint32_t spif_em_dbg_get_block_erase_count(int spif_emul_hdl, uint32_t block);

/**
 * Resets erase count for all blocks.
 */
int spif_em_dbg_reset_block_erase_count(int spif_emul_hdl);

/**
 * Cleans up and releases resources for given spi flash.
 */
int spif_em_destroy(int spif_emul_hdl);

/**
 * Cleans up and releases resources for all emulated spi flashes.
 */
void spif_em_destroy_all(void);

/**
 * Returns a string representation of spi flash emulation error code.
 */
const char *spif_em_strerr(int err);

#endif /* _SPIF_EMUL_H_ */
