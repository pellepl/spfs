/*
 * spfs_cache.h
 *
 *  Created on: Aug 19, 2017
 *      Author: petera
 */

#ifndef _SPFS_CACHE_H_
#define _SPFS_CACHE_H_

#include "spfs_compile_cfg.h"
#include "spfs.h"

/*

 spfs caching mechanisms.

 The cache pages are stored in three double-linked lists: Free, Read, Write.

 Each page represents a buffer being same size as the log page size.

 When a page is needed, the Free list is checked first. If it contains
 pages, the first page is taken. If the Free list is empty, the first page
 in the Read list is taken. If there are no pages in either Free or Read lists,
 the operation cannot be cached.

 When taking a cache page, the first page is always taken.
 When a read or write cache page is accessed, it is touched - meaning it is
 moved to end of corresponding list. This will ensure that the most recent
 accessed page will have least probability to be taken.

 When searching for files and such, many LU pages might be traversed before
 finding the correct page. This would shred the read cache. In these cases, a
 page taken to represent a LU page will be put in the beginning of the Read
 list making it least prioritized.

 Read cache pages are simply mirrors of logical pages.

 Write cache pages are more complicated. A write cache page (WCP) is assigned
 to file descriptors. As many file descriptors can be opened to the same file,
 many fd:s can also be associated with the same WCP - but a WCP is always
 associated to a unique file id.

 The WCP keeps track of (cached) file offset and length, and overrides the
 info of the file descriptor.

 When writing, a WCP is only requested if the write length is less than a
 logical page size. Else, it is written through. On such a write, any
 related uncommited WCP is flushed before the write operation.

 If a write operation writes data that is beyond the range of the current WCP,
 the WCP is first flushed and then reassigned to the bounds of the new write
 operation.

 For each read/seek/tell/eof operation it is checked if corresponding file has
 a WCP. If so, the WCP is first flushed. This will update the actual length and
 data of the file to ensure consistency.

 When a file is removed, any related uncommitted WCPs are dropped.

 When a file is truncated, any related WCPs with offset beyond new end are
 dropped.

 Metadata updates such as LU, file headers etc updates the read cache and
 writes through.

 */

typedef struct spfs_cache_page_s {
  uint8_t flags;
  union {
    // type read cache
    struct {
      // read cache logical page index
      uint32_t lpix;
    };
    // type write cache
    struct {
      // write cache
      uint32_t id;
      // cache page offset in file
      uint32_t offset;
      // size of cached data
      uint16_t size;
    };
  };
  struct spfs_cache_page_s *_prev;
  struct spfs_cache_page_s *_next;
  uint8_t *buf;
} spfs_cache_page;


typedef struct {
  spfs_cache_page *head;
  spfs_cache_page *tail;
} spfs_cache_list;

typedef struct {
  spfs_cache_list read;
  spfs_cache_list write;
  spfs_cache_list free;
} spfs_cache;

#define SPFS_CACHE_FL_TYPE_MASK     (1<<0)
#define SPFS_CACHE_FL_TYPE_RD       (0<<0)
#define SPFS_CACHE_FL_TYPE_WR       (1<<0)
#define SPFS_CACHE_FL_RD_LU         (1<<1)
#define SPFS_CACHE_FL_WR_DIRTY      (1<<2)

_SPFS_STATIC int spfs_cache_init(spfs_t *fs, void *mem, uint32_t sz);
_SPFS_STATIC int spfs_cache_page_claim(spfs_t *fs, uint8_t flags, spfs_cache_page **page);
_SPFS_STATIC int spfs_cache_page_flush(spfs_t *fs, spfs_cache_page *p);
_SPFS_STATIC int spfs_cache_page_drop(spfs_t *fs, spfs_cache_page *p);
_SPFS_STATIC spfs_cache_page *spfs_cache_rdpage_lookup(spfs_t *fs, uint32_t lpix);


#endif /* _SPFS_CACHE_H_ */
