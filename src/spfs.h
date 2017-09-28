/*
 * spfs.h
 *
 *  Created on: Aug 9, 2017
 *      Author: petera
 */

/*
 * Design driving assumptions:
 * + NOR flash does not need block ECC like NAND flash. No need for bad block
 *   management.
 * + Power loss can happen anytime.
 * + Writing single byte to flash is atomic, and cannot be interrupted amidst
 *   bits on an unexpected power loss.
 * + Writing multiple bytes is non-atomic when it comes to power loss.
 * + Writing pulls set bits to zeroed bits.
 * + Only blocks can be erased, pulling all bits in block to ones.
 * + Erases wear the flash and must be leveled over blocks.
 * + It is allowed to overwrite already written data.
 * + Reading is random access.
 * + Calling one HAL read operation for many bytes is less costly than calling
 *   many HAL read operations for fewer bytes. I.e. it is assumed that the
 *   setup time is larger than the transfer time.
 * + RAM is small and expensive.
 * + No heap, must only use static allocated RAM and/or stack.
 */

/*
 * Design notes:
 * + Page header is really page footer. When writing a full page, the meta info
 *   will be written last, only making it a valid page if everything is written
 *   without interruptions.
 */

#ifndef _SPSF_H_
#define _SPSF_H_

#include "spfs_common.h"
#include "spfs_compile_cfg.h"

#define SPFS_VERSION                    (0x0400)

#ifndef SPFS_ERR_BASE
#define SPFS_ERR_BASE                   (5000)
#endif
/** all dandy */
#define SPFS_OK                         (0)
/** bad argument */
#define SPFS_ERR_ARG                    (SPFS_ERR_BASE+0)
/** file system not configured */
#define SPFS_ERR_UNCONFIGURED           (SPFS_ERR_BASE+1)
/** file system in an invalid mount state for operation */
#define SPFS_ERR_MOUNT_STATE            (SPFS_ERR_BASE+2)
/** FATAL: file system lu/page id inconsistency */
#define SPFS_ERR_LU_PHDR_ID_MISMATCH    (SPFS_ERR_BASE+3)
/** FATAL: file system lu/page flag inconsistency */
#define SPFS_ERR_LU_PHDR_FLAG_MISMATCH  (SPFS_ERR_BASE+4)
/** file system full, no more free or deleted pages */
#define SPFS_ERR_OUT_OF_PAGES           (SPFS_ERR_BASE+5)
/** page could not be found */
#define SPFS_ERR_PAGE_NOT_FOUND         (SPFS_ERR_BASE+6)
/** FATAL: no free unique file ids */
#define SPFS_ERR_OUT_OF_IDS             (SPFS_ERR_BASE+7)
/** block or page size in configuration is not valid */
#define SPFS_ERR_CFG_SZ_NOT_REPR        (SPFS_ERR_BASE+8)
/** image, block or page size is not aligned */
#define SPFS_ERR_CFG_SZ_NOT_ALIGNED     (SPFS_ERR_BASE+9)
/** too few pages per block - page size too big or block size too small */
#define SPFS_ERR_CFG_LPAGE_SZ           (SPFS_ERR_BASE+10)
/** too few blocks - block size too big or image size too small */
#define SPFS_ERR_CFG_LBLOCK_SZ          (SPFS_ERR_BASE+11)
/** a ram buffer has unaligned address */
#define SPFS_ERR_CFG_MEM_NOT_ALIGNED    (SPFS_ERR_BASE+12)
/** ram buffer too small or non-existent */
#define SPFS_ERR_CFG_MEM_SZ             (SPFS_ERR_BASE+13)
/** configuration and actual file system mismatchs */
#define SPFS_ERR_CFG_MOUNT_MISMATCH     (SPFS_ERR_BASE+14)
/** probe failed, no file system found */
#define SPFS_ERR_NOT_A_FS               (SPFS_ERR_BASE+15)
/** file could not be found */
#define SPFS_ERR_FILE_NOT_FOUND         (SPFS_ERR_BASE+16)
/** unknown journal id, or inconsistent journal flags */
#define SPFS_ERR_JOURNAL_BROKEN         (SPFS_ERR_BASE+17)
/** unfinished journal entry */
#define SPFS_ERR_JOURNAL_INTERRUPTED    (SPFS_ERR_BASE+18)
/** starting new operation during unfinished operation */
#define SPFS_ERR_JOURNAL_PENDING        (SPFS_ERR_BASE+19)
/** no free file descriptors */
#define SPFS_ERR_OUT_OF_FILEDESCRIPTORS (SPFS_ERR_BASE+20)
/** invalid file descriptor */
#define SPFS_ERR_BAD_FILEDESCRIPTOR     (SPFS_ERR_BASE+21)
/** closed file descriptor */
#define SPFS_ERR_FILE_CLOSED            (SPFS_ERR_BASE+22)
/** end of file */
#define SPFS_ERR_EOF                    (SPFS_ERR_BASE+23)
/** file name conflict */
#define SPFS_ERR_NAME_CONFLICT          (SPFS_ERR_BASE+24)
/** getting an unreserved reserved free page */
#define SPFS_ERR_FREE_PAGE_NOT_RESERVED (SPFS_ERR_BASE+25)
/** file not readable */
#define SPFS_ERR_NOT_READABLE           (SPFS_ERR_BASE+26)
/** file not writable */
#define SPFS_ERR_NOT_WRITABLE           (SPFS_ERR_BASE+27)
/** internal usage: do not use this as a base */
#define _SPFS_ERR_INT                   (SPFS_ERR_BASE+100)
#if SPFS_TEST
#define _SPFS_TEST_DEF(_arg) , uint32_t _arg
#define _SPFS_TEST_ARG(_arg) , _arg
#else
#define _SPFS_TEST_DEF(_arg)
#define _SPFS_TEST_ARG(_arg)
#endif

