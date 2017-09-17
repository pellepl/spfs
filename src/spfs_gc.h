/*
 * spfs_gc.h
 *
 *  Created on: Sep 13, 2017
 *      Author: petera
 */

#ifndef _SPFS_GC_H_
#define _SPFS_GC_H_

#include "spfs_compile_cfg.h"
#include "spfs.h"
#include "spfs_lowlevel.h"

_SPFS_STATIC int spfs_gc(spfs_t *fs);
_SPFS_STATIC int spfs_gc_evacuate(spfs_t *fs, bix_t src_dbix);

#endif /* _SPFS_GC_H_ */
