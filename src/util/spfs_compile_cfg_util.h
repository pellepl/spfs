/*
 * spfs_compile_cfg_mkimg.h
 *
 *  Created on: Aug 10, 2017
 *      Author: petera
 */

#ifndef _SPFS_COMPILE_CFG_MKIMG_H_
#define _SPFS_COMPILE_CFG_MKIMG_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "spfs_util_common.h"

#define SPFS_TEST                       1

#define SPFS_CFG_DYNAMIC                1

#define SPFS_ERRSTR                     1
#define SPFS_DUMP                       1
#define SPFS_ASSERT                     1

#ifndef _DBG_FROM_MAKE
#define _DBG_FROM_MAKE                  0
#endif

#define SPFS_LOCK(fs)                   spfs_lock(fs)
#define SPFS_UNLOCK(fs)                 spfs_unlock(fs)

#define SPFS_DBG_ERROR                  _DBG_FROM_MAKE
#define SPFS_DBG_FS                     _DBG_FROM_MAKE
#define SPFS_DBG_FILE                   _DBG_FROM_MAKE
#define SPFS_DBG_GC                     _DBG_FROM_MAKE
#define SPFS_DBG_CACHE                  _DBG_FROM_MAKE
#define SPFS_DBG_JOURNAL                _DBG_FROM_MAKE
#define SPFS_DBG_LOWLEVEL               _DBG_FROM_MAKE
#define SPFS_DBG_HIGHLEVEL              _DBG_FROM_MAKE
#define SPFS_DBG_LL_MEDIUM_WR           0
#define SPFS_DBG_LL_MEDIUM_RD           0
#define SPFS_DBG_LL_MEDIUM_ER           0

#define SPFS_DBG_USE_ANSI_COLORS        1
#define SPFS_EXPORT                     1
#define SPFS_DUMP                       1

#define SPFS_DUMP_PRINTF(f, ...)        fprintf(__dumpfd, f, ## __VA_ARGS__)

#define BITMANIO_H_WHEREABOUTS          "bitmanio.h"

#include "spfs_util_common.h"
#include "test/framework/spif_emul.h"

#endif /* _SPFS_COMPILE_CFG_MKIMG_H_ */
