#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "spfs_fs.h"
#include "spfs_lowlevel.h"
#include "spfs_dbg.h"

#define BL_MAGIC_SZ             SPFS_BL_MAGIC_SZ
#define EXTRA_IDS               SPFS_LU_EXTRA_IDS
#define LU_EXTRA_ID_BITS        SPFS_LU_FLAG_BITS
#define PAGE_FLAG_BITS          SPFS_PHDR_FLAG_BITS
#define OIX_NAME_LEN            SPFS_CFG_FILE_NAME_SZ
#define OIX_HDR_EXTRA_BITINFO   SPFS_CFG_FILE_META_SZ*8
#define ERROR(x) do {fprintf(stderr, "ERR:" x); goto end;} while (0)


// .. simply wonderful
#define _LOG2(v) ( \
  (v)&0x80000000UL?32:((v)&0x40000000UL?31:((v)&0x20000000UL?30:((v)&0x10000000UL?29:( \
  (v)&0x08000000UL?28:((v)&0x04000000UL?27:((v)&0x02000000UL?26:((v)&0x01000000UL?25:( \
  (v)&0x00800000UL?24:((v)&0x00400000UL?23:((v)&0x00200000UL?22:((v)&0x00100000UL?21:( \
  (v)&0x00080000UL?20:((v)&0x00040000UL?19:((v)&0x00020000UL?18:((v)&0x00010000UL?17:( \
  (v)&0x00008000UL?16:((v)&0x00004000UL?15:((v)&0x00002000UL?14:((v)&0x00001000UL?13:( \
  (v)&0x00000800UL?12:((v)&0x00000400UL?11:((v)&0x00000200UL?10:((v)&0x00000100UL? 9:( \
  (v)&0x00000080UL? 8:((v)&0x00000040UL? 7:((v)&0x00000020UL? 6:((v)&0x00000010UL? 5:( \
  (v)&0x00000008UL? 4:((v)&0x00000004UL? 3:((v)&0x00000002UL? 2:((v)&0x00000001UL? 1:0 \
      ))))))))))))))))))))))))))))))))

