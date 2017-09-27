/*
 * spfs_util_lock.c
 *
 *  Created on: Sep 27, 2017
 *      Author: petera
 */

#include "spfs_util_lock.h"
#include "spfs.h"
#include <pthread.h>

pthread_mutex_t spfs_mutex = PTHREAD_MUTEX_INITIALIZER;

void *spfs_lock_get_mutex(void) {
  return &spfs_mutex;
}

// overrideable
__attribute__(( weak )) void spfs_lock(void *fs) {
  if (((spfs_t*)fs)->user) {
    pthread_mutex_lock((pthread_mutex_t *)((spfs_t*)fs)->user);
  }
}

// overrideable
__attribute__(( weak )) void spfs_unlock(void *fs) {
  if (((spfs_t*)fs)->user) {
    pthread_mutex_unlock((pthread_mutex_t *)((spfs_t*)fs)->user);
  }
}
