/*
 * test.c
 *
 *  Created on: Aug 10, 2017
 *      Author: petera
 */


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>

#include "spfs.h"
#include "spfs_lowlevel.h"
#include "spfs_fs.h"
#include "spfs_gc.h"
#include "spfs_file.h"
#include "spfs_journal.h"
#include "spfs_cache.h"
#include "spfs_dbg.h"
#include "spif_emul.h"

#include "sillyfs.h"
#include "cfs_sillyfs.h"
#include "cfs_posix.h"

int spif_hdl;

#if SPFS_TEST == 0
#error this file can only be compiled with SPFS_TEST = 1
#endif

#define _COLS   (uint32_t)16
static void _dump_data(uint8_t *buf, uint32_t len, uint32_t addr, const char *prefix) {
  uint32_t i;
  while (len) {
    uint8_t col_offs = (addr&(_COLS-1));
    uint8_t entries = spfs_min(_COLS-col_offs, len);
    if (prefix) printf("%s", prefix);
    printf("%08x  " , addr & (~(_COLS-1)));
    for (i=0; i < col_offs;i++) printf("__ ");
    for (i=0; i < entries; i++) printf("%02x ", buf[i]);
    for (i=0; col_offs==0 && i < _COLS-entries; i++) printf("__ ");
    printf("  ");
    for (i=0; i < col_offs;i++) printf("_");
    for (i=0; i < entries; i++) printf("%c", buf[i] >= 32 && buf[i] < 127 ? buf[i] : '.');
    for (i=0; col_offs==0 && i < _COLS-entries; i++) printf("_");
    buf += entries;
    addr += entries;
    len -= entries;
    printf("\n");
  }
}
static int _diff_data(uint8_t *buf1, uint8_t *buf2, uint32_t len, uint32_t addr, const char *prefix) {
  uint32_t i;
  int res = 0;
  while (len) {
    uint8_t col_offs = (addr&(_COLS-1));
    uint8_t entries = spfs_min(_COLS-col_offs, len);
    if (prefix) printf("%s", prefix);
    printf("%08x  " , addr & (~(_COLS-1)));
    for (i=0; i < col_offs;i++) printf("__ ");
    for (i=0; i < entries; i++) {
      if (buf2[i] != buf1[i]) {
        res = -1;
        printf("\x1b[33;1m");
      }
      printf("%02x ", buf1[i]);
      if (buf2[i] != buf1[i]) printf("\x1b[0m");
    }
    for (i=0; col_offs==0 && i < _COLS-entries; i++) printf("__ ");
    printf("  ");
    for (i=0; i < col_offs;i++) printf("__ ");
    for (i=0; i < entries; i++) {
      if (buf2[i] != buf1[i]) printf("\x1b[32;1m");
      printf("%02x ", buf2[i]);
      if (buf2[i] != buf1[i]) printf("\x1b[0m");
    }
    for (i=0; col_offs==0 && i < _COLS-entries; i++) printf("__ ");
    printf("  ");
    for (i=0; i < col_offs;i++) printf("_");
    for (i=0; i < entries; i++) {
      if (buf2[i] != buf1[i]) {
        printf("\x1b[33;1m");
        printf("%c", buf1[i] >= 32 && buf1[i] < 127 ? buf1[i] : '.');
        printf("\x1b[32;1m");
        printf("%c", buf2[i] >= 32 && buf2[i] < 127 ? buf2[i] : '.');
        printf("\x1b[0m");
      } else {
        printf("%c ", buf1[i] >= 32 && buf1[i] < 127 ? buf1[i] : '.');
      }
    }
    for (i=0; col_offs==0 && i < _COLS-entries; i++) printf("_");
    buf1 += entries;
    buf2 += entries;
    addr += entries;
    len -= entries;
    printf("\n");
  }
  return res;
}

static int _dump_file(spfs_t *fs, spfs_fd_t *fd, uint32_t offs, uint32_t len) {
  uint8_t *buf = malloc(len);
  int res = spfs_file_read(fs, fd, offs, len, buf);
  if (res >= 0) {
    _dump_data(buf, len, offs, "  ");
  }
  free(buf);
  return res;
}

static void _dump_page_ldata(spfs_t *fs, pix_t lpix) {
  uint8_t *buf;
  spif_em_dbg_get_buffer(spif_hdl, &buf);
  uint32_t ix = SPFS_LPIX2ADDR(fs, lpix);
  _dump_data(&buf[ix], SPFS_CFG_LPAGE_SZ(fs), ix, "  ");
}