int main(int argc, char **argv) {
  spfs_t _fs;
  spfs_t *fs = &_fs;
  int err = -1;
  printf("spfs calculator [fs v%d.%d.%d]\n\n",
      (SPFS_VERSION >> 12), (SPFS_VERSION >> 8) & 0xf, SPFS_VERSION & 0xff);
  if (argc < 4) {
    printf("usage:\n%s <log block size> <nbr of log blocks> <log page size>\n", argv[0]);
    return err;
  }

  uint32_t blk_sz = (int)strtol(argv[1], NULL, 0);
  uint32_t blk_cnt = (int)strtol(argv[2], NULL, 0);
  uint32_t page_sz = (int)strtol(argv[3], NULL, 0);
  uint32_t fs_sz = blk_sz * blk_cnt;
  printf("File system size:      %dB (%dKB)\n", fs_sz, fs_sz/1024);
  printf("Logical block size:    %dB (%dKB)\n", blk_sz, blk_sz/1024);
  printf("Logical blocks:        %d\n", blk_cnt);
  printf("Logical page size:     %dB\n", page_sz);

  spfs_cfg_t cfg = {
      .pflash_sz = blk_sz * blk_cnt,
      .pflash_addr_offs = 0,
      .pblk_sz = blk_sz,
      .lblk_sz = blk_sz,
      .lpage_sz = page_sz
  };

  int res = spfs_config(fs, &cfg, 0);
  if (res) {
    printf("config fail:%d %s\n", res, spfs_strerror(res));
    ERROR("configuration error\n");
  }

  printf("\n");


  printf("Number of log pages:   %d\n", fs_sz / page_sz);
  uint32_t pages_per_block = SPFS_LPAGES_P_BLK(fs);
  printf("Pages per block:       %d\n", pages_per_block);


  uint32_t id_cnt = SPFS_MAX_ID(fs);
  printf("Needed ids:            %d\n", id_cnt);

  uint32_t b_id = SPFS_BITS_ID(fs);
  printf("Bits for id:           %db => max id %d\n", b_id, 1<<b_id);
  if ((uint32_t)(1<<b_id) < id_cnt) ERROR("calc error for needed id bits\n");


  uint32_t b_lu = SPFS_BITS_ID(fs) + SPFS_LU_FLAG_BITS;
  printf("Bits for lu entry:     %db\n", b_lu);

  uint32_t b_stray;
  uint32_t ent_lu0 = SPFS_LU_ENT_CNT(fs, 0);
  printf("Entries in LU0 page:   %d\n", ent_lu0);

  uint32_t ent_lu = SPFS_LU_ENT_CNT(fs, 1);
  printf("Entries in LU page:    %d\n", ent_lu);

  uint32_t pages_lu = SPFS_LUPAGES_P_BLK(fs);
  uint32_t pdata = pages_per_block - pages_lu;
  uint32_t ent_lus = pdata;
  printf("Needed LU entries:     %d\n", ent_lus);

  printf("Needed LU pages:       %d\n", pages_lu);

  uint32_t e_stray = (ent_lu0 + (pages_lu > 1 ? ent_lu * (pages_lu-1) : 0)) - ent_lus;
  printf("Stray LU entries:      %d (%dB)\n",  e_stray, spfs_ceil(e_stray * b_lu, 8));
  if (e_stray >= ent_lu) ERROR("calc error for LU pages\n");

  uint32_t b_page_hdr = SPFS_PHDR_SZ(fs) * 8;
  uint32_t page_hdr_sz = spfs_ceil(b_page_hdr, 8);
  printf("Page header size:      %dB, %db\n", page_hdr_sz, b_page_hdr);

  uint32_t b_oix_hdr = SPFS_PIXHDR_SZ(fs) * 8;
  uint32_t oix_hdr_sz = spfs_ceil(b_oix_hdr, 8);
  printf("IX0 hdr size:          %dB, %db\n", oix_hdr_sz, b_oix_hdr);

  uint32_t ent_ix0 = SPFS_LU_ENT_CNT(fs, 0);
  printf("Entries in IX0 page:   %d\n", ent_ix0);

  uint32_t ent_ix = SPFS_LU_ENT_CNT(fs, 1);
  printf("Entries in IX page:    %d\n", ent_ix);


  printf("\n");


  uint32_t pages_data = SPFS_DPAGES_MAX(fs);
  uint32_t page_data_sz = SPFS_DPAGE_SZ(fs);
  printf("User data pages:       %d => %dB (%dKB)\n", pages_data, pages_data*page_data_sz, pages_data*page_data_sz/1024);

  printf("Max nbr of files:      %d\n", pages_data/2);

  uint32_t ix0_file_sz = ent_ix0 * (page_data_sz);
  printf("IX0 file span size:    %dB (%dKB)\n", ix0_file_sz, ix0_file_sz/1024);



  printf("\n");

  printf("Work buffer:           %dB\n", spfs_align(page_sz*2, 4));

  uint32_t b_blk = _LOG2(blk_cnt);
  printf("Block IX entry size:   %db\n", b_blk);

  uint32_t blk_ix_sz = spfs_ceil(_LOG2(blk_cnt) * blk_cnt, 8);
  printf("Block IX buffer:       %dB\n", spfs_align(blk_ix_sz,4));


  printf("\n");


  static const uint32_t T[32] = {
      0x0,0x4,0x8,0xe,0x18,0x2a,0x48,0x7f,
      0xe3,0x199,0x2e8,0x555,0x9d8,0x1249,0x2222,0x4000,
      0x7878,0xe38e,0x1af28,0x33333,0x61862,0xba2e9,0x1642c8,0x2aaaab,
      0x51eb85,0x9d89d9,0x12f684c,0x2492492,0x469ee58,0x8888889,0x10842108,0x20000000};

  int passes = 0;
  int pass_bits[16];
  int x,i;
  i = id_cnt;
  do {
    uint32_t ItM = i/page_sz;
    for (x = 0; x < 32 && T[x] < ItM; x++);
    x++;
    pass_bits[passes++] = x;
    i = (1<<x) - 1;
  } while (x>1);
  printf("Free id scan passes:   %d, bits[ ", passes);
  for (i = 0; i < passes; i++) {
    printf("%db ", pass_bits[i]);
  }
  printf("]\n");


  printf("\n");


  int p_user_ix = 1;
  int pd = pages_data;
  pd -= 1; // ix0
  int ix_entries = ent_ix0;
  int p_user = 0;
  while (pd > 0) {
    p_user++;
    pd -= 1;
    ix_entries--;
    if (ix_entries == 0) {
      ix_entries = ent_ix;
      pd -= 1;
      p_user_ix += 1;
    }
  }
  int max_p_user = p_user;
  printf("Longest possible file: %dB (%dKB), data pages:%d, index pages:%d\n",
         p_user*page_data_sz, p_user*page_data_sz/1024, p_user, p_user_ix);
  printf("Min metadata usage:    %2d%% (one large file, %dB)\n", 100-(100 * p_user*page_data_sz) /fs_sz, p_user*page_data_sz);

  // when calculating medium metadata usage we use a model where
  // we start by creating a file being 1/2 of max filesize, then a new file
  // whose size is mulitplied by 2/3 etc etc, but never less than one data page.
  // until fs is filled.
  p_user = 0;
  p_user_ix = 0;
  pd = pages_data;
  int p_file = spfs_max(1, max_p_user / 2);
  int p_files = 1;
  int f_size = p_file * page_data_sz;
  while (pd > 0) {
    ix_entries = ent_ix0;
    pd -= 1;
    while (pd > 0 && p_file > 0) {
      p_user++;
      p_file--;
      pd -= 1;
      ix_entries--;
      if (ix_entries == 0) {
        ix_entries = ent_ix;
        pd -= 1;
      }
    }
    p_file = spfs_max(1, (p_file * 2) / 3);
    f_size += p_file * page_data_sz;
    p_files++;
  }
  printf("Med metadata usage:    %2d%% (%d files, avg %dB)\n", 100-(100 * p_user*page_data_sz) /fs_sz, p_files,
         f_size  / (p_files));

  p_user = 0;
  p_user_ix = 0;
  pd = pages_data;
  p_user = 0;
  p_files = 0;
  while (pd > 0) {
    p_user++;
    pd -= 1;
    p_user_ix++;
    pd -= 1;
    p_files ++;
  }
  printf("Max metadata usage:    %2d%% (%d small files, %dB)\n", 100-(100 * p_user*page_data_sz) /fs_sz, p_files,
         page_data_sz);

  err = 0;
  end:
  return err;
}