#define _SPFS_HAL_WR_FL_OVERWRITE   (1<<0)
#define _SPFS_HAL_WR_FL_IGNORE_BITS (1<<1)



/* Any write to the filehandle is appended to end of the file */
#define SPFS_APPEND                   (1<<0)
#define SPFS_O_APPEND                 SPFS_APPEND
/* If the opened file exists, it will be truncated to zero length before opened */
#define SPFS_TRUNC                    (1<<1)
#define SPFS_O_TRUNC                  SPFS_TRUNC
/* If the opened file does not exist, it will be created before opened */
#define SPFS_CREAT                    (1<<2)
#define SPFS_O_CREAT                  SPFS_CREAT
/* The opened file may only be read */
#define SPFS_RDONLY                   (1<<3)
#define SPFS_O_RDONLY                 SPFS_RDONLY
/* The opened file may only be written */
#define SPFS_WRONLY                   (1<<4)
#define SPFS_O_WRONLY                 SPFS_WRONLY
/* The opened file may be both read and written */
#define SPFS_RDWR                     (SPFS_RDONLY | SPFS_WRONLY)
#define SPFS_O_RDWR                   SPFS_RDWR
/* Any writes to the filehandle will never be cached but flushed directly */
#define SPFS_DIRECT                   (1<<5)
#define SPFS_O_DIRECT                 SPFS_DIRECT
/* If SPFS_O_CREAT and SPFS_O_EXCL are set, SPFS_open() shall fail if the file exists */
#define SPFS_EXCL                     (1<<6)
#define SPFS_O_EXCL                   SPFS_EXCL
/**
 * Data written with O_REWR will not allocate any new pages for the data.
 * Instead, it will simply rewrite existing data with given data. Considering
 * NOR flash, it means this flag can be used to clear bits in already written
 * files without allocating any new space. It will be as a logical AND
 * operation of given data and persisted data.
 * When writing with O_REWRITE, the file cannot grow. Writes must be within
 * existing file boundaries.
 * O_APPEND and O_REWR is invalid.
 */
#define SPFS_O_REWR                   (1<<7)
/**
 * Data written with O_SENS will be overwritten with zeroes when deleted.
 * Technically, any page having any sensitive bytes in it will be overwritten
 * with zeroes when deleted.
 * This is not to be used alone for safe data. Encryption must also be a part
 * of it.
 * This is more to avoid providing statistics on encrypted data to a
 * malicious attacker.
 */
