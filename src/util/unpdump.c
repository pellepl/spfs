/*
 * unpdump spfs utility
 *
 * Parses logs and creates images from calls to spfs_export.
 * Can be used to dump target file systems from e.g. uart.
 */

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

#include "spfs_lowlevel.h"
#include "spfs_dbg.h"

#define ERRPRI(f, ...) do {fprintf(stderr, "ERR:" f, ## __VA_ARGS__); } while (0)
#define ERREND(f, ...) do {fprintf(stderr, "ERR:" f, ## __VA_ARGS__); goto end;} while (0)

#define DECODEBUF_SZ           (1024*1024)
#define UNPACKBUF_SZ          DECODEBUF_SZ

typedef enum {
  SCAN = 0,
  VER,
  DATA,
  OCHECK,
  CHECK,
  RES,
  END
} pline_state_t;

static pline_state_t state;
static uint8_t *decodebuf;
static uint8_t *unpackbuf;
static uint8_t first_page;
static uint32_t page_sz;
static int outfiles = 0;
static int fdo = -1;
static uint16_t chk;
static uint16_t ochk;
static char dst_path[PATH_MAX];

static uint32_t _rle_unpack(uint8_t *dst, uint8_t *src, uint32_t len) {
  uint8_t *end = src + len;
  uint8_t *odst = dst;
  uint32_t dlen = 0;
  while (src < end) {
    uint8_t hdr = *src++;
    uint8_t l;
    if (hdr & 0x80) {
      // rle encoded
      l = (hdr & 0x7f) + 1;
      uint8_t i;
      uint8_t b = *src++;
      if (dst) for (i = 0; i < l; i++) *dst++ = b;
    } else {
      // raw data
      l = hdr + 1;
      if (dst) {
        memcpy(dst, src, l);
        dst += l;
      }
      src += l;
    }
    dlen += l;
  }
  return dlen;
}

