/*
 * cfs_tormentor.h
 *
 *  Created on: Sep 22, 2017
 *      Author: petera
 */

#ifndef _CFS_TORMENTOR_H_
#define _CFS_TORMENTOR_H_

#include <stdint.h>
#include "cfs.h"

typedef struct torment_session {
  uint32_t max_fs_size;
  void *_priv;
} torment_session_t;

int torment_create_session(uint32_t seed, torment_session_t *sess, cfs_t *cfs);
int torment_step(torment_session_t *sess);
int torment_run(torment_session_t *sess);
void torment_cleanup_session(torment_session_t *sess);


#endif /* _CFS_TORMENTOR_H_ */
