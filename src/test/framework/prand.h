/*
 * prand.h
 *
 *  Created on: Sep 22, 2017
 *      Author: petera
 */

#ifndef _PRAND_H_
#define _PRAND_H_

#include <stdint.h>

typedef struct prnd {
  uint32_t x, y, z;
} prnd_t;
uint32_t prnd(prnd_t *p);
uint8_t prndbool(prnd_t *p);
uint8_t prndbyte(prnd_t *p);
int32_t prndrngi(prnd_t *p, int32_t min, int32_t max);
double prndrngd(prnd_t *p, double min, double max);
void prnd_seed(prnd_t *p, uint32_t seed);

#endif /* _PRAND_H_ */
