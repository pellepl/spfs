/*
 * spfs_dbg.h
 *
 *  Created on: Aug 14, 2017
 *      Author: petera
 */

#ifndef _SPFS_DBG_H_
#define _SPFS_DBG_H_

#include "spfs_compile_cfg.h"

// do not dump config
#define SPFS_DUMP_NO_CFG            (1<<0)
// do not dump state
#define SPFS_DUMP_NO_STATE          (1<<1)
// do not dump file system contents
#define SPFS_DUMP_NO_FS             (1<<2)
// do not dump file system free pages
#define SPFS_DUMP_NO_FREE           (1<<3)
// do not dump file system deleted pages
#define SPFS_DUMP_NO_DELE           (1<<4)
// do not dump block headers
#define SPFS_DUMP_NO_BLOCK_HDRS     (1<<5)
// dump full page data
#define SPFS_DUMP_PAGE_DATA         (1<<6)
// dump journal entries
#define SPFS_DUMP_JOURNAL           (1<<7)
// dump only entries with spix 0
#define SPFS_DUMP_ONLY_SPIX0        (1<<8)

#define SPFS_DUMP_SMALL             \
  (SPFS_DUMP_NO_CFG | SPFS_DUMP_NO_STATE | SPFS_DUMP_NO_FREE | \
   SPFS_DUMP_JOURNAL | SPFS_DUMP_NO_DELE)
#define SPFS_DUMP_LS                \
  (SPFS_DUMP_NO_CFG | SPFS_DUMP_NO_STATE | SPFS_DUMP_NO_FREE | \
   SPFS_DUMP_NO_BLOCK_HDRS | SPFS_DUMP_NO_DELE | SPFS_DUMP_ONLY_SPIX0)

// export, metadata only
#define SPFS_DUMP_EXPORT_META       (0)
// export, all data
#define SPFS_DUMP_EXPORT_ALL        (1)

#define SPFS_EXPORT_START           "SPFS EXPORT START\n"
#define SPFS_EXPORT_VER             "ver %d.%d.%d"
#define SPFS_EXPORT_OCHK            "OCHK:%04x"
#define SPFS_EXPORT_CHK             "CHK:%04x"
#define SPFS_EXPORT_RES             "RES:%d"
#define SPFS_EXPORT_END             "SPFS EXPORT END\n"
#define SPFS_EXPORT_HDR_UNPACKED    'U'
#define SPFS_EXPORT_HDR_PACKED      'P'
#define SPFS_EXPORT_HDR_FREE        'F'

// Defines debug

// Set generic spfs debug output call.
#ifndef SPFS_DBG
#define SPFS_DBG(_f, ...) printf(_f, ## __VA_ARGS__)
#endif
// Set generic spfs error output call.
#ifndef SPFS_ERR
#define SPFS_ERR(_f, ...) printf(_f, ## __VA_ARGS__)
#endif
// Set debug output call for all api invocations.
#ifndef SPFS_API
#define SPFS_API(_f, ...) //printf(_f, ## __VA_ARGS__)
#endif

