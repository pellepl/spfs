/*
 * cfs_tormentor.c
 *
 *  Created on: Sep 22, 2017
 *      Author: petera
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "cfs.h"
#include "cfs_tormentor.h"
#include "prand.h"

typedef struct priv {
  double honor_remove_when_full;
  double honor_create_when_empty;
  double keep_large_files;
  double keep_small_files;
  double modify_large_files;
  double modify_small_files;
  prnd_t rnd;
  cfs_t *cfs;
} priv_t;

#define PRIV(_s) ((priv_t *)((_s)->_priv))

static void memrnd(torment_session_t *sess, uint8_t *dst, uint32_t len) {
  while (len--) {
    *dst++=prndbyte(&PRIV(sess)->rnd);
  }
}

int torment_create_session(uint32_t seed, torment_session_t *sess, cfs_t *cfs) {
  priv_t *p = malloc(sizeof(priv_t));
  if (p == NULL) return ENOMEM;
  memset(p, 0, sizeof(priv_t));
  sess->_priv = p;
  prnd_seed(&p->rnd, seed);
  p->cfs = cfs;
  p->honor_remove_when_full = prndrngd(&PRIV(sess)->rnd, 0, 1);
  p->honor_create_when_empty = prndrngd(&PRIV(sess)->rnd, 0, 1);
  p->keep_large_files = prndrngd(&PRIV(sess)->rnd, 0, 1);
  p->keep_small_files = prndrngd(&PRIV(sess)->rnd, 0, 1);
  p->modify_large_files = prndrngd(&PRIV(sess)->rnd, 0, 1);
  p->modify_small_files = prndrngd(&PRIV(sess)->rnd, 0, 1);
  return 0;
}
//int torment_step(torment_session_t *sess);
//int torment_run(torment_session_t *sess);
void torment_cleanup_session(torment_session_t *sess) {
  priv_t *p = (priv_t *)sess->_priv;
  cfs_free(PRIV(sess)->cfs);
  free(p);
}
