#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#include "spfs_fs.h"
#include "spfs_lowlevel.h"
#include "spfs_file.h"
#include "spfs_dbg.h"
#include "spif_emul.h"


#define IMG_FILE "spfs.img"
#define ERRPRI(f, ...) do {fprintf(stderr, "ERR:" f, ## __VA_ARGS__); } while (0)
#define ERREND(f, ...) do {fprintf(stderr, "ERR:" f, ## __VA_ARGS__); goto end;} while (0)
#define _SPFS_HAL_WR_FL_MEM_OR      (0xfe)
#define _SPFS_HAL_WR_FL_MEM_SET     (0xff)

static int spif_hdl;

static int fs_hal_erase(spfs_t *fs, uint32_t addr, uint32_t size, uint32_t flags) {
  (void)fs;
  (void)flags;
  uint32_t spif_em_flags = 0;
  int res = spif_em_erase(spif_hdl, addr, size, spif_em_flags);
  if (res) ERRPRI("ER ERR: %s @ addr %08x\n", spif_em_strerr(res), spif_em_dbg_get_err_addr(spif_hdl));
  return res;
}

static int fs_hal_write(spfs_t *fs, uint32_t addr, const uint8_t *buf, uint32_t size, uint32_t flags) {
  (void)fs;
  uint32_t spif_em_flags = 0;
  if (flags & _SPFS_HAL_WR_FL_OVERWRITE) spif_em_flags |= SPIF_EM_FL_WR_NO_FREECHECK;
  if (flags & _SPFS_HAL_WR_FL_IGNORE_BITS) spif_em_flags |= SPIF_EM_FL_WR_NO_BITCHECK;
  if (flags == _SPFS_HAL_WR_FL_MEM_OR)
    spif_em_flags =
        SPIF_EM_FL_WR_NO_FREECHECK |
        SPIF_EM_FL_WR_NO_BITCHECK |
        SPIF_EM_FL_WR_OR;
  if (flags == _SPFS_HAL_WR_FL_MEM_SET)
    spif_em_flags =
        SPIF_EM_FL_WR_NO_FREECHECK |
        SPIF_EM_FL_WR_NO_BITCHECK |
        SPIF_EM_FL_WR_SET;
  int res = spif_em_write(spif_hdl, addr, buf, size, spif_em_flags);
  if (res) ERRPRI("WR ERR: %s @ addr %08x\n", spif_em_strerr(res), spif_em_dbg_get_err_addr(spif_hdl));
  return res;
}

