/*
 * spfs_file.h
 *
 *  Created on: Sep 9, 2017
 *      Author: petera
 */

#ifndef _SPFS_FILE_H_
#define _SPFS_FILE_H_

#include "spfs_compile_cfg.h"
#include "spfs.h"
#include "spfs_lowlevel.h"

/**
 * File handle.
 */
typedef struct spfs_fd_s {
  /** file handle number */
  spfs_file_t hdl;
  /** current file descriptor offset */
  uint32_t offset;
  /** data page index for index page for current offset */
  pix_t dpix_ix;
  /** data page index for index header page */
  pix_t dpix_ixhdr;
  /** file info */
  spfs_fi_t fi;
  /** descriptor flags */
  uint32_t fd_oflags;
} spfs_fd_t;


typedef struct {
  /** file info on visited file */
  spfs_fi_t *fi;
  /** bit array over current index page entries */
  barr8 ixarr;
  /** span index for current index page */
  spix_t ixspix;
  /** data page index for current index page */
  pix_t dpix_ix;
  /** data page index for current index header page */
  pix_t dpix_ixhdr;
  /** relative index entry number for current index page */
  uint32_t ixent;
  /** current file offset */
  uint32_t offset;
  /** valid data length */
  uint32_t len;
  /** visitor flags */
  uint32_t v_flags;
  /** indicates if the index was created in memory only during visiting */
  uint8_t ix_constructed;
} spfs_file_vis_info_t;

typedef enum {
  SPFS_F_EV_UPDATE_IX,
  SPFS_F_EV_REMOVE_IX,
  SPFS_F_EV_NEW_SIZE
} spfs_file_event_t;

typedef union {
  struct {
    spix_t spix;
    pix_t dpix;
  } update;
  struct {
    spix_t spix;
  } remove;
  uint32_t size;
} spfs_file_event_data_t;

/**
 * File visitor:
 * Called for each page denoted by each entry in corresponding index page.
 * @param fs      the file system
 * @param dpix    the data page as denoted by index entry
 * @param info    information of current file visitor state
 * @param varg    the user pointer passed to file visit function
 */
typedef int (* spfs_file_visitor_t)(spfs_t *fs, pix_t dpix,
    spfs_file_vis_info_t *info, void *varg);
/**
 * File visitor:
 * Called each time a new index page is loaded to memory and when visitor is done.
 * @param fs      the file system
 * @param final   zero if in midst visiting stuff, nonzero if visiting is finished
 * @param res     current result of visiting pages
 * @param info    information of current file visitor state
 * @param varg    the user pointer passed to file visit function
 */
typedef int (* spfs_file_ix_visitor_t)(spfs_t *fs, uint8_t final, int res,
    spfs_file_vis_info_t *info, void *varg);


_SPFS_STATIC int spfs_file_find(spfs_t *fs, const char *name, pix_t *dpix, spfs_pixhdr_t *pixhdr);

/**
 * Visit a file from given offset to offset plus len. Will look up corresponding
 * index page(s) and put it in work buffer 2. Entries in given range will be visited
 * by visitor v. Before a new index page is loaded, visitor vix is called. Also,
 * vix is called when visiting is finished, either erroneous or successful.
 * @param fs      the file system
 * @param i       file information
 * @param dpix_ixhdr  the data page index of the file index header
 * @param offset  offset in file where to start visit
 * @param len     how many bytes to visit
 * @param create_memory_ix  create empty index tables to memory if persisted not found
 * @param varg    user argument passed to visitor functions
 * @param v       visitor called for each data page
 * @param vix     visitor called before loading a new index page and when visiting ends
 * @param v_flags visitor flags, normally fd flags
 */
_SPFS_STATIC int spfs_file_visit(spfs_t *fs,
                                 spfs_fi_t *fi, pix_t dpix_ixhdr,
                                 uint32_t offset, uint32_t len,
                                 uint8_t create_memory_ix,
                                 void *varg,
                                 spfs_file_visitor_t v, spfs_file_ix_visitor_t vix,
                                 uint32_t v_flags);
_SPFS_STATIC void _fd_init(spfs_t *fs, void *mem, uint32_t mem_sz);
_SPFS_STATIC int _fd_claim(spfs_t *fs, spfs_fd_t **fd);
_SPFS_STATIC int _fd_resolve(spfs_t *fs, spfs_file_t fh, spfs_fd_t **fd);
_SPFS_STATIC void _fd_release(spfs_t *fs, spfs_fd_t *fd);
_SPFS_STATIC int spfs_file_create(spfs_t *fs, spfs_fd_t *fd, const char *name);
_SPFS_STATIC int spfs_file_create_fix(spfs_t *fs, spfs_fd_t *fd, const char *name, uint32_t fixed_size);
/*_SPFS_STATIC int spfs_file_create_rot(spfs_t *fs, spfs_fd_t *fd, const char *name, uint32_t rot_size);*/
_SPFS_STATIC int spfs_file_read(spfs_t *fs, spfs_fd_t *fd, uint32_t offs, uint32_t len, uint8_t *dst);
_SPFS_STATIC int spfs_file_write(spfs_t *fs, spfs_fd_t *fd, uint32_t offs, uint32_t len,
                                 const uint8_t *src);
_SPFS_STATIC int spfs_file_fremove(spfs_t *fs, spfs_fd_t *fd);
_SPFS_STATIC int spfs_file_remove(spfs_t *fs, const char *path);
_SPFS_STATIC int spfs_file_ftruncate(spfs_t *fs, spfs_fd_t *fd, uint32_t size);
_SPFS_STATIC int spfs_file_truncate(spfs_t *fs, const char *path, uint32_t target_size);

#endif /* _SPFS_FILE_H_ */
