/*
 * spfs_fs.h
 *
 *  Created on: Sep 13, 2017
 *      Author: petera
 */

#ifndef _SPFS_FS_H_
#define _SPFS_FS_H_

#include "spfs_compile_cfg.h"
#include "spfs.h"
#include "spfs_lowlevel.h"

_SPFS_STATIC int spfs_config(spfs_t *fs, spfs_cfg_t *cfg, void *user);
_SPFS_STATIC int spfs_format(spfs_t *fs);
_SPFS_STATIC int spfs_mount(spfs_t *fs, uint32_t mount_flags, uint32_t descriptors, uint32_t cache_pages);
_SPFS_STATIC int spfs_umount(spfs_t *fs);
_SPFS_STATIC int spfs_probe(spfs_cfg_t *cfg, uint32_t start_addr, uint32_t end_addr);

#endif /* _SPFS_FS_H_ */
