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
#include <signal.h>
#include <linux/memfd.h>
#include <sys/syscall.h>

#include "spfs.h"
#include "spfs_lowlevel.h"
#include "spfs_file.h"
#include "spfs_fs.h"
#include "spfs_dbg.h"

#define fdbg(f, ...)

#define FILE_DBG_EXPORT ".spfs.dbg.export"
#define FILE_DBG_DUMP   ".spfs.dbg.dump"
#define FH_DBG_EXPORT   9999123
#define FH_DBG_DUMP     9999321

typedef struct {
  spfs_t _fs;
  spfs_t *fs;
  void *spfs_mallocs[_SPFS_MEM_TYPES];
  int img_fd;
  uint8_t *img;
  uint32_t img_sz;
  pthread_mutex_t *spfs_mutex;
  uint8_t ro;
  int memfd;
} st_t;

static st_t _st;
static st_t *st = &_st;

static int sys_memfd_create(const char *name)
{
  return syscall(__NR_memfd_create, name,  0);
}

#if SPFS_DUMP
static int _get_dump_size(spfs_t *fs) {
  ftruncate(st->memfd, 0);
  spfs_dump(fs, 0);
  fflush(__dumpfd);
  return lseek(st->memfd, 0, SEEK_END);
}
static void _read_dump(spfs_t *fs, void *dst, uint32_t off, uint32_t sz) {
  ftruncate(st->memfd, 0);
  spfs_dump(fs, 0);
  fflush(__dumpfd);
  lseek(st->memfd, off, SEEK_SET);
  read(st->memfd, dst, sz);
}
#endif
#if SPFS_EXPORT
static int _get_export_size(spfs_t *fs) {
  ftruncate(st->memfd, 0);
  spfs_export(fs, 0);
  fflush(__dumpfd);
  return lseek(st->memfd, 0, SEEK_END);
}
static void _read_export(spfs_t *fs, void *dst, uint32_t off, uint32_t sz) {
  ftruncate(st->memfd, 0);
  spfs_export(fs, 0);
  fflush(__dumpfd);
  lseek(st->memfd, off, SEEK_SET);
  read(st->memfd, dst, sz);
}
#endif

static int err_spfs2posix(int err) {
  switch (err) {
  case SPFS_OK:
    return 0;
  case -SPFS_ERR_ARG:
  case -SPFS_ERR_UNCONFIGURED:
  case -SPFS_ERR_CFG_SZ_NOT_REPR:
  case -SPFS_ERR_CFG_SZ_NOT_ALIGNED:
  case -SPFS_ERR_CFG_LPAGE_SZ:
  case -SPFS_ERR_CFG_MEM_NOT_ALIGNED:
  case -SPFS_ERR_CFG_MEM_SZ:
  case -SPFS_ERR_CFG_MOUNT_MISMATCH:
    return -EINVAL;
  case -SPFS_ERR_OUT_OF_PAGES:
    return -ENOSPC;
  case -SPFS_ERR_FILE_NOT_FOUND:
    return -ENOENT;
  case -SPFS_ERR_OUT_OF_FILEDESCRIPTORS:
    return -EMFILE;
  case -SPFS_ERR_BAD_FILEDESCRIPTOR:
  case -SPFS_ERR_FILE_CLOSED:
    return -EBADFD;
  case -SPFS_ERR_NAME_CONFLICT:
    return -EEXIST;
  case -SPFS_ERR_NOT_READABLE:
  case -SPFS_ERR_NOT_WRITABLE:
    return -EACCES;
  default:
    return -EIO;
  }
}

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
  st_t *sts = (st_t *)private_data;
  fdbg("%s\n", __func__);
  cleanup(sts);
}

