/*
 * cache.c
 *
 *  Created on: Aug 19, 2017
 *      Author: petera
 */

//@REQUIRED_GCOV:90

#include "spfs_compile_cfg.h"
#include "spfs.h"
#include "spfs_lowlevel.h"
#include "spfs_cache.h"

#undef _SPFS_DBG_PRE
#undef _SPFS_DBG_POST
#define _SPFS_DBG_PRE ANSI_COLOR_CYAN"CA "
#define _SPFS_DBG_POST ANSI_COLOR_RESET
#include "spfs_dbg.h"
#undef dbg
#define dbg(_f, ...) dbg_ca(_f, ## __VA_ARGS__)

#define cache(_fs)      ((spfs_cache *)((_fs)->run.cache))
#define _is_empty(_l)   ((_l)->head == NULL)
#define free_list(_fs)  (&cache(_fs)->free)
#define read_list(_fs)  (&cache(_fs)->read)
#define write_list(_fs) (&cache(_fs)->write)

static void _add_last(spfs_cache_list *l, spfs_cache_page *p) {
  if (l->head == NULL) {
     l->head = p;
     p->_prev = NULL;
   } else {
     l->tail->_next = p;
     p->_prev = l->tail;
   }
   l->tail = p;
   p->_next = NULL;
}

static void _add_first(spfs_cache_list *l, spfs_cache_page *p) {
  if (l->head == NULL) {
    l->tail = p;
    p->_next = NULL;
  } else {
    l->head->_prev = p;
    p->_next = l->head;
  }
  l->head = p;
  p->_prev = NULL;
}

static spfs_cache_page *_remove_first(spfs_cache_list *l) {
  if (l->head == NULL) return NULL;
  spfs_cache_page *p;
  if (l->head == l->tail) {
    p = l->head;
    l->head = NULL;
    l->tail = NULL;
  } else {
    p = l->head;
    l->head->_prev = NULL;
    l->head = p->_next;
  }
  p->_next = NULL;
  p->_prev = NULL;
  return p;
}

static void _remove(spfs_cache_list *l, spfs_cache_page *p) {
  // first remove
  if (p == l->head) {
    // removing first element in list
    l->head = l->head->_next;
  } else {
    p->_prev->_next = p->_next;
  }

  if (p->_next == NULL) { // no element after this
    l->tail = p->_prev;
  } else {
    p->_next->_prev = p->_prev;
  }
}

static void _touch(spfs_cache_list *l, spfs_cache_page *p) {
  if (l->tail == p) return; // already at end of list
  // first remove
  _remove(l, p);
  // then add
  _add_last(l, p);
}

// This function has two return codes, actual return value and **page,
// If cache page was claimed, it will end up in *page.
// If, when claiming a page, data had to be persistenced, the write result goes
// in return value.
_SPFS_STATIC int spfs_cache_page_claim(spfs_t *fs, uint8_t flags, spfs_cache_page **page) {
  spfs_cache_page *p = NULL;
  int res = SPFS_OK;
  char wr = ((flags & SPFS_CACHE_FL_TYPE_MASK) == SPFS_CACHE_FL_TYPE_WR);
  if (!_is_empty(free_list(fs))) {
    p = _remove_first(free_list(fs));
  } else if (!_is_empty(read_list(fs))) {
    p = _remove_first(read_list(fs));
  } else if (wr && !_is_empty(write_list(fs))) {
    p = _remove_first(write_list(fs));
    res = spfs_cache_page_flush(fs, p);
  }
  if (p) {
    if (wr) {
      p->flags = SPFS_CACHE_FL_TYPE_WR;
      _add_last(write_list(fs), p);
    } else {
      if (flags & SPFS_CACHE_FL_RD_LU) {
        p->flags = SPFS_CACHE_FL_TYPE_RD | SPFS_CACHE_FL_RD_LU;
        _add_first(read_list(fs), p);
      } else {
        p->flags = SPFS_CACHE_FL_TYPE_RD;
        _add_last(read_list(fs), p);
      }
    }
  }
  *page = p;
  return res;
}
_SPFS_STATIC int spfs_cache_page_flush(spfs_t *fs, spfs_cache_page *p) {
  return 0;
}

_SPFS_STATIC int spfs_cache_page_drop(spfs_t *fs, spfs_cache_page *p) {
  if ((p->flags & SPFS_CACHE_FL_TYPE_MASK) == SPFS_CACHE_FL_TYPE_RD) {
    _remove(read_list(fs), p);
  } else {
    _remove(write_list(fs), p);
  }
  _add_last(free_list(fs), p);
  return 0;
}

spfs_cache_page *spfs_cache_rdpage_lookup(spfs_t *fs, pix_t lpix){
  spfs_cache_page *p = free_list(fs)->head;
  while (p) {
    if (p->lpix == lpix) {
      _touch(free_list(fs), p);
      return p;
    }
    p = p->_next;
  }
  return NULL;
}

_SPFS_STATIC int spfs_cache_init(spfs_t *fs, void *mem, uint32_t sz) {
  // portion up the cache memory given to us
  uint8_t *m = (uint8_t *)mem;
  uint32_t pages = (sz - spfs_align(sizeof(spfs_cache), SPFS_ALIGN)) / (
      spfs_align(sizeof(spfs_cache_page), SPFS_ALIGN) + SPFS_CFG_LPAGE_SZ(fs));
  dbg("cache pages: " _SPIPRIi "\n", pages);
  spfs_memset(mem, 0x00, sz);
  fs->run.cache_cnt = pages;
  fs->run.cache = (void *)m;
  m += spfs_align(sizeof(spfs_cache), SPFS_ALIGN);

  uint32_t p;
  for (p = 0; p < pages; p++) {
    spfs_cache_page *page;
    page = (spfs_cache_page *)m;
    m += spfs_align(sizeof(spfs_cache_page), SPFS_ALIGN);
    page->buf = m;
    m += SPFS_CFG_LPAGE_SZ(fs);
    _add_last(free_list(fs), page);
  }

  return SPFS_OK;
}
