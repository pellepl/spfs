/*
 * spfs_bits.h
 *
 *  Created on: Aug 21, 2017
 *      Author: petera
 */

#ifndef _SPFS_BITS_H_
#define _SPFS_BITS_H_

#include "spfs_compile_cfg.h"

#if SPFS_TEST
#define _SPFS_STATIC
#else
#if SPFS_MONOLITH
#define _SPFS_STATIC static
#else
#define _SPFS_STATIC
#endif
#endif

#if !SPFS_CFG_DYNAMIC
/**
 * In non-dynamic builds, the physcial and logical config is set by
 * following defines.
 */
#ifndef SPFS_CFG_PSZ
#error SPFS_CFG_PSZ(fs) must be defined to physical filesystem size in bytes
#endif
#ifndef SPFS_CFG_PADDR_OFFS
#error SPFS_CFG_PADDR_OFFS(fs) must be defined to physical start address of filesystem
#endif
#ifndef SPFS_CFG_LBLK_SZ
#error SPFS_CFG_LBLK_SZ(fs) must be defined to logical block size in bytes
#endif
#ifndef SPFS_CFG_PBLK_SZ
#error SPFS_CFG_PBLK_SZ(fs) must be defined to physical block size in bytes
#endif
#ifndef SPFS_CFG_LPAGE_SZ
#error SPFS_CFG_LPAGE_SZ(fs) must be defined to logical page size in bytes
#endif
#endif //!SPFS_CFG_DYNAMIC


#define BITMANIO_STORAGE_BITS 32
#define BITMANIO_HEADER
#include "bitmanio.h"
#define BITMANIO_STORAGE_BITS BYTE
#define BITMANIO_HEADER
#include "bitmanio.h"

// bitmanio: nisi digitos meos
typedef bitmanio_array32_t          barr;
typedef bitmanio_stream32_t         bstr;
typedef bitmanio_bytearray_t        barr8;
typedef bitmanio_bytestream_t       bstr8;
#define bstr_init(_b, _mem)         bitmanio_init_stream32((_b), (_mem))
#define bstr_rd(_b, _bits)          bitmanio_read32((_b), (_bits))
#define bstr_wr(_b, _bits, _val)    bitmanio_write32((_b), (_val), (_bits))
#define bstr_wrz(_b, _bits, _val)   bitmanio_writez32((_b), (_val), (_bits))
#define bstr_setp(_b, _bits)        bitmanio_setpos32((_b), (_bits))
#define bstr_getp(_b)               bitmanio_getpos32((_b))
#define barr_init(_b, _mem, _bits)  bitmanio_init_array32((_b), (_mem), (_bits))
#define barr_get(_b, _ix)           bitmanio_get32((_b), (_ix))
#define barr_set(_b, _ix, _val)     bitmanio_set32((_b), (_ix), (_val))
#define bstr8_init(_b, _mem)        bitmanio_init_stream((_b), (_mem))
#define bstr8_rd(_b, _bits)         bitmanio_read((_b), (_bits))
#define bstr8_wr(_b, _bits, _val)   bitmanio_write((_b), (_val), (_bits))
#define bstr8_setp(_b, _bits)       bitmanio_setpos((_b), (_bits))
#define bstr8_getp(_b)              bitmanio_getpos((_b))
#define barr8_init(_b, _mem, _bits) bitmanio_init_array((_b), (_mem), (_bits))
#define barr8_get(_b, _ix)          bitmanio_get((_b), (_ix))
#define barr8_set(_b, _ix, _val)    bitmanio_set((_b), (_ix), (_val))

#endif /* _SPFS_BITS_H_ */
