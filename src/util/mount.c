/*
 * mount spfs utility
 *
 * Mounts a spfs image to a folder using fuse.
 * Depends on libfuse.
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include "spfs.h"
#include "spfs_lowlevel.h"
#include "spfs_file.h"
#include "spfs_fs.h"
#include "spfs_dbg.h"

#define fdbg(f, ...)

typedef struct {
  spfs_t _fs;
  spfs_t *fs;
  void *spfs_mallocs[_SPFS_MEM_TYPES];
  int img_fd;
  uint8_t *img;
  uint32_t img_sz;
  pthread_mutex_t *spfs_mutex;
} st_t;

static st_t _st;
static st_t *st = &_st;



static void cleanup(st_t *tst) {
  int i;
  for (i = 0; i < _SPFS_MEM_TYPES; i++) {
    free(tst->spfs_mallocs[i]);
  }
  if (tst->img != MAP_FAILED) munmap(tst->img, tst->img_sz);
  if (tst->img_fd > 0) close(tst->img_fd);
}

static void *fuse_spfs_init(struct fuse_conn_info *conn) {
  fdbg("%s\n", __func__);
  return st;
}

static void fuse_spfs_destroy(void *private_data) {
  fdbg("%s\n", __func__);
  cleanup((st_t *)private_data);
}

static int fuse_spfs_getattr(const char *path, struct stat *stbuf) {
  fdbg("%s\n", __func__);
  int res = 0;
  memset(stbuf, 0, sizeof(struct stat));
  if (strcmp(path, "/") == 0) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
  } else {
    struct spfs_stat buf;
    res = SPFS_stat(st->fs, path+1, &buf);
    if (res == SPFS_OK) {
      stbuf->st_mode = S_IFREG | 0640;
      stbuf->st_nlink = 1;
      stbuf->st_size = buf.size;
    } else {
      res = -ENOENT;
    }
  }
  return res;
}

static int fuse_spfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
  fdbg("%s\n", __func__);
  (void) offset;
  (void) fi;

  if (strcmp(path, "/")) return -ENOENT;

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);

  spfs_DIR d;
  struct spfs_dirent *de;
  (void)SPFS_opendir(st->fs, &d, "/");
  while ((de = SPFS_readdir(st->fs, &d))!=NULL) {
    filler(buf, de->s.name, NULL, 0);
  }
  (void)SPFS_closedir(st->fs, &d);

  return 0;
}

static int fuse_spfs_open(const char *path, struct fuse_file_info *fi) {
  fdbg("%s\n", __func__);
  int oflags = 0;
  if ((fi->flags & O_ACCMODE) == O_RDONLY) oflags = SPFS_O_RDONLY;
  else if ((fi->flags & O_ACCMODE) == O_WRONLY) oflags = SPFS_O_WRONLY;
  else if ((fi->flags & O_ACCMODE) == O_RDWR) oflags = SPFS_O_RDWR;
  int fh = SPFS_open(st->fs, path+1, oflags, 0);
  if (fh < 0) return -ENOENT;
  fi->fh = fh;
  return 0;
}

static int fuse_spfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  fdbg("%s\n", __func__);
  int res;
  res = SPFS_lseek(st->fs, fi->fh, offset, SPFS_SEEK_SET);
  if (res < 0) return -EIO;
  res = SPFS_read(st->fs, fi->fh, (uint8_t *)buf, size);
  if (res < 0) return -EIO;
  return res;
}

static int fuse_spfs_write(const char *name, const char *buf, size_t size, off_t offset,
        struct fuse_file_info *fi) {
  fdbg("%s %s fh:%d offs:%d size:%d\n", __func__, name, fi->fh, offset, size);
  int res;
  res = SPFS_lseek(st->fs, fi->fh, offset, SPFS_SEEK_SET);
  if (res < 0) return -EIO;
  res = SPFS_write(st->fs, fi->fh, (const uint8_t *)buf, size);
  if (res < 0) return -EIO;
  return res;
}


static int fuse_spfs_release(const char *path, struct fuse_file_info *fi) {
  fdbg("%s fh:%d\n", __func__, fi->fh);
  int res = SPFS_close(st->fs, fi->fh);
  if (res < 0) return -EIO;
  return res;
}

static int fuse_spfs_fsync(const char *name, int what, struct fuse_file_info *fsync) {
  fdbg("%s %s\n", __func__, name);
  return 0;
}

static int fuse_spfs_create(const char *name, mode_t mode, struct fuse_file_info *fi) {
  fdbg("%s %s\n", __func__, name);
  int res = SPFS_create(st->fs, name+1);
  if (res < 0) return -EIO;
  fi->fh = res;
  return 0;
}

static int fuse_spfs_access(const char *name, int mode) {
  fdbg("%s %s %d\n", __func__, name, mode);
  return 0;
}
static int fuse_spfs_unlink(const char *name) {
  fdbg("%s\n", __func__);
  int res = SPFS_remove(st->fs, name+1);
  if (res < 0) return -EIO;
  return 0;
}

static int fuse_spfs_flush(const char *path, struct fuse_file_info *fi) {
  fdbg("%s fh:%d\n", __func__, fi->fh);
  return 0;
}

static int fuse_spfs_truncate(const char *path, off_t offset) {
  fdbg("%s path:%s offs:%d\n", __func__, path, offset);
  int res = SPFS_truncate(st->fs, path+1, offset);
  if (res < 0) return -EIO;
  return 0;
}

static int fuse_spfs_ftruncate(const char *path, off_t offset, struct fuse_file_info *fi) {
  fdbg("%s fh:%d\n", __func__, fi->fh);
  int res = SPFS_ftruncate(st->fs, fi->fh, offset);
  if (res < 0) return -EIO;
  return 0;
}

static int fuse_spfs_fsyncdir(const char *path, int what, struct fuse_file_info *fi) {
  printf("fuse spfs fsyncdir %s\n", path);
  return 0;
}

static int fuse_spfs_statfs(const char *path, struct statvfs *s) {
  fdbg("%s %s\n", __func__, path);
  // The 'f_frsize', 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
  s->f_bsize = SPFS_CFG_LPAGE_SZ(st->fs);
  //s->f_frsize
  s->f_blocks = SPFS_LBLK_CNT(st->fs) * SPFS_LPAGES_P_BLK(st->fs);
  s->f_bfree = st->fs->run.pfree + st->fs->run.pdele;
  s->f_bavail = s->f_bfree;
  s->f_namemax = SPFS_CFG_FILE_NAME_SZ;
  return 0;
}

static struct fuse_operations spfs_operations = {
    .getattr = fuse_spfs_getattr,
    //.readlink = ,
    //.getdir = ,
    //.mknod = ,
    //.mkdir = ,
    .unlink = fuse_spfs_unlink,
    //.rmdir = ,
    //.symlink = ,
    //.rename = , TODO
    //.link = ,
    //.chmod = ,
    //.chown = ,
    .truncate = fuse_spfs_truncate,
    //.utime = ,
    .open = fuse_spfs_open,
    .read = fuse_spfs_read,
    .write = fuse_spfs_write,
    .statfs = fuse_spfs_statfs,
    .flush = fuse_spfs_flush,
    .release = fuse_spfs_release,
    .fsync = fuse_spfs_fsync,
    //.setxattr = ,
    //.getxattr = ,
    //.listxattr = ,
    //.removexattr = ,
    //.opendir = ,
    .readdir = fuse_spfs_readdir,
    //.releasedir = ,
    .fsyncdir = fuse_spfs_fsyncdir,
    .init = fuse_spfs_init,
    .destroy = fuse_spfs_destroy,
    .access = fuse_spfs_access,
    .create = fuse_spfs_create,
    .ftruncate = fuse_spfs_ftruncate,
    //.fgetattr = ,
    //.lock = ,
    //.utimens = ,
    //.bmap = ,
    //.ioctl = ,
    //.poll = ,
    //.write_buf = ,
    //.read_buf = ,
    //.flock = ,
    //.fallocate = ,
};

static void *spfs_alloc(spfs_t *lfs, spfs_mem_type_t type, uint32_t req_size, uint32_t *acq_size) {
  (void)lfs;
  void *m = malloc(req_size);
  ((st_t *)lfs->user)->spfs_mallocs[type] = m;
  *acq_size = req_size;
  return m;
}

static int spfs_hal_read(spfs_t *lfs, uint32_t addr, uint8_t *buf, uint32_t size, uint32_t flags) {
  memcpy(buf, &((st_t *)lfs->user)->img[addr], size);
  return 0;
}

static int spfs_hal_write(spfs_t *lfs, uint32_t addr, const uint8_t *buf, uint32_t size, uint32_t flags) {
  uint8_t *dst = &((st_t *)lfs->user)->img[addr];
  while (size--) {
    *dst++ &= *buf++;
  }
  return 0;
}

static int spfs_hal_erase(spfs_t *lfs, uint32_t addr, uint32_t size, uint32_t flags) {
  memset(&((st_t *)lfs->user)->img[addr], 0xff, size);
  return 0;
}

// override spfs_util_lock.c definition
void spfs_lock(void *fs) {
  pthread_mutex_lock(((st_t *)((spfs_t*)fs)->user)->spfs_mutex);
}

// override spfs_util_lock.c definition
void spfs_unlock(void *fs) {
  pthread_mutex_unlock(((st_t *)((spfs_t*)fs)->user)->spfs_mutex);
}


int main(int argc, char *argv[]) {
  printf("spfs mount [fs v%d.%d.%d]\n",
      (SPFS_VERSION >> 12), (SPFS_VERSION >> 8) & 0xf, SPFS_VERSION & 0xff);
  if (argc < 2) {
    printf("Mounts a spfs image to a folder using fuse.\n");
    printf("usage:\n%s <image file> <mount path> (fuse params)\n", argv[0]);
    return 1;
  }

  st = malloc(sizeof(st_t));
  if (st == NULL) {
    fdbg("Out of memory\n");
    exit(EXIT_FAILURE);
  }
  memset(st, 0, sizeof(st_t));
  st->fs = &st->_fs;

  st->img_fd = open(argv[1], O_RDWR);
  if (st->img_fd < 0) {
    perror("Cannot open file");
    exit(EXIT_FAILURE);
  }

  st->img_sz = lseek(st->img_fd, 0, SEEK_END);
  lseek(st->img_fd, 0, SEEK_SET);

  st->img = mmap(0, st->img_sz, PROT_READ | PROT_WRITE, MAP_SHARED, st->img_fd, 0);
  if (st->img == MAP_FAILED) {
    cleanup(st);
    perror("Cannot map file");
    exit(EXIT_FAILURE);
  }

  st->fs->user = st;
  st->spfs_mutex = spfs_lock_get_mutex();

  spfs_cfg_t fscfg;
  fscfg.malloc = spfs_alloc;
  fscfg.read = spfs_hal_read;
  fscfg.write = spfs_hal_write;
  fscfg.erase = spfs_hal_erase;
  fscfg.filehandle_offset = 0;
  int res = spfs_probe(&fscfg, 0, st->img_sz, st);
  if (res < 0) {
    cleanup(st);
    printf("Cannot find file system in %s: %s\n", argv[1], spfs_strerror(res));
    exit(EXIT_FAILURE);
  }
  res = spfs_config(st->fs, &fscfg, st);
  if (res < 0) {
    cleanup(st);
    printf("Cannot configure file system in %s: %s\n", argv[1], spfs_strerror(res));
    exit(EXIT_FAILURE);
  }
  res = spfs_mount(st->fs, 0, 8, 16);
  if (res < 0) {
    cleanup(st);
    printf("Cannot mount file system in %s: %s\n", argv[1], spfs_strerror(res));
    exit(EXIT_FAILURE);
  }
  printf("spfs file system mounted\nsize:%dkB, log block size:%dkB, log page size:%d bytes\n",
      SPFS_CFG_LBLK_SZ(st->fs) * SPFS_LBLK_CNT(st->fs) / 1024, SPFS_CFG_LBLK_SZ(st->fs) / 1024, SPFS_CFG_LPAGE_SZ(st->fs));
  printf("used:%d free:%d\n",
      st->fs->run.pused * SPFS_CFG_LPAGE_SZ(st->fs),
      (st->fs->run.pfree+st->fs->run.pdele) * SPFS_CFG_LPAGE_SZ(st->fs));

  return fuse_main(argc-1, &argv[1], &spfs_operations, st);
}