// enable or disable assert errors
#ifndef SPFS_ASSERT
#define SPFS_ASSERT                 0
#endif
// enable or disable error output
#ifndef SPFS_DBG_ERROR
#define SPFS_DBG_ERROR              0
#endif
// enable or disable internal debug output
#ifndef SPFS_DBG_LOWLEVEL
#define SPFS_DBG_LOWLEVEL           0
#endif
// enable or disable cache debug output
#ifndef SPFS_DBG_CACHE
#define SPFS_DBG_CACHE              0
#endif
// enable or disable file debug output
#ifndef SPFS_DBG_FILE
#define SPFS_DBG_FILE               0
#endif
// enable or disable journal debug output
#ifndef SPFS_DBG_JOURNAL
#define SPFS_DBG_JOURNAL            0
#endif
// enable or disable file system debug output
#ifndef SPFS_DBG_FS
#define SPFS_DBG_FS                 0
#endif
// enable or disable garbage collection debug output
#ifndef SPFS_DBG_GC
#define SPFS_DBG_GC                 0
#endif
// enable or disable api implementation debug output
#ifndef SPFS_DBG_HIGHLEVEL
#define SPFS_DBG_HIGHLEVEL          0
#endif
// enable or disable debug on lowlevel medium write
#ifndef SPFS_DBG_LL_MEDIUM_WR
#define SPFS_DBG_LL_MEDIUM_WR       0
#endif
// enable or disable debug on lowlevel medium read
#ifndef SPFS_DBG_LL_MEDIUM_RD
#define SPFS_DBG_LL_MEDIUM_RD       0
#endif
// enable or disable debug on lowlevel medium erase
#ifndef SPFS_DBG_LL_MEDIUM_ER
#define SPFS_DBG_LL_MEDIUM_ER       0
#endif


#ifndef SPFS_DBG_USE_ANSI_COLORS
#define SPFS_DBG_USE_ANSI_COLORS    0
#endif

// some general signed number
#ifndef _SPIPRIi
#define _SPIPRIi   "%d"
#endif
// address
#ifndef _SPIPRIp
#define _SPIPRIp   "%p"
#endif
// flash address
#ifndef _SPIPRIad
#define _SPIPRIad  "%08x"
#endif
// block
#ifndef _SPIPRIbl
#define _SPIPRIbl  "%04x"
#endif
// page
#ifndef _SPIPRIpg
#define _SPIPRIpg  "%04x"
#endif
// span index
#ifndef _SPIPRIsp
#define _SPIPRIsp  "%04x"
#endif
// file descriptor
#ifndef _SPIPRIfd
#define _SPIPRIfd  "%d"
#endif
// file object id
#ifndef _SPIPRIid
#define _SPIPRIid  "%04x"
#endif
// file flags
#ifndef _SPIPRIfl
#define _SPIPRIfl  "%02x"
#endif

#if SPFS_DBG_USE_ANSI_COLORS
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_WHITE   "\x1b[37;1m"
#define ANSI_COLOR_RED     "\x1b[31;1m"
#define ANSI_COLOR_BLUE    "\x1b[34;1m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_PINK    "\x1b[35;1m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"
#else
#define ANSI_COLOR_GREEN   ""
#define ANSI_COLOR_YELLOW  ""
#define ANSI_COLOR_RED     ""
#define ANSI_COLOR_BLUE    ""
#define ANSI_COLOR_CYAN    ""
#define ANSI_COLOR_RESET   ""
#endif

#ifndef SPFS_DUMP_PRINTF
#define SPFS_DUMP_PRINTF(f, ...) printf(f, ## __VA_ARGS__)
#endif

// internal debug macros

#ifndef _SPFS_DBG_PRE
#define _SPFS_DBG_PRE "   "
#endif
#ifndef _SPFS_DBG_POST
#define _SPFS_DBG_POST ""
#endif
#ifndef _SPFS_DBG_PRE_ERR
#define _SPFS_DBG_PRE_ERR ANSI_COLOR_RED

#endif

// define error message output
#if SPFS_DBG_ERROR
#ifndef SPFS_ERRMSG
#if SPFS_ERRSTR
#define SPFS_ERRMSG(_x) \
    if ((_x)) { \
      SPFS_ERR("%s%s<%-30s> (%s:%d): ERR %d %s%s\n", _SPFS_DBG_PRE, _SPFS_DBG_PRE_ERR, __func__, \
                __FILE__, __LINE__, (_x), spfs_strerror(_x), _SPFS_DBG_POST); \
    }
#else // SPFS_ERRSTR
#define SPFS_ERRMSG(_x) \
    if ((_x)) { \
      SPFS_ERR("%s%s<%-30s> (%s:%d): ERR %d%s\n", _SPFS_DBG_PRE, _SPFS_DBG_PRE_ERR, __func__, \
                __FILE__, __LINE__, (_x), _SPFS_DBG_POST); \
    }
