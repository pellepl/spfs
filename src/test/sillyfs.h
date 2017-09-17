/*
 * sillyfs.h
 *
 *  Created on: Sep 14, 2017
 *      Author: petera
 */

#ifndef _SILLYFS_H_
#define _SILLYFS_H_

#include <stdint.h>

#define SFS_CHUNK     4096

typedef struct sfs_file {
  char *name;
  uint32_t f_size;
  uint32_t m_size;
  uint8_t *data;
  struct sfs_file *_prev;
  struct sfs_file *_next;
} sfs_file_t;

typedef struct sfs_fd {
  int fh;
  sfs_file_t *f;
  uint32_t oflags;
  uint32_t offs;
  struct sfs_fd *_prev;
  struct sfs_fd *_next;
} sfs_fd_t;

typedef struct {
  int fh_cnt;
  uint32_t size;
  uint32_t used_size;
  sfs_file_t *fhead;
  sfs_fd_t *fdhead;
} sfs_t;

struct sfs_stat {
  char name[256];
  uint32_t size;
};

#define sfs_dirent sfs_stat
#define sfs_DIR struct sfs_dir
struct sfs_dir {
  struct sfs_dirent de;
  sfs_file_t *_f;
};

#define SFS_O_RDONLY      (1<<0)
#define SFS_O_WRONLY      (1<<1)
#define SFS_O_RDWR        (SFS_O_RDONLY | SFS_O_WRONLY)
#define SFS_O_APPEND      (1<<2)
#define SFS_O_CREAT       (1<<3)
#define SFS_O_EXCL        (1<<4)
#define SFS_O_TRUNC       (1<<5)

#define SFS_SEEK_SET      0
#define SFS_SEEK_CUR      1
#define SFS_SEEK_END      2

sfs_t *sfs_config(uint32_t max_size);
int sfs_mount(sfs_t *fs);
void sfs_format(sfs_t *fs);
void sfs_destroy(sfs_t *fs);
int sfs_creat(sfs_t *fs, const char *path);
int sfs_open(sfs_t *fs, const char *path, uint32_t oflags);
int sfs_read(sfs_t *fs, int fh, void *buf, uint32_t sz);
int sfs_write(sfs_t *fs, int fh, const void *buf, uint32_t sz);
int sfs_truncate(sfs_t *fs, const char *path, uint32_t sz);
int sfs_ftruncate(sfs_t *fs, int fh, uint32_t sz);
int sfs_close(sfs_t *fs, int fh);
int sfs_lseek(sfs_t *fs, int fh, int offs, int whence);
int sfs_ftell(sfs_t *fs, int fh);
int sfs_stat(sfs_t *fs, const char *path, struct sfs_stat *buf);
int sfs_remove(sfs_t *fs, const char *path);
int sfs_rename(sfs_t *fs, const char *old_path, const char *new_path);
int sfs_opendir(sfs_t *fs, sfs_DIR *d, const char *path);
struct sfs_dirent *sfs_readdir(sfs_t *fs, sfs_DIR *d);

#endif /* _SILLYFS_H_ */
