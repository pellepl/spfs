/*
 * spfs_compile_cfg.h
 *
 *  Created on: Aug 10, 2017
 *      Author: petera
 */

#ifndef _SPFS_COMPILE_CFG_H_
#define _SPFS_COMPILE_CFG_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#if SPFS_TEST
#include "spfs_compile_cfg_test.h"
#else
#include "spfs_compile_cfg_local.h"
#endif

// normally, these types should not be tampered with
#ifndef SPFS_TYPEDEF_PAGE
// this type must hold maximum number of pages in filesystem
#define SPFS_TYPEDEF_PAGE   uint32_t
#endif
#ifndef SPFS_TYPEDEF_SPAN
// this type must hold maximum number of pages any file can consist of
#define SPFS_TYPEDEF_SPAN   uint32_t
#endif
#ifndef SPFS_TYPEDEF_ID
// this type must hold absolute maximum number of files
#define SPFS_TYPEDEF_ID     uint32_t
#endif
#ifndef SPFS_TYPEDEF_BLOCK
// this type must hold maximum number of blocks
#define SPFS_TYPEDEF_BLOCK  uint16_t
#endif

// this is the target required memory alignment
#ifndef SPFS_ALIGN
#define SPFS_ALIGN sizeof(void *)
#endif

#ifndef SPFS_CFG_DYNAMIC
#define SPFS_CFG_DYNAMIC                0
#endif

#ifndef SPFS_CFG_FILE_NAME_SZ
#define SPFS_CFG_FILE_NAME_SZ           (32)
#endif
#ifndef SPFS_CFG_FILE_META_SZ
#define SPFS_CFG_FILE_META_SZ           (0)
#endif
#ifndef SPFS_CFG_COPY_BUF_SZ
#define SPFS_CFG_COPY_BUF_SZ            (128)
#endif


/**
 * GC candidate picker counts free, deleted and used pages per block.
 * Also, the erase count difference is calculated as the difference between
 * global maximum erase count minus current block's erase count.
 * Each count of a type of page is multiplied by 256 and normalized against
 * total number of pages in a block. The normalization is ceiled as we're
 * avoiding floating arithmetics.
 * This means that (deleted_pages + free_pages + used_pages) will always
 * represent 256 or a little more due to rounding errors.
 * The GC score will then be calculated as a sum of weights:
 *    W_era_cnt_diff * era_cnt_diff +
 *    W_deleted * deleted_count_normalized +
 *    W_free * free_count_normalized +
 *    W_used * used_count_normalized
 */
// gc score weight factor for block erase count count difference
#ifndef SPFS_CFG_GC_WEIGHT_ERA_CNT
#define SPFS_CFG_GC_WEIGHT_ERA_CNT(fs)    (32) //repr 32/256=1/8=0.125
#endif
// gc score weight factor for deleted pages, normally adds to score
#ifndef SPFS_CFG_GC_WEIGHT_DELE
#define SPFS_CFG_GC_WEIGHT_DELE(fs)       (2)
#endif
// gc score weight factor for free pages, normally subtracts from score
#ifndef SPFS_CFG_GC_WEIGHT_FREE
#define SPFS_CFG_GC_WEIGHT_FREE(fs)       (-1)
#endif
// gc score weight factor for used pages, may subtract from score
#ifndef SPFS_CFG_GC_WEIGHT_USED
#define SPFS_CFG_GC_WEIGHT_USED(fs)       (0)
#endif

// Data written with SPFS_O_SENSITIVE will be physically zeroed
// on spiflash when the data is deleted. It will however add an
// extra read call every time a page needs to be deleted. 
#ifndef SPFS_CFG_SENSITIVE_DATA
#define SPFS_CFG_SENSITIVE_DATA           (0)
#endif


#ifndef SPFS_ERRSTR
#define SPFS_ERRSTR                       0
#endif
#ifndef SPFS_DUMP
#define SPFS_DUMP                         0
#endif


#ifndef BITMANIO_H_WHEREABOUTS
#define BITMANIO_H_WHEREABOUTS          "bitmanio.h"
#endif

#endif /* _SPFS_COMPILE_CFG_H_ */