static void _dump_page_ddata(spfs_t *fs, pix_t dpix) {
  _dump_page_ldata(fs, _dpix2lpix(fs, dpix));
}

static int _diff_page_ldata(spfs_t *fs, pix_t lpix1, pix_t lpix2) {
  uint8_t *buf;
  spif_em_dbg_get_buffer(spif_hdl, &buf);
  uint32_t ix1 = SPFS_LPIX2ADDR(fs, lpix1);
  uint32_t ix2 = SPFS_LPIX2ADDR(fs, lpix2);
  return _diff_data(&buf[ix1], &buf[ix2], SPFS_CFG_LPAGE_SZ(fs), ix1, "  ");
}

static int _diff_page_ddata(spfs_t *fs, pix_t dpix1, pix_t dpix2) {
  return _diff_page_ldata(fs, _dpix2lpix(fs, dpix1), _dpix2lpix(fs, dpix2));
}

static int fs_hal_erase(spfs_t *fs, uint32_t addr, uint32_t size, uint32_t flags) {
  (void)fs;
  (void)flags;
  //printf("erase %08x sz %08x\n", addr, size);
  uint32_t spif_em_flags = 0;
  int res = spif_em_erase(spif_hdl, addr, size, spif_em_flags);
  if (res) {
    printf("ER ERR: %s @ addr %08x\n", spif_em_strerr(res), spif_em_dbg_get_err_addr(spif_hdl));
  }
  return res;
}

static int fs_hal_write(spfs_t *fs, uint32_t addr, const uint8_t *buf, uint32_t size, uint32_t flags) {
  (void)fs;
//  printf("write %08x\n", addr);
//  uint32_t i;
//  for (i = 0; i < size; i++) printf("%02x ", buf[i]);
//  printf("\n");
  uint32_t spif_em_flags = 0;
  if (flags & _SPFS_HAL_WR_FL_OVERWRITE) spif_em_flags |= SPIF_EM_FL_WR_NO_FREECHECK;
  if (flags & _SPFS_HAL_WR_FL_IGNORE_BITS) spif_em_flags |= SPIF_EM_FL_WR_NO_BITCHECK;
  int res = spif_em_write(spif_hdl, addr, buf, size, spif_em_flags);
  if (res) {
    printf("WR ERR: %s @ addr %08x\n", spif_em_strerr(res), spif_em_dbg_get_err_addr(spif_hdl));
  }
  return res;
}

static int fs_hal_read(spfs_t *fs, uint32_t addr, uint8_t *buf, uint32_t size, uint32_t flags) {
  (void)fs;
  (void)flags;
  //printf("<<< read %08x sz %08x\n", addr, size);
  uint32_t spif_em_flags = 0;
  int res = spif_em_read(spif_hdl, addr, buf, size, spif_em_flags);
  if (res) {
    printf("RD ERR: %s @ addr %08x\n", spif_em_strerr(res), spif_em_dbg_get_err_addr(spif_hdl));
  }
  return res;

}

static void *_spfs_mallocs[_SPFS_MEM_TYPES];

static void * fs_alloc(spfs_t *fs, spfs_mem_type_t type, uint32_t req_size, uint32_t *acq_size) {
  (void)fs;
  void *m = malloc(req_size);
  _spfs_mallocs[type] = m;
  *acq_size = req_size;
  return m;
}

