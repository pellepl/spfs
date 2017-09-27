/*
 * spfs_compile_cfg_test.h
 *
 *  Created on: Aug 10, 2017
 *      Author: petera
 */

#ifndef _SPFS_COMPILE_CFG_TEST_H_
#define _SPFS_COMPILE_CFG_TEST_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "test/framework/spif_emul.h"

#define SPFS_T_CFG_PSZ                  (65536*16*1)
#define SPFS_T_CFG_PADDR_OFFS           (0)
#define SPFS_T_CFG_LBLK_SZ              (65536)
#define SPFS_T_CFG_PBLK_SZ              (65536)
#define SPFS_T_CFG_LPAGE_SZ             (256)

#define SPFS_CFG_DYNAMIC                1
#if !SPFS_CFG_DYNAMIC
#define SPFS_CFG_PSZ(ignore)            SPFS_T_CFG_PSZ
#define SPFS_CFG_PADDR_OFFS(ignore)     SPFS_T_CFG_PADDR_OFFS
#define SPFS_CFG_LBLK_SZ(ignore)        SPFS_T_CFG_LBLK_SZ
#define SPFS_CFG_PBLK_SZ(ignore)        SPFS_T_CFG_PBLK_SZ
#define SPFS_CFG_LPAGE_SZ(ignore)       SPFS_T_CFG_LPAGE_SZ
#endif

#define SPFS_CFG_FILE_NAME_SZ           (32)
#define SPFS_CFG_FILE_META_SZ           (3)
#define SPFS_CFG_COPY_BUF_SZ            (256)
#define SPFS_CFG_SENSITIVE_DATA         (1)

#define SPFS_ERRSTR                     1
#define SPFS_DUMP                       1
#define SPFS_EXPORT                     1
#define SPFS_ASSERT                     1

#ifndef _DBG_FROM_MAKE
#define _DBG_FROM_MAKE                  0
#endif

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

#define BITMANIO_H_WHEREABOUTS          "bitmanio.h"

#endif /* _SPFS_COMPILE_CFG_TEST_H_ */