static int fuse_spfs_getattr(const char *path, struct stat *stbuf) {
  fdbg("%s\n", __func__);
  int res = SPFS_OK;
  memset(stbuf, 0, sizeof(struct stat));
  if (strcmp(path, "/") == 0) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
#if SPFS_EXPORT
  } else if (st->memfd > 0 && strcmp(path, "/"FILE_DBG_EXPORT) == 0) {
    stbuf->st_mode = S_IFREG | 0440;
    stbuf->st_nlink = 1;
    stbuf->st_size = _get_export_size(st->fs);
#endif
#if SPFS_DUMP
  } else if (st->memfd > 0 && strcmp(path, "/"FILE_DBG_DUMP) == 0) {
    stbuf->st_mode = S_IFREG | 0440;
    stbuf->st_nlink = 1;
    stbuf->st_size =  _get_dump_size(st->fs);
#endif
  } else {
    struct spfs_stat buf;
    res = SPFS_stat(st->fs, path+1, &buf);
    if (res == SPFS_OK) {
      stbuf->st_mode = S_IFREG | (st->ro ? 0440 : 0640);
      stbuf->st_nlink = 1;
      stbuf->st_size = buf.size;
    }
  }
  return err_spfs2posix(res);
;
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

#if SPFS_EXPORT
  if (st->memfd > 0) filler(buf, FILE_DBG_EXPORT, NULL, 0);
#endif
#if SPFS_DUMP
  if (st->memfd > 0) filler(buf, FILE_DBG_DUMP, NULL, 0);
#endif
  return 0;
}

static int fuse_spfs_open(const char *path, struct fuse_file_info *fi) {
  fdbg("%s\n", __func__);
#if SPFS_DUMP
  if (strcmp("/"FILE_DBG_DUMP, path)== 0) {
    fi->fh = FH_DBG_DUMP;
    return 0;
  }
#endif
#if SPFS_EXPORT
  if (strcmp("/"FILE_DBG_EXPORT, path)== 0) {
    fi->fh = FH_DBG_EXPORT;
    return 0;
  }
#endif
  int oflags = 0;
  if ((fi->flags & O_ACCMODE) == O_RDONLY) oflags = SPFS_O_RDONLY;
  else if ((fi->flags & O_ACCMODE) == O_WRONLY) oflags = SPFS_O_WRONLY;
  else if ((fi->flags & O_ACCMODE) == O_RDWR) oflags = SPFS_O_RDWR;
  int fh = SPFS_open(st->fs, path+1, oflags, 0);
  if (fh < 0) return err_spfs2posix(fh);
  fi->fh = fh;
  return 0;
}

static int fuse_spfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  fdbg("%s\n", __func__);
#if SPFS_DUMP
  if (strcmp("/"FILE_DBG_DUMP, path)== 0 && fi->fh == FH_DBG_DUMP) {
    _read_dump(st->fs, buf, offset, size);
    return size;
  }
#endif
#if SPFS_EXPORT
  if (strcmp("/"FILE_DBG_EXPORT, path)== 0 && fi->fh == FH_DBG_EXPORT) {
    _read_export(st->fs, buf, offset, size);
    return size;
  }
#endif
  int res;
  res = SPFS_lseek(st->fs, fi->fh, offset, SPFS_SEEK_SET);
  if (res < 0) return err_spfs2posix(res);
  res = SPFS_read(st->fs, fi->fh, (uint8_t *)buf, size);
  if (res < 0) return err_spfs2posix(res);
  return res;
}

static int fuse_spfs_write(const char *name, const char *buf, size_t size, off_t offset,
        struct fuse_file_info *fi) {
  fdbg("%s %s fh:%d offs:%d size:%d\n", __func__, name, fi->fh, offset, size);
  if (st->ro) return -EROFS;
  int res;
  res = SPFS_lseek(st->fs, fi->fh, offset, SPFS_SEEK_SET);
  if (res < 0) return err_spfs2posix(res);
  res = SPFS_write(st->fs, fi->fh, (const uint8_t *)buf, size);
  if (res < 0) return err_spfs2posix(res);
  return res;
}


static int fuse_spfs_release(const char *path, struct fuse_file_info *fi) {
  fdbg("%s fh:%d\n", __func__, fi->fh);
  if (fi->fh == FH_DBG_EXPORT || fi->fh == FH_DBG_DUMP) return 0;
  int res = SPFS_close(st->fs, fi->fh);
  return err_spfs2posix(res);
}

static int fuse_spfs_fsync(const char *name, int isdatadir, struct fuse_file_info *fsync) {
  fdbg("%s %s\n", __func__, name);
  return 0;
}

