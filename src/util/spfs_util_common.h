/*
 * spfs_linux_common.h
 *
 *  Created on: Sep 27, 2017
 *      Author: petera
 */

#ifndef _SPFS_UTIL_COMMON_H_
#define _SPFS_UTIL_COMMON_H_

#include <stdio.h>

extern FILE *__dumpfd;

void *spfs_lock_get_mutex(void);
void spfs_lock(void *fs);
void spfs_unlock(void *fs);

#endif /* _SPFS_UTIL_COMMON_H_ */
