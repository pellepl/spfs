/*
 * combinedfs.h
 *
 *  Created on: Sep 14, 2017
 *      Author: petera
 */

#ifndef _COMBINEDFS_H_
#define _COMBINEDFS_H_

#include <stdint.h>

#define MAP_BUCKS       8

typedef struct buckelem_s {
  uint32_t key;
  uint32_t val;
  struct buckelem_s *_next;
} buckelem_t;

typedef struct {
  buckelem_t *bucks[MAP_BUCKS];
} map_t;

struct cfs_stat {
  char name[256];
  uint32_t size;
};

#define cfs_dirent cfs_stat
struct cfs_dir {
  struct cfs_dirent de;
  void *user;
};
typedef struct cfs_dir cfs_DIR;


typedef int (* cfs_mount_fn_t)(void *wrap, void *cfg);
typedef int (* cfs_umount_fn_t)(void *wrap);
typedef int (* cfs_creat_fn_t)(void *wrap, const char *path);
typedef int (* cfs_open_fn_t)(void *wrap, const char *path, uint32_t oflags);
typedef int (* cfs_read_fn_t)(void *wrap, int fh, void *buf, uint32_t sz);
typedef int (* cfs_write_fn_t)(void *wrap, int fh, const void *buf, uint32_t sz);
typedef int (* cfs_truncate_fn_t)(void *wrap, const char *path, uint32_t sz);
typedef int (* cfs_ftruncate_fn_t)(void *wrap, int fh, uint32_t sz);
typedef int (* cfs_close_fn_t)(void *wrap, int fh);
typedef int (* cfs_lseek_fn_t)(void *wrap, int fh, int offs, int whence);
typedef int (* cfs_ftell_fn_t)(void *wrap, int fh);
typedef int (* cfs_stat_fn_t)(void *wrap, const char *path, struct cfs_stat *buf);
typedef int (* cfs_remove_fn_t)(void *wrap, const char *path);
typedef int (* cfs_rename_fn_t)(void *wrap, const char *old_path, const char *new_path);

typedef int (* cfs_opendir_fn_t)(void *wrap, cfs_DIR *d, const char *path);
typedef int (* cfs_readdir_fn_t)(void *wrap, cfs_DIR *d);
typedef int (* cfs_closedir_fn_t)(void *wrap, cfs_DIR *d);

typedef void (* cfs_free_t)(void *wrap);
typedef uint32_t (* cfs_translate_oflags_fn_t)(void *wrap, uint32_t oflags);
typedef uint32_t (* cfs_translate_whence_fn_t)(void *wrap, uint32_t whence);
typedef int (* cfs_translate_err_fn_t)(void *wrap, int err);

typedef struct {
  cfs_mount_fn_t mount;
  cfs_umount_fn_t umount;
  cfs_creat_fn_t creat;
  cfs_open_fn_t open;
  cfs_read_fn_t read;
  cfs_write_fn_t write;
  cfs_truncate_fn_t truncate;
  cfs_ftruncate_fn_t ftruncate;
  cfs_close_fn_t close;
  cfs_lseek_fn_t lseek;
  cfs_ftell_fn_t ftell;
  cfs_stat_fn_t stat;
  cfs_remove_fn_t remove;
  cfs_rename_fn_t rename;

  cfs_opendir_fn_t opendir;
  cfs_readdir_fn_t readdir;
  cfs_closedir_fn_t closedir;

  cfs_translate_oflags_fn_t translate_oflags;
  cfs_translate_whence_fn_t translate_whence;
  cfs_translate_err_fn_t translate_err;
  cfs_free_t free;
  void *fs;
  void *fs_cfg;
  char name[256];
} cfs_fs_t;

typedef struct _cfs_entry_s {
  cfs_fs_t *wrap;
  map_t fd_map;
  int last_res;
  int last_err;
  struct _cfs_entry_s *_next;
} _cfs_entry_t;

typedef struct {
  int cfs_fh;
  int fs_cnt;
  _cfs_entry_t *fs_head;
} cfs_t;

#define CFS_O_RDONLY      (1<<0)
#define CFS_O_WRONLY      (1<<1)
#define CFS_O_RDWR        (1<<2)
#define CFS_O_APPEND      (1<<3)
#define CFS_O_CREAT       (1<<4)
#define CFS_O_EXCL        (1<<5)
#define CFS_O_TRUNC       (1<<6)

#define CFS_SEEK_SET      0
#define CFS_SEEK_CUR      1
#define CFS_SEEK_END      2

void cfs_free(cfs_t *cfs);
void cfs_link_fs(cfs_t *cfs, const char *name, cfs_fs_t *fs);
void *cfs_get_fs_by_name(cfs_t *cfs, const char *name);
int cfs_validate_file(cfs_t *cfs, const char *path);
int cfs_validate_fs(cfs_t *cfs);
int cfs_validate_ls(cfs_t *cfs);

int cfs_mount(cfs_t *fs);
int cfs_umount(cfs_t *fs);
int cfs_creat(cfs_t *fs, const char *path);
int cfs_open(cfs_t *fs, const char *path, uint32_t oflags);
int cfs_read(cfs_t *fs, int fh, uint8_t **buf, uint32_t sz);
int cfs_write(cfs_t *fs, int fh, const void *buf, uint32_t sz);
int cfs_truncate(cfs_t *fs, const char *path, uint32_t sz);
int cfs_ftruncate(cfs_t *fs, int fh, uint32_t sz);
int cfs_close(cfs_t *fs, int fh);
int cfs_lseek(cfs_t *fs, int fh, int offs, int whence);
int cfs_ftell(cfs_t *fs, int fh);
int cfs_stat(cfs_t *fs, const char *path, struct cfs_stat **buf);
int cfs_remove(cfs_t *fs, const char *path);
int cfs_rename(cfs_t *fs, const char *old_path, const char *new_path);
int cfs_opendir(cfs_t *fs, cfs_DIR **d, const char *path);
int cfs_readdir(cfs_t *fs, cfs_DIR **d);
int cfs_closedir(cfs_t *cfs, cfs_DIR **d);

int cfs_mountc(cfs_t *fs, int *ret);
int cfs_umountc(cfs_t *fs, int *ret);
int cfs_creatc(cfs_t *fs, int *ret, const char *path);
int cfs_openc(cfs_t *fs, int *ret, const char *path, uint32_t oflags);
int cfs_readc(cfs_t *fs, int *ret, int fh, uint8_t **buf, uint32_t sz);
int cfs_writec(cfs_t *fs, int *ret, int fh, const void *buf, uint32_t sz);
int cfs_truncatec(cfs_t *fs, int *ret, const char *path, uint32_t sz);
int cfs_ftruncatec(cfs_t *fs, int *ret, int fh, uint32_t sz);
int cfs_closec(cfs_t *fs, int *ret, int fh);
int cfs_lseekc(cfs_t *fs, int *ret, int fh, int offs, int whence);
int cfs_ftellc(cfs_t *fs, int *ret, int fh);
int cfs_statc(cfs_t *fs, int *ret, const char *path, struct cfs_stat **buf);
int cfs_removec(cfs_t *fs, int *ret, const char *path);
int cfs_renamec(cfs_t *fs, int *ret, const char *old_path, const char *new_path);
int cfs_opendirc(cfs_t *fs, int *ret, cfs_DIR **d, const char *path);
int cfs_readdirc(cfs_t *fs, int *ret, cfs_DIR **d);
int cfs_closedirc(cfs_t *cfs, int *ret, cfs_DIR **d);

#endif /* _COMBINEDFS_H_ */