static int fuse_spfs_create(const char *name, mode_t mode, struct fuse_file_info *fi) {
  fdbg("%s %s\n", __func__, name);
  if (st->ro) return -EROFS;
  int res = SPFS_creat(st->fs, name+1);
  if (res < 0) return err_spfs2posix(res);
  fi->fh = res;
  return 0;
}

static int fuse_spfs_access(const char *name, int mode) {
  fdbg("%s %s %d\n", __func__, name, mode);
  return 0;
}
static int fuse_spfs_unlink(const char *name) {
  fdbg("%s\n", __func__);
  if (st->ro) return -EROFS;
  int res = SPFS_remove(st->fs, name+1);
  return err_spfs2posix(res);
}

static int fuse_spfs_flush(const char *path, struct fuse_file_info *fi) {
  fdbg("%s fh:%d\n", __func__, fi->fh);
  return 0;
}

static int fuse_spfs_truncate(const char *path, off_t offset) {
  fdbg("%s path:%s offs:%d\n", __func__, path, offset);
  if (st->ro) return -EROFS;
  int res = SPFS_truncate(st->fs, path+1, offset);
  return err_spfs2posix(res);
}

static int fuse_spfs_ftruncate(const char *path, off_t offset, struct fuse_file_info *fi) {
  fdbg("%s fh:%d\n", __func__, fi->fh);
  if (st->ro) return -EROFS;
  int res = SPFS_ftruncate(st->fs, fi->fh, offset);
  return err_spfs2posix(res);
}

static int fuse_spfs_fsyncdir(const char *path, int isdatasync, struct fuse_file_info *fi) {
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
  if (st->ro) return -EACCES;
  uint8_t *dst = &((st_t *)lfs->user)->img[addr];
  while (size--) {
    *dst++ &= *buf++;
  }
  return 0;
}

static int spfs_hal_erase(spfs_t *lfs, uint32_t addr, uint32_t size, uint32_t flags) {
  if (st->ro) return -EACCES;
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
    printf("usage:\n%s <image file> (--read_only) <mount path> (fuse params)\n", argv[0]);
    return 1;
  }

  uint8_t nxt_arg = 1;
  uint8_t ro = 0;
  st = malloc(sizeof(st_t));
  if (st == NULL) {
    fdbg("Out of memory\n");
    exit(EXIT_FAILURE);
  }
  memset(st, 0, sizeof(st_t));
  st->fs = &st->_fs;

  if (strcmp("--read-only", argv[2])==0) {
    ro = 1;
    nxt_arg = 2;
  }
  st->img_fd = open(argv[1], ro ? O_RDONLY : O_RDWR);
  if (st->img_fd < 0) {
    perror("Cannot open file");
    exit(EXIT_FAILURE);
  }

  st->img_sz = lseek(st->img_fd, 0, SEEK_END);
  lseek(st->img_fd, 0, SEEK_SET);

  st->img = mmap(0, st->img_sz, PROT_READ | (ro ? 0 : PROT_WRITE), MAP_SHARED, st->img_fd, 0);
  if (st->img == MAP_FAILED) {
    cleanup(st);
    perror("Cannot map file");
    exit(EXIT_FAILURE);
  }

  st->fs->user = st;
  st->spfs_mutex = spfs_lock_get_mutex();
  st->ro = ro;

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

  printf("spfs file system mounted%s\nsize:%dkB, log block size:%dkB, log page size:%d bytes\n",
      ro ? " read-only":"",
      SPFS_CFG_LBLK_SZ(st->fs) * SPFS_LBLK_CNT(st->fs) / 1024, SPFS_CFG_LBLK_SZ(st->fs) / 1024, SPFS_CFG_LPAGE_SZ(st->fs));
  printf("used:%d free:%d\n",
      st->fs->run.pused * SPFS_CFG_LPAGE_SZ(st->fs),
      (st->fs->run.pfree+st->fs->run.pdele) * SPFS_CFG_LPAGE_SZ(st->fs));

  st->memfd = sys_memfd_create("spfs_output");
  if (st->memfd > 0) {
    __dumpfd = fdopen(st->memfd, "a+");
  }

  return fuse_main(argc-nxt_arg, &argv[nxt_arg], &spfs_operations, st);
}