static int fs_hal_read(spfs_t *fs, uint32_t addr, uint8_t *buf, uint32_t size, uint32_t flags) {
  (void)fs;
  (void)flags;
  uint32_t spif_em_flags = 0;
  int res = spif_em_read(spif_hdl, addr, buf, size, spif_em_flags);
  if (res) ERRPRI("RD ERR: %s @ addr %08x\n", spif_em_strerr(res), spif_em_dbg_get_err_addr(spif_hdl));
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

static int page_free_v(spfs_t *fs, uint32_t lu_entry, spfs_vis_info_t *info, void *varg) {
  id_t id = spfs_signext(lu_entry >> SPFS_LU_FLAG_BITS, SPFS_BITS_ID(fs));
  if (id == SPFS_IDDELE) {
    uint8_t page_ones[SPFS_CFG_LPAGE_SZ(fs)];
    memset(page_ones, 0xff, SPFS_CFG_LPAGE_SZ(fs));
    uint8_t lu_ones[5] = {0,0,0,0,0}; // max 32 bits + 0..7 bits offs

    pix_t lpix =  _dpix2lpix(fs, info->dpix);

    // *** write lu
    // get physical address of relevant lu page
    uint32_t lu_page_addr = SPFS_LPIX2ADDR(fs, SPFS_LPIX2LLUPIX(fs, lpix));
    // get bit index of entry in relevant lu page
    uint32_t bitpos_lu_entry = SPFS_LPIX2LUENT(fs, lpix) * (SPFS_BITS_ID(fs) + SPFS_LU_FLAG_BITS);

    // write id to memory only comprising the change on medium
    bstr8 bs;
    bstr8_init(&bs, lu_ones);
    bstr8_setp(&bs, bitpos_lu_entry % 8);
    bstr8_wr(&bs, SPFS_BITS_ID(fs) + SPFS_LU_FLAG_BITS, SPFS_IDFREE);

    // get address and length of bytes in lu page to actually update
    uint32_t lu_entry_addr = lu_page_addr + bitpos_lu_entry/8;
    if (SPFS_LPIX_FIRSTBLKLU(fs, lpix)) {
      lu_entry_addr += SPFS_BLK_HDR_SZ;
    }
    uint32_t lu_entry_len = spfs_ceil(bstr8_getp(&bs), 8);
    // and write
    int res = fs_hal_write(fs, lu_entry_addr, lu_ones, lu_entry_len, _SPFS_HAL_WR_FL_MEM_OR);
    if (res) return res;

    // *** write page
    uint32_t addr = SPFS_DPIX2ADDR(fs, info->dpix);
    res = fs_hal_write(fs, addr, page_ones, SPFS_CFG_LPAGE_SZ(fs), _SPFS_HAL_WR_FL_MEM_OR);
    if (res) return res;
    fs->run.pdele--;
    fs->run.pfree++;
  }
  return SPFS_VIS_CONT;
}

int main(int argc, char **argv) {
  spfs_t _fs;
  spfs_t *fs = &_fs;
  DIR *d = NULL;
  int fd = 0;

  int err = -1;
  printf("spfs mkimg [fs v%d.%d.%d]\n\n",
      (SPFS_VERSION >> 12), (SPFS_VERSION >> 8) & 0xf, SPFS_VERSION & 0xff);
  if (argc < 4) {
    printf("usage:\n%s <log block size> <nbr of log blocks> <log page size> (<data dir>)\n", argv[0]);
    return err;
  }

  memset(_spfs_mallocs, 0, sizeof(_spfs_mallocs));

  uint32_t blk_sz = (int)strtol(argv[1], NULL, 0);
  uint32_t blk_cnt = (int)strtol(argv[2], NULL, 0);
  uint32_t page_sz = (int)strtol(argv[3], NULL, 0);
  uint32_t fs_sz = blk_sz * blk_cnt;

  char path[PATH_MAX];
  if (argc > 4) {
    realpath(argv[4], path);
  } else {
    realpath(".", path);
  }

  // config format mount

  spif_em_cfg spif_cfg;
  spif_cfg.blk_sz_mask = spif_em_get_blockmask(blk_sz);
  spif_cfg.page_size = page_sz;
  spif_cfg.size = fs_sz;
  spif_hdl = spif_em_create(&spif_cfg);
  if (spif_hdl < 0) ERREND("could not create emulated spi flash area, err %d\n", spif_hdl);

  spfs_cfg_t fscfg;
  fscfg.malloc = fs_alloc;
  fscfg.read = fs_hal_read;
  fscfg.write = fs_hal_write;
  fscfg.erase = fs_hal_erase;
  fscfg.pflash_sz = fs_sz;
  fscfg.lblk_sz = blk_sz;
  fscfg.lpage_sz = page_sz;
  fscfg.pflash_addr_offs = 0;
  fscfg.pblk_sz = blk_sz;

  int res = spfs_config(fs, &fscfg, NULL);
  if (res) ERREND("config fail:%d %s\n", res, spfs_strerror(res));
  res = spfs_format(fs);
  if (res) ERREND("format fail:%d %s\n", res, spfs_strerror(res));
  res = spfs_mount(fs, 0, 4, 16);
  if (res) ERREND("mount fail:%d %s\n", res, spfs_strerror(res));

  // scan directory, add files
  printf("adding files from %s\n", path);
  struct dirent *de;
  d = opendir(path);
  if (d == NULL) {
    err = errno;
    ERREND("could not open %s\n%s\n", path, strerror(errno));
  }
  while ((de = readdir(d))) {
    char fpath[PATH_MAX];
    snprintf(fpath, PATH_MAX, "%s/%s", path, de->d_name);
    struct stat path_stat;
    res = stat(fpath, &path_stat);
    if (res) {
      err = errno;
      ERREND("could not stat %s\n%s\n", fpath, strerror(errno));
    }
    if (!S_ISREG(path_stat.st_mode)) continue;
    printf("  %s (%ld bytes)\n", de->d_name, path_stat.st_size);

    spfs_fd_t *spfs_fd;
    _fd_claim(fs, &spfs_fd);
    res = spfs_file_create(fs, spfs_fd, de->d_name);
    spfs_fd->fd_flags |= SPFS_O_SENSITIVE;
    if (res) ERREND("create fail:%d %s\n", res, spfs_strerror(res));

    fd = open(fpath, O_RDONLY);
    if (fd < 0) {
      err = errno;
      ERREND("could not open %s\n%s\n", fpath, strerror(errno));
    }

    uint8_t buf[4096];
    int rd;
    uint32_t offs = 0;
    while ((rd = read(fd, buf, sizeof(buf))) > 0) {
      res = spfs_file_write(fs, spfs_fd, offs, rd, buf);
      if (res != rd) ERREND("write fail:%d %s\n", res, spfs_strerror(res));
      offs += rd;
    }

    if (rd < 0) {
      err = errno;
      ERREND("could not read %s\n%s\n", fpath, strerror(errno));
    }

    _fd_release(fs, spfs_fd);
    close(fd);
    fd = 0;
  }

  // scan spfs fs, free deleted pages
  res = spfs_page_visit(fs, 0, 0, NULL, page_free_v, 0);
  if (res == -SPFS_ERR_VIS_END) res = 0;
  if (res) ERREND("freeing pages fail:%d %s\n", res, spfs_strerror(res));

  // reset block headers erase count
  bix_t lbix;
  bix_t lbix_end = SPFS_LBLK_CNT(fs);
  spfs_bhdr_t b;

  for (lbix = 0; res == SPFS_OK && lbix < lbix_end; lbix++) {
    res = _bhdr_rd(fs, lbix, &b);
    if (res) ERREND("read block hdr fail:%d %s\n", res, spfs_strerror(res));
    b.era_cnt = 0;
    res = _bhdr_write(fs, lbix, b.dbix, 0, 0, _SPFS_HAL_WR_FL_MEM_SET);
    if (res) ERREND("write block hdr fail:%d %s\n", res, spfs_strerror(res));
  } // per block


  double used = fs->run.pused * SPFS_CFG_LPAGE_SZ(fs);
  double free = fs->run.pfree * SPFS_CFG_LPAGE_SZ(fs);
  double dele = fs->run.pdele * SPFS_CFG_LPAGE_SZ(fs);

  printf("used:%0.1fkB free:%0.1fkB\n",
      used/1024, (free+dele) / 1024);


  // dump spfs fs
  printf("writing image "IMG_FILE"\n");
  fd = open("spfs.img", O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, S_IRUSR | S_IWUSR);
  if (fd < 0) {
    err = errno;
    ERREND("could not open %s\n%s\n", IMG_FILE, strerror(errno));
  }

  uint8_t *data;
  spif_em_dbg_get_buffer(spif_hdl, &data);
  uint32_t offs = 0;
  while (offs < fs_sz) {
    int wr = write(fd, data, fs_sz - offs);
    if (wr < 0) {
      err = errno;
      ERREND("could not write %s\n%s\n", IMG_FILE, strerror(errno));
    }
    offs += wr;
    data += wr;
  }

  close(fd);

//  spfs_dump(fs, SPFS_DUMP_LS);
//  spfs_dump(fs, SPFS_DUMP_PAGE_DATA);

  end:
  if (fd > 0) close(fd);
  if (d) closedir(d);
  fs_free();
  return err;
}