static void fs_free(void) {
  int i;
  for (i = 0; i < _SPFS_MEM_TYPES; i++) {
    free(_spfs_mallocs[i]);
  }
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  printf("start\n");

  spif_em_cfg spif_cfg;
  spif_cfg.blk_sz_mask = spif_em_get_blockmask(SPFS_T_CFG_PBLK_SZ);
  spif_cfg.page_size = 256;
  spif_cfg.size = 65536*1024;
  spif_hdl = spif_em_create(&spif_cfg);

  spfs_t _fs;
  spfs_t *fs = &_fs;
  spfs_cfg_t fscfg;
  fscfg.malloc = fs_alloc;
  fscfg.read = fs_hal_read;
  fscfg.write = fs_hal_write;
  fscfg.erase = fs_hal_erase;
#if SPFS_CFG_DYNAMIC
  fscfg.pflash_sz = SPFS_T_CFG_PSZ;
  fscfg.lblk_sz = SPFS_T_CFG_LBLK_SZ;
  fscfg.lpage_sz = SPFS_T_CFG_LPAGE_SZ;
  fscfg.pflash_addr_offs = SPFS_T_CFG_PADDR_OFFS;
  fscfg.pblk_sz = SPFS_T_CFG_PBLK_SZ;
#endif
  int res = spfs_config(fs, &fscfg, NULL);
  if (res) return -1;
  res = spfs_format(fs);
  if (res) return -1;
  res = spfs_mount(fs, 0, 4, 16);
  if (res) return -1;

  spfs_pixhdr_t pixhdr;

  {
    res = spfs_file_find(fs, "nisse", NULL, &pixhdr);
    res = spfs_file_create(fs, NULL, "nisse");
    res = spfs_file_find(fs, "nisse", NULL, &pixhdr);
    res = spfs_file_find(fs, "nisse2", NULL, &pixhdr);
    res = spfs_file_create(fs, NULL, "nisse2");
    res = spfs_file_find(fs, "nisse2", NULL, &pixhdr);
  }

  res = spfs_file_find(fs, "nisse", NULL, &pixhdr);
  res = spfs_file_find(fs, "nisse2", NULL, &pixhdr);

  spfs_jour_entry j;
  j.ongoing = 1;
  j.id = SPFS_JOUR_ID_FCREAT;
  j.fcreat.id = 0x0001;
  res = spfs_journal_add(fs, &j);
  if (res) goto end;
  res = spfs_journal_complete(fs, j.id);
  if (res) goto end;
  j.id = SPFS_JOUR_ID_FMOD;
  j.fmod.id = 0x0002;
  res = spfs_journal_add(fs, &j);
  if (res) goto end;
  res = spfs_journal_complete(fs, j.id);
  if (res) goto end;
  j.id = SPFS_JOUR_ID_FRENAME;
  j.frename.src_id = 0x0003;
  j.frename.dst_id = 0x0004;
  res = spfs_journal_add(fs, &j);
  if (res) goto end;
  res = spfs_journal_complete(fs, j.id);
  if (res) goto end;
  j.id = SPFS_JOUR_ID_FRM;
  j.frm.id = 0x0005;
  res = spfs_journal_add(fs, &j);
  if (res) goto end;
  res = spfs_journal_complete(fs, j.id);
  if (res) goto end;
  j.id = SPFS_JOUR_ID_FTRUNC;
  j.ftrunc.id = 0x0006;
  j.ftrunc.sz = 12345678;
  res = spfs_journal_add(fs, &j);
  if (res) goto end;

  fs->run.journal.bitoffs = 0;
  res = spfs_journal_read(fs);
  res = spfs_journal_complete(fs, j.id);
  if (res) goto end;
  res = spfs_journal_read(fs);
  if (res) goto end;


  spfs_cfg_t cfg2;
  cfg2.read = fs_hal_read;
  spfs_probe(&cfg2, 0, 1000000);

  spfs_fd_t *fd;
  _fd_claim(fs, &fd);
  res = spfs_file_create(fs, fd, "somefile");
  if (res) goto end;
  uint8_t buf[10000];
  {
    uint32_t i;
    for (i = 0; i < sizeof(buf); i++) buf[i] = i;
  }
  printf("\n\nwrite\n\n");
  res = spfs_file_write(fs, fd, 0, 400, buf);
  printf("%d\n", res);
  if (res < 0) goto end;
  _dump_file(fs, fd, 0, fd->fi.size);

  printf("\n\nwrite sensitive\n\n");
  fd->fd_flags = SPFS_O_SENSITIVE;
  res = spfs_file_write(fs, fd, 200, 10000, buf);
  printf("%d\n", res);
  if (res < 0) goto end;

  _dump_file(fs, fd, 500,500);
  uint8_t buf2[500];
  memset(buf2, 0xf0, 500);
  printf("\n\nrewrite\n\n");
  fd->fd_flags = SPFS_O_REWRITE;
  res = spfs_file_write(fs, fd, 500, 500, buf2);
  printf("%d\n", res);
  if (res < 0) goto end;
  _dump_file(fs, fd, 490,520);

  printf("\n\ntrunc\n\n");
  res = spfs_file_truncate(fs, fd, 100);
  printf("%d\n", res);
  if (res < 0) goto end;
  _dump_file(fs, fd, 0, 124);

  res = spfs_file_remove(fs, fd);
  printf("%d\n", res);
  if (res < 0) goto end;

  end:
  spfs_gc(fs);
//  spfs_dump(fs, SPFS_DUMP_NO_DELE | SPFS_DUMP_NO_FREE | SPFS_DUMP_PAGE_DATA);
  spfs_dump(fs, SPFS_DUMP_LS);
  //_diff_page_ddata(fs, 2,3);
  fs_free();
  printf("end, res %d %s\n", res, spfs_strerror(res));


#if 0

  sfs_t* sfs = sfs_config(1200);
  res = sfs_mount(sfs);
  if (res < 0) printf("err:%d\n", res);
  int fh = sfs_open(sfs,  "nisse65", SFS_O_CREAT | SFS_O_RDWR);
  if (fh < 0) printf("err:%d\n", fh);
  res = sfs_close(sfs,  fh);
  if (res < 0) printf("err:%d\n", res);

  fh = sfs_open(sfs,  "nisse43", SFS_O_CREAT | SFS_O_RDWR);
  if (fh < 0) printf("err:%d\n", fh);
  res = sfs_close(sfs,  fh);
  if (res < 0) printf("err:%d\n", res);

  fh = sfs_open(sfs,  "nisse32", SFS_O_CREAT | SFS_O_RDWR);
  if (fh < 0) printf("err:%d\n", fh);
  res = sfs_close(sfs,  fh);
  if (res < 0) printf("err:%d\n", res);

  fh = sfs_open(sfs,  "nisse", SFS_O_CREAT | SFS_O_RDWR);
  if (fh < 0) printf("err:%d\n", fh);
  uint8_t sfsbuf[10000];
  {
    uint32_t i;
    for (i = 0; i < sizeof(sfsbuf); i++) sfsbuf[i] = i;
  }
  res = sfs_write(sfs,  fh, sfsbuf, 1000);
  if (res < 0) printf("err:%d\n", res);
  res = sfs_lseek(sfs,  fh, 500, SFS_SEEK_SET);
  if (res < 0) printf("err:%d\n", res);
  res = sfs_write(sfs,  fh, &sfsbuf[1000], 1000);
  if (res < 0) printf("err:%d\n", res);
  printf("size:%d\n", sfs_ftell(sfs,  fh));
  uint8_t bufr[10000];
  res = sfs_lseek(sfs,  fh, 0, SFS_SEEK_SET);
  if (res < 0) printf("err:%d\n", res);
  res = sfs_read(sfs,  fh, bufr, 2000);
  if (res < 0) printf("err:%d\n", res);
  sfs_DIR dir;
  struct sfs_dirent *de;
  sfs_opendir(sfs,  &dir, "/");
  while ((de = sfs_readdir(sfs,  &dir))) {
    printf("%s %d\n", de->name, de->size);
  }

  res = sfs_remove(sfs,  "nisse43");
  if (res < 0) printf("err:%d\n", res);

  sfs_opendir(sfs,  &dir, "/");
  while ((de = sfs_readdir(sfs,  &dir))) {
    printf("%s %d\n", de->name, de->size);
  }

  sfs_destroy(sfs);

#endif
  {
    cfs_t cfs;
    csfs_link(&cfs, 10000);
    cposfs_link(&cfs, NULL);
    cfs_mount(&cfs);
    cfs_validate_file(&cfs,"noexist");

    int cfd = cfs_open(&cfs, "test", CFS_O_RDWR | CFS_O_CREAT | CFS_O_APPEND);
    cfs_write(&cfs, cfd, buf, 10000);
    cfs_close(&cfs, cfd);
    cfd = cfs_open(&cfs, "bob", CFS_O_RDWR | CFS_O_CREAT | CFS_O_APPEND);
    cfs_close(&cfs, cfd);
    cfd = cfs_open(&cfs, "adam", CFS_O_RDWR | CFS_O_CREAT | CFS_O_APPEND);
    cfs_close(&cfs, cfd);
    cfd = cfs_open(&cfs, "bob2", CFS_O_RDWR | CFS_O_CREAT | CFS_O_APPEND);
    cfs_close(&cfs, cfd);
    cfd = cfs_open(&cfs, "cheryl", CFS_O_RDWR | CFS_O_CREAT | CFS_O_APPEND);
    cfs_close(&cfs, cfd);
    cfd = cfs_open(&cfs, "duderino", CFS_O_RDWR | CFS_O_CREAT | CFS_O_APPEND);
    cfs_close(&cfs, cfd);
    cfs_validate_file(&cfs,"test");
    cfs_validate_fs(&cfs);
    cfs_free(&cfs);

  }

  return 0;
}