#endif // SPFS_ERRSTR
#endif // SPFS_ERRMSG
#else // SPFS_DBG_ERROR
#define SPFS_ERRMSG(_x)
#endif

// print and return if error
#define ERR(_x) do { \
    int ___res = (_x); \
    if (___res) { \
      SPFS_ERRMSG(___res); \
      return ___res; \
    } \
  } while(0)

// print, unlock, and return if error
#define ERRUNLOCK(_fs, _x) do { \
    int ___res = (_x); \
    if (___res) { \
      SPFS_ERRMSG(___res); \
      SPFS_UNLOCK(_fs); \
      return ___res; \
    } \
  } while(0)

// goto err on error
#define ERRGO(_x) do { \
    int ___res = (_x); \
    if (___res) { \
      SPFS_ERRMSG(___res); \
      goto err; \
    } \
  } while(0)

// print if error and return result regardless
#define ERRET(_x) do { \
    int ___res = (_x); \
    SPFS_ERRMSG(___res); \
    return ___res; \
  } while (0)

#if SPFS_DBG_LOWLEVEL
#define dbg_ll(_f, ...) \
  SPFS_DBG("%s.%-30s. "_f"%s", _SPFS_DBG_PRE, __func__ ,## __VA_ARGS__, _SPFS_DBG_POST)
#else
#define dbg_ll(_f, ...)
#endif
#if SPFS_DBG_CACHE
#define dbg_ca(_f, ...) \
  SPFS_DBG("%s.%-30s. "_f"%s", _SPFS_DBG_PRE, __func__ ,## __VA_ARGS__, _SPFS_DBG_POST)
#else
#define dbg_ca(_f, ...)
#endif
#if SPFS_DBG_FILE
#define dbg_fi(_f, ...) \
  SPFS_DBG("%s.%-30s. "_f"%s", _SPFS_DBG_PRE, __func__ ,## __VA_ARGS__, _SPFS_DBG_POST)
#else
#define dbg_fi(_f, ...)
#endif
#if SPFS_DBG_JOURNAL
#define dbg_jo(_f, ...) \
  SPFS_DBG("%s.%-30s. "_f"%s", _SPFS_DBG_PRE, __func__ ,## __VA_ARGS__, _SPFS_DBG_POST)
#else
#define dbg_jo(_f, ...)
#endif
#if SPFS_DBG_FS
#define dbg_fs(_f, ...) \
  SPFS_DBG("%s.%-30s. "_f"%s", _SPFS_DBG_PRE, __func__ ,## __VA_ARGS__, _SPFS_DBG_POST)
#else
#define dbg_fs(_f, ...)
#endif
#if SPFS_DBG_GC
#define dbg_gc(_f, ...) \
  SPFS_DBG("%s.%-30s. "_f"%s", _SPFS_DBG_PRE, __func__ ,## __VA_ARGS__, _SPFS_DBG_POST)
#else
#define dbg_gc(_f, ...)
#endif
#if SPFS_DBG_HIGHLEVEL
#define dbg_hl(_f, ...) \
  SPFS_DBG("%s.%-30s. "_f"%s", _SPFS_DBG_PRE, __func__ ,## __VA_ARGS__, _SPFS_DBG_POST)
#else
#define dbg_hl(_f, ...)
#endif

#if SPFS_ASSERT
#ifndef spfs_assert
#define spfs_assert(x) if (!(x)) ERRET(-SPFS_ERR_ASSERT)
#endif
#else
#define spfs_assert(x)
#endif

#if SPFS_DUMP
void spfs_dump(spfs_t *fs, uint32_t flags);
#endif

#if SPFS_EXPORT
int spfs_export(spfs_t *fs, uint32_t flags);
#endif

#if SPFS_ERRSTR
const char *spfs_strerror(int err);
#endif

#endif /* _SPFS_DBG_H_ */

