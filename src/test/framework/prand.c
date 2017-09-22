/*
 * prand.c
 *
 *  Created on: Sep 22, 2017
 *      Author: petera
 */

#include "prand.h"

#define POLYx 0x80000E74
#define POLYy 0x80000EA6

static int step(prnd_t *p) {
  if (p->x & 1) p->x = (p->x >> 1) ^ POLYx;
  else          p->x = (p->x >> 1);
  if (p->y & 1) p->y = (p->y >> 1) ^ POLYy;
  else          p->y = (p->y >> 1);

  return (p->x ^ p->y) & 1;
}
void prnd_seed(prnd_t *p, uint32_t seed) {
  if ((p->x = 0x20070515 ^ seed) == 0) p->x = 0x20140318;
  if ((p->y = 0x20090129 ^ seed) == 0) p->y = 0x20140318;
}
uint8_t prndbool(prnd_t *p) {
  return step(p);
}
uint8_t prndbyte(prnd_t *p) {
  uint8_t x = 0;
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  return x;
}
uint32_t prnd(prnd_t *p) {
  uint32_t x = 0;
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  x = (x<<1) | step(p);
  return x;
}

int32_t prndrngi(prnd_t *p, int32_t min, int32_t max) {
  int32_t ran = max-min;
  double rnd = (double)prnd(p)/(double)0xffffffffUL;
  return rnd * ran + min;
}

double prndrngd(prnd_t *p, double min, double max) {
  double ran = max-min;
  double rnd = (double)prnd(p)/(double)0xffffffffUL;
  return rnd * ran + min;
}