#define SPFS_O_SENS                 (1<<8)


#define SPFS_SEEK_SET               (0)
#define SPFS_SEEK_CUR               (1)
#define SPFS_SEEK_END               (2)


#define SPFS_PFREE_RESV             (3)


typedef SPFS_TYPEDEF_ID id_t;
typedef SPFS_TYPEDEF_PAGE pix_t;
typedef SPFS_TYPEDEF_SPAN spix_t;
typedef SPFS_TYPEDEF_BLOCK bix_t;

struct spfs_s;

typedef enum {
  SPFS_MEM_WORK_BUF = 0,
  SPFS_MEM_FILEDESCS,
  SPFS_MEM_BLOCK_LU,
  SPFS_MEM_CACHE,
  _SPFS_MEM_TYPES
} spfs_mem_type_t;

/**
 * Function prototype for reading from medium.
 * It is guaranteed that a read call never will cross any logical page
 * boundaries. This implies that reads will only be requested anywhere within
 * following ranges:
 *   (n*log_page_size) to ((n+1)*log_page_size-1),
 * where n is a logaical page* index.
 * @param fs      the filesystem struct
 * @param addr    the address to read from
 * @param dst     the memory to read to
 * @param size    number of bytes to read
 * @param flags   only in test builds for verification and debugging
 * @return SPFS_OK on success. Anything else is considered an error.
 */
typedef int (*hal_read_t)(struct spfs_s *fs, uint32_t addr, uint8_t *dst,
    uint32_t size _SPFS_TEST_DEF(flags));
/**
 * Function prototype for writing to medium.
 * It is guaranteed that a write call never will cross any logical page
 * boundaries. This implies that writes will only be requested anywhere within
 * following ranges:
 *   (n*log_page_size) to ((n+1)*log_page_size-1),
 * where n is a logaical page* index.
 * @param fs      the filesystem struct
 * @param addr    the address to write to
 * @param src     the memory to write from
 * @param size    number of bytes to write
 * @param flags   only in test builds for verification and debugging
 * @return SPFS_OK on success. Anything else is considered an error.
 */
typedef int (*hal_write_t)(struct spfs_s *fs, uint32_t addr, const uint8_t *src,
    uint32_t size _SPFS_TEST_DEF(flags));
/**
 * Function prototype for erasing blocks on medium.
 * It is guaranteed that an erase call always start at an aligned address with
 * regard to physical block size, and is always with same size as the physical
 * block size
 * @param fs      the filesystem struct
 * @param addr    the address of the block to erase
 * @param size    number of bytes to erase
 * @param flags   only in test builds for verification and debugging
 * @return SPFS_OK on success. Anything else is considered an error.
 */
typedef int (*hal_erase_t)(struct spfs_s *fs, uint32_t addr, uint32_t size
    _SPFS_TEST_DEF(flags));
/**
 * Function prototype for requesting memory.
 * No need to be worried - this is called when mounting the file system in
 * order to get working RAM. Then, it will never be called again.
 * Note the absence of free.
 * Some memory types must get the exact amount of requested memory, while
 * others copes with less or none.
 *   SPFS_MEM_WORK_BUF: mandatory exact size,
 *   SPFS_MEM_BLOCK_LU: mandatory exact size,
 *   SPFS_MEM_FILEDESCS: enough memory for at least one filedescriptor,
 *   SPFS_MEM_CACHE: may be zero, but not recommended.
 * @param fs        the filesystem struct
 * @param type      what the memory will be used for
 * @param req_size  requested number of bytes to erase
 * @param acq_size  the implementation must populate this with actual amount
 *                  of memory given
 */
typedef void *(*hal_malloc_t)(struct spfs_s *fs, spfs_mem_type_t type,
    uint32_t req_size, uint32_t *acq_size);

typedef struct {
  // HAL function for reading from medium
  hal_read_t read;
  // HAL function for writing to medium
  hal_write_t write;
  // HAL function for erasing blocks on medium
  hal_erase_t erase;
  // HAL function for requesting memory
  hal_malloc_t malloc;
#if SPFS_CFG_DYNAMIC
  // physical flash size in bytes
  uint32_t pflash_sz;
  // physical flash address offset of fs
  uint32_t pflash_addr_offs;
  // physical block size in bytes
  uint32_t pblk_sz;
  // logical block size in bytes
  uint32_t lblk_sz;
  // logical page size in bytes
  uint32_t lpage_sz;
#endif
  // file handle offset
  int filehandle_offset;
} spfs_cfg_t;