static uint8_t b64_ctoi(char c) {
  if (c >= 'A' && c <= 'Z') return c-'A';
  if (c >= 'a' && c <= 'z') return c-'a'+26;
  if (c >= '0' && c <= '9') return c-'0'+2*26;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

static uint32_t dec_base64(const uint8_t *buf, uint8_t *out, uint32_t len) {
  uint8_t a,b,c,d;
  uint32_t olen = 0;
  while (len >= 4) {
    a = b64_ctoi(*buf++);
    b = b64_ctoi(*buf++);
    c = b64_ctoi(*buf++);
    d = b64_ctoi(*buf++);
    out[olen++] = (a<<2) | (b>>4);
    out[olen++] = (b<<4) | (c>>2);
    out[olen++] = (c<<6) | d;
    len -= 4;
  }
  if (len <= 1) return olen;
  a = b = c = 0;
  if (len >= 2) {
    a = b64_ctoi(*buf++);
    b = b64_ctoi(*buf++);
    out[olen++] = (a<<2) | (b>>4);
  }
  if (len == 2) return olen;
  c = b64_ctoi(*buf++);
  out[olen++] = (b<<4) | (c>>2);
  return olen;
}

static int unpack_block(char *line, uint32_t len, uint8_t packed) {
  uint32_t ix = 0;
  uint32_t six = 0;
  uint32_t res_sz = 0;
  int res = 0;
  uint8_t *res_buf;

  // decode base64 subblocks
  uint8_t *dec = decodebuf;
  uint32_t dec_sz = 0;
  while (ix < len) {
    six = ix;
    while (six < len && line[six] != ',') six++;
    dec_sz += dec_base64((uint8_t*)&line[ix], dec, six-ix);
    ix = six + 1;
    dec += dec_sz;
  }

  // unpack if necessary
  if (packed) {
    res_sz = _rle_unpack(unpackbuf, decodebuf, dec_sz);
    res_buf = unpackbuf;
  } else {
    res_sz = dec_sz;
    res_buf = decodebuf;
  }

  if (first_page) {
    page_sz = res_sz; // we know that the first page is always full as it is a meta info page
    first_page = 0;
  } else {
    if (res_sz < page_sz) {
      // this was probably only a header, so fill rest with ee
      uint8_t eebuf[page_sz - res_sz];
      memset(eebuf, 0xee, sizeof(eebuf));
      res = write(fdo, eebuf, sizeof(eebuf));
      if (res < 0) {
        ERRPRI("could not write to destination file\n%s\n", strerror(errno));
        return -1;
      }
    }
  }
  chk = _chksum(res_buf, res_sz, chk);
  res = write(fdo, res_buf, res_sz);
  if (res < 0) {
    ERRPRI("could not write to destination file\n%s\n", strerror(errno));
    return -1;
  }
  return page_sz;
}

static int parse_data(char *line, uint32_t len) {
  uint32_t ix = 0;
  uint32_t res_sz = 0;
  while (ix < len) {
    char c = line[ix];
    if (c == '\n' || c == '\r') {
      ix++; continue;
    }
    if (c == SPFS_EXPORT_HDR_PACKED) {
      // packed data
    } else if (c == SPFS_EXPORT_HDR_UNPACKED) {
      // unpacked data
    } else if (c == SPFS_EXPORT_HDR_FREE) {
      // free data
    } else {
      ERRPRI("malformed export data, unexpected export block header '%c'\n", c);
      return -1;
    }

    if (c == SPFS_EXPORT_HDR_FREE) {
      // handle free page
      uint8_t ffbuf[page_sz];
      memset(ffbuf, 0xff, sizeof(ffbuf));
      int res = write(fdo, ffbuf, sizeof(ffbuf));
      if (res < 0) {
        ERRPRI("could not write to destination file\n%s\n", strerror(errno));
        return -1;
      }
      ix++;
      res_sz += page_sz;
    } else {
      // handle packed / unpacked page
      uint32_t six = ix+1;
      while (six < len && line[six] != '.') six++;
      if (six == len) {
        ERRPRI("malformed export data, no export block footer\n");
        return -1;
      } else {
        int usz;
        if ((usz = unpack_block(&line[ix+1], six-ix-1, c == SPFS_EXPORT_HDR_PACKED))<0) {
          return -1;
        } else {
          res_sz += usz;
        }
      }
      ix = six+1;
    }
  }
  printf("  image size:%d (export size:%d, %.1f%%)\n", res_sz, len, 100.0*(double)len/(double)res_sz);
  return 0;
}

static int parseline(char *infile, int lnbr, char *line, uint32_t len) {
  pline_state_t ostate = state;
  switch (state) {
  case SCAN:
    if ((strncmp(SPFS_EXPORT_START, line, len)) == 0) {
      first_page = 1;
      chk = 0;
      state = VER;
      outfiles++;
      printf("found exported image @ line %d\n", lnbr);
    }
    break;
  case VER:
  {
    int maj, min, mic;
    if (sscanf(line, SPFS_EXPORT_VER, &maj, &min, &mic) != 3) {
      ERRPRI("malformed version @ line %d, skipping\n", lnbr);
      state = SCAN;
    } else {
      printf("  exported fs version: %d.%d.%d\n", maj, min, mic);
    }
    state = DATA;
    break;
  }
  case DATA:
  {
    sprintf(dst_path, "%s.%d.img", infile, outfiles);
    fdo = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, S_IRUSR | S_IWUSR);
    if (fdo < 0) {
      ERRPRI("could not create destination file %s\n%s\n", dst_path, strerror(errno));
      return -1;
    }
    if (parse_data(line, len)) {
      ERRPRI("error handling exported data @ line %d, skipping\n", lnbr);
      state = SCAN;
    } else {
      ochk = _chksum((uint8_t *)line, len-1, 0); // -1 for the newline
      state = OCHECK;
    }
    if (fdo > 0) {
      close(fdo);
      fdo = -1;
    }
    break;
  }
  case OCHECK:
  {
    uint32_t rochk;
    if (sscanf(line, SPFS_EXPORT_OCHK, &rochk) != 1) {
      ERRPRI("malformed export checksum @ line %d, skipping\n", lnbr);
      state = SCAN;
    } else {
      if (rochk != ochk) {
        printf("  export checksum export/local: 0x%04x/0x%04x\n", rochk, ochk);
        printf("WARNING: this image may be broken, the export checksums differs\n");
      }
      state = CHECK;
    }
    break;
  }
  case CHECK:
  {
    uint32_t rchk;
    if (sscanf(line, SPFS_EXPORT_CHK, &rchk) != 1) {
      ERRPRI("malformed image checksum @ line %d, skipping\n", lnbr);
      state = SCAN;
    } else {
      if (rchk != chk) {
        printf("  image checksum export/local: 0x%04x/0x%04x\n", rchk, chk);
        printf("WARNING: this image may be broken, the image checksums differs\n");
      }
      state = RES;
    }
    break;
  }
  case RES:
  {
    int rres;
    if (sscanf(line, SPFS_EXPORT_RES, &rres) != 1) {
      ERRPRI("malformed result @ line %d, skipping\n", lnbr);
      state = SCAN;
    } else {
      if (rres) {
        printf("WARNING: this image may be broken, the export result was %d\n", rres);
      }
      state = END;
    }
    break;
  }
  case END:
    if ((strncmp(SPFS_EXPORT_END, line, len)) == 0) {
      printf("  image stored: %s\n", dst_path);
      state = SCAN;
    }
    break;
  }
  if (ostate > SCAN && ostate == state) {
    ERRPRI("malformed export format @ line %d, ignoring\n", lnbr);
    state = SCAN;
  }
  return 0;
}

int main(int argc, char **argv) {
  spfs_t _fs;
  spfs_t *fs = &_fs;
  DIR *d = NULL;
  FILE *fd = NULL;
  char *line = NULL;
  size_t llen = 0;
  state = SCAN;
  int lnbr = 0;
  outfiles = 0;

  int err = 0;
  printf("spfs unpdump [fs v%d.%d.%d]\n",
      (SPFS_VERSION >> 12), (SPFS_VERSION >> 8) & 0xf, SPFS_VERSION & 0xff);
  if (argc < 2) {
    printf("Parses logs and creates images from calls to spfs_export.\n");
    printf("usage:\n%s <input file>\n", argv[0]);
    return 1;
  }


  decodebuf = malloc(DECODEBUF_SZ); // assume not bigger pages than this
  if (decodebuf == NULL) {
    ERRPRI("out of memory\n");
    return -1;
  }
  unpackbuf = malloc(UNPACKBUF_SZ);
  if (unpackbuf == NULL) {
    free(decodebuf);
    ERRPRI("out of memory\n");
    return -1;
  }
  fd = fopen(argv[1], "r");
  if (fd == NULL) {
    err = errno;
    ERREND("could not open %s\n%s\n", argv[1], strerror(errno));
  }

  ssize_t len;
  printf("processing %s\n", argv[1]);
  while ((len = getline(&line, &llen, fd)) > 0) {
    if (parseline(argv[1], ++lnbr, line, len)) {
      err = 1;
      break;
    }
  }

  printf("%d image%s exported\n", outfiles, outfiles == 1 ? "" : "s");
  end:
  free(unpackbuf);
  free(decodebuf);
  if (line) free(line);
  if (fd) fclose(fd);
  if (fdo > 0) close(fdo);

  return err;
}

