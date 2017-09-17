/*
 * spfs_journal.h
 *
 *  Created on: 23 aug 2017
 *      Author: petera
 */

#ifndef _SPFS_JOURNAL_H_
#define _SPFS_JOURNAL_H_

#include "spfs_compile_cfg.h"
#include "spfs.h"
#include "spfs_lowlevel.h"

// journal entry format :
// bits 0123....x
//      FIII....D
// F = flag, where 1 means started and zero means finished
// I = journal task id
// ... = task args
// D = must be 0, or else the actual journal entry persisting was interrupted

#define SPFS_JOUR_BITS_ID       (3)

#define SPFS_JOUR_ID_FREE       (0x7) // must have all bits set
#define SPFS_JOUR_ID_FCREAT     (0x6)
#define SPFS_JOUR_ID_FMOD       (0x5)
#define SPFS_JOUR_ID_FTRUNC     (0x4)
#define SPFS_JOUR_ID_FRM        (0x3)
#define SPFS_JOUR_ID_FRENAME    (0x2)

#define SPFS_JOUR_ENTRY_MAX_SZ  (1+4+4)

typedef struct {
  uint8_t ongoing;
  uint8_t id;
  union {
    // create new original file operation with file id
    // if file with id already exists, it must be removed
    struct {
      id_t id;
    } fcreat;

    // file modification of file with given id
    // will check that length and indices are valid and remove
    // any orphaned pages
    struct {
      id_t id;
    } fmod;

    // file truncation of file with given id to given length
    // will check that length and indices are valid and remove
    // any orphaned pages
    struct {
      id_t id;
      uint32_t sz;
    } ftrunc;

    // full file removal of file with given id
    // cleanses all data of given file
    struct {
      id_t id;
    } frm;

    // rename of file to an existing file
    // remove original file and renames given file to same name
    struct {
      id_t src_id;
      id_t dst_id;
    } frename;
  };
  uint8_t unwritten;
} spfs_jour_entry;

_SPFS_STATIC int spfs_journal_add(spfs_t *fs, spfs_jour_entry *jentry);
_SPFS_STATIC int spfs_journal_complete(spfs_t *fs, uint8_t jentry_id);
_SPFS_STATIC int spfs_journal_read(spfs_t *fs);

_SPFS_STATIC int _journal_create(spfs_t *fs);
_SPFS_STATIC int _journal_parse(spfs_t *fs, spfs_jour_entry *jentry, bstr8 *bs);

#endif /* _SPFS_JOURNAL_H_ */