typedef struct {
#if SPFS_CFG_DYNAMIC
  // number of log blocks
  uint16_t lblk_cnt;
  // number of log pages per log block
  uint16_t lpages_p_blk;
  // number of data pages per log block
  uint16_t dpages_p_blk;
  // number of LU pages per log block
  uint16_t lupages_p_blk;
  // number of LU entries in LU page 0
  uint16_t lu_ent_cnt0;
  // number of LU entries in LU page > 0
  uint16_t lu_ent_cnt;
  // min number of bits needed to repr all data pages
  uint8_t id_bits;
  // min number of bits needed to repr all log blocks
  uint8_t blk_bits;
#endif
} spfs_dyn_t;

typedef struct {
  // work ram 1
  uint8_t *work1;
  // work ram 1
  uint8_t *work2;
  // log/data block index lookup bit vector
  bitmanio_array32_t blk_lu;
  // log/data block lookup entry count
  uint16_t blk_lu_cnt;
  // maximum erase count amongs blocks
  uint16_t max_era_cnt;
  // filedescriptor ram
  void *fd_area;
  // filedescriptor count
  uint16_t fd_cnt;
  // cache
  void *cache;
  // cache page count
  uint16_t cache_cnt;

  bix_t lbix_gc_free;
  pix_t dpix_free_page_cursor;
  pix_t dpix_find_cursor;

  uint16_t pfree;
  uint16_t pdele;
  uint16_t pused;

  // journal info
  struct {
    pix_t dpix;
    uint32_t bitoffs;
    uint8_t resv_free;
    uint8_t pending_op;
  } journal;

  // reserved free pages
  struct {
    uint8_t ptaken;
    pix_t arr[SPFS_PFREE_RESV];
  } resv;

  bitmanio_bytearray_t lu;
} spfs_run_t;

typedef struct spfs_s {
  spfs_cfg_t cfg; // static configuration
  spfs_dyn_t dyn; // derived configuration
  spfs_run_t run; // runtime working variables
  void *user;
  uint8_t config_state;
  uint8_t mount_state;
} spfs_t;

typedef int spfs_file_t;

struct spfs_stat {
  id_t id;
  pix_t dpix;
  char name[SPFS_CFG_FILE_NAME_SZ];
  uint32_t size;
  uint8_t type;
#if SPFS_CFG_FILE_META_SZ
  uint8_t meta[SPFS_CFG_FILE_META_SZ];
#endif
};

struct spfs_dirent {
  struct spfs_stat s;
};

typedef struct spfs_dir {
  struct spfs_dirent de;
  pix_t dpix;
} spfs_DIR;


int SPFS_stat(spfs_t *fs, const char *path, struct spfs_stat *buf);
spfs_file_t SPFS_open(spfs_t *fs, const char *name, int oflags, int mode);
spfs_file_t SPFS_creat(spfs_t *fs, const char *name);
int SPFS_read(spfs_t *fs, spfs_file_t fh, void *buf, uint32_t len);
int SPFS_write(spfs_t *fs, spfs_file_t fh, const void *buf, uint32_t len);
int SPFS_close(spfs_t *fs, spfs_file_t fh);
int SPFS_remove(spfs_t *fs, const char *path);
int SPFS_lseek(spfs_t *fs, spfs_file_t fh, int offs, uint8_t whence);
int SPFS_truncate(spfs_t* fs, const char* path, uint32_t offset);
int SPFS_ftruncate(spfs_t *fs, spfs_file_t fh, uint32_t offset);
int SPFS_opendir(spfs_t *fs, spfs_DIR *d, const char *path);
struct spfs_dirent *SPFS_readdir(spfs_t *fs, spfs_DIR *d);
int SPFS_closedir(spfs_t *fs, spfs_DIR *d);

#endif /* _SPSF_H_ */
