/* abootimg -  Manipulate (read, modify, create) Android Boot Images
 * Copyright (c) 2010-2011 Gilles Grandou <gilles@grandou.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>


#ifdef __linux__
#include <sys/ioctl.h>
#include <linux/fs.h> /* BLKGETSIZE64 */
#endif

#ifdef __CYGWIN__
#include <sys/ioctl.h>
#include <cygwin/fs.h> /* BLKGETSIZE64 */
#endif

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <sys/disk.h> /* DIOCGMEDIASIZE */
#include <sys/sysctl.h>
#endif

#if defined(__APPLE__)
# include <sys/disk.h> /* DKIOCGETBLOCKCOUNT */
#endif


#ifdef HAS_BLKID
#include <blkid/blkid.h>
#endif

#include "version.h"
#include "bootimg.h"


enum command {
  none,
  help,
  info,
  extract,
  update,
  create,
  dtbs
};


typedef struct
{
  unsigned     size;
  int          is_blkdev;

  char*        fname;
  char*        config_fname;
  char*        kernel_fname;
  char*        ramdisk_fname;
  char*        second_fname;
  char*        dtbs_fname;
  char*        signature_fname;

  FILE*        stream;

  boot_img_hdr header;

  char*        kernel;
  char*        ramdisk;
  char*        second;
  void*        dtbh;
  void**       dtbs;

  char signature[255];

} t_abootimg;


#define MAX_CONF_LEN    4096
char config_args[MAX_CONF_LEN] = "";



void abort_perror(char* str)
{
  perror(str);
  exit(errno);
}

void abort_printf(char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
  exit(1);
}


int blkgetsize(int fd, unsigned long long *pbsize)
{
# if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
  return ioctl(fd, DIOCGMEDIASIZE, pbsize);
# elif defined(__APPLE__)
  return ioctl(fd, DKIOCGETBLOCKCOUNT, pbsize);
# elif defined(__NetBSD__)
  // does a suitable ioctl exist?
  // return (ioctl(fd, DIOCGDINFO, &label) == -1);
  return 1;
# elif defined(__linux__) || defined(__CYGWIN__)
  return ioctl(fd, BLKGETSIZE64, pbsize);
# elif defined(__GNU__)
  // does a suitable ioctl for HURD exist?
  return 1;
# else
  return 1;
# endif

}

void print_usage(void)
{
  printf (
 " abootimg - manipulate Android Boot Images.\n"
 " (c) 2010-2011 Gilles Grandou <gilles@grandou.net>\n"
 " " VERSION_STR "\n"
 "\n"
 " abootimg [-h]\n"
 "\n"
 "      print usage\n"
 "\n"
 " abootimg -i <bootimg>\n"
 "\n"
 "      print boot image information\n"
 "\n"
 " abootimg -x <bootimg> [<bootimg.cfg> [<kernel> [<ramdisk> [<secondstage>[<devicetrees>]]]]]\n"
 "\n"
 "      extract objects from boot image:\n"
 "      - config file (default name bootimg.cfg)\n"
 "      - kernel image (default name zImage)\n"
 "      - ramdisk image (default name initrd.gz)\n"
 "      - second stage image (default name stage2.img)\n"
 "      - device trees (default name platform[.dtbh|.dtb_p#])\n"
 "      - signature (default name signature )\n"
 "\n"
 " abootimg -u <bootimg> [-c \"param=value\"] [-f <bootimg.cfg>] [-k <kernel>] [-r <ramdisk>] [-s <secondstage>] [-d <dtbs>] [-g <signature>]\n"
 "\n"
 "      update a current boot image with objects given in command line\n"
 "      - header informations given in arguments (several can be provided)\n"
 "      - header informations given in config file\n"
 "      - kernel image\n"
 "      - ramdisk image\n"
 "      - second stage image\n"
 "      - dtbs\n"
 "\n"
 "      bootimg has to be valid Android Boot Image, or the update will abort.\n"
 "\n"
 " abootimg --create <bootimg> [-c \"param=value\"] [-f <bootimg.cfg>] -k <kernel> -r <ramdisk> [-s <secondstage>] [-d <dtbs>] [-g <signature>]\n"
 "\n"
 "      create a new image from scratch.\n"
 "      if the boot image file is a block device, sanity check will be performed to avoid overwriting a existing\n"
 "      filesystem.\n"
 "\n"
 "      argurments are the same than for -u.\n"
 "      kernel and ramdisk are mandatory.\n"
 "\n"
 " abootimg --dtbs <platform.dbts>\n"
 "\n"
 "      print device tree header information\n"
 "\n"
    );
}


enum command parse_args(int argc, char** argv, t_abootimg* img)
{
  enum command cmd = none;
  int i;

  if (argc<2)
    return none;

  if (!strcmp(argv[1], "-h")) {
    return help;
  }
  else if (!strcmp(argv[1], "-i")) {
    cmd=info;
  }
  else if (!strcmp(argv[1], "-x")) {
    cmd=extract;
  }
  else if (!strcmp(argv[1], "-u")) {
    cmd=update;
  }
  else if (!strcmp(argv[1], "--create")) {
    cmd=create;
  }
  else if (!strcmp(argv[1], "--dtbs")) {
    cmd=dtbs;
  }
  else
    return none;

  switch(cmd) {
    case none:
    case help:
	    break;

    case info:
      if (argc != 3)
        return none;
      img->fname = argv[2];
      break;
      
    case extract:
      if ((argc < 3) || (argc > 7))
        return none;
      img->fname = argv[2];
      if (argc >= 4)
        img->config_fname = argv[3];
      if (argc >= 5)
        img->kernel_fname = argv[4];
      if (argc >= 6)
        img->ramdisk_fname = argv[5];
      if (argc >= 7)
        img->second_fname = argv[6];
      if (argc >= 8)
        img->dtbs_fname = argv[7];
      if (argc >= 9)
        img->signature_fname = argv[8];
      break;

    case update:
    case create:
      if (argc < 3)
        return none;
      img->fname = argv[2];
      img->config_fname = NULL;
      img->kernel_fname = NULL;
      img->ramdisk_fname = NULL;
      img->second_fname = NULL;
      img->dtbs_fname = NULL;
      img->signature_fname = NULL;

      for(i=3; i<argc; i++) {
        if (!strcmp(argv[i], "-c")) {
          if (++i >= argc)
            return none;
          unsigned len = strlen(argv[i]);
          if (strlen(config_args)+len+1 >= MAX_CONF_LEN)
            abort_printf("too many config parameters.\n");
          strcat(config_args, argv[i]);
          strcat(config_args, "\n");
        }
        else if (!strcmp(argv[i], "-f")) {
          if (++i >= argc)
            return none;
          img->config_fname = argv[i];
        }
        else if (!strcmp(argv[i], "-k")) {
          if (++i >= argc)
            return none;
          img->kernel_fname = argv[i];
        }
        else if (!strcmp(argv[i], "-r")) {
          if (++i >= argc)
            return none;
          img->ramdisk_fname = argv[i];
        }
        else if (!strcmp(argv[i], "-s")) {
          if (++i >= argc)
            return none;
          img->second_fname = argv[i];
        }
        else if (!strcmp(argv[i], "-d")) {
          if (++i >= argc)
            return none;
          img->dtbs_fname = argv[i];
        }
        else if (!strcmp(argv[i], "-g")) {
          if (++i >= argc)
            return none;
          img->signature_fname = argv[i];
        }
        else
          return none;
      }
      break;
    case dtbs:
      if (argc != 3)
        return none;
      img->fname = argv[2];
      break;
  }
  
  return cmd;
}



int check_boot_img_header(t_abootimg* img)
{
  if (strncmp((char*)(img->header.magic), BOOT_MAGIC, BOOT_MAGIC_SIZE)) {
    fprintf(stderr, "%s: no Android Magic Value\n", img->fname);
    return 1;
  }

  if (!(img->header.kernel_size)) {
    fprintf(stderr, "%s: kernel size is null\n", img->fname);
    return 1;
  }

  if (!(img->header.ramdisk_size)) {
    fprintf(stderr, "%s: ramdisk size is null\n", img->fname);
    return 1;
  }

  unsigned page_size = img->header.page_size;
  if (!page_size) {
    fprintf(stderr, "%s: Image page size is null\n", img->fname);
    return 1;
  }

  // warning, page size is not of valid size?

  unsigned n = (img->header.kernel_size + page_size - 1) / page_size;
  unsigned m = (img->header.ramdisk_size + page_size - 1) / page_size;
  unsigned o = (img->header.second_size + page_size - 1) / page_size;
  unsigned p = (img->header.dtbs_size + page_size - 1) / page_size;

  unsigned total_size = (1+n+m+o+p)*page_size;

  if (total_size > img->size) {
    fprintf(stderr, "%s: sizes mismatches\n  total_size %u != img size %u\n", img->fname, total_size, img->size);
    return 1;
  }

  return 0;
}



void check_if_block_device(t_abootimg* img)
{
  struct stat st;

  if (stat(img->fname, &st))
    if (errno != ENOENT) {
      printf("errno=%d\n", errno);
      abort_perror(img->fname);
    }

#ifdef HAS_BLKID
  if (S_ISBLK(st.st_mode)) {
    img->is_blkdev = 1;

    char* type = blkid_get_tag_value(NULL, "TYPE", img->fname);
    if (type)
      abort_printf("%s: refuse to write on a valid partition type (%s)\n", img->fname, type);

    int fd = open(img->fname, O_RDONLY);
    if (fd == -1)
      abort_perror(img->fname);
    
    unsigned long long bsize = 0;
    if (blkgetsize(fd, &bsize))
      abort_perror(img->fname);
    img->size = bsize;

    close(fd);
  }
#endif
}



void open_bootimg(t_abootimg* img, char* mode)
{
  img->stream = fopen(img->fname, mode);
  if (!img->stream)
    abort_perror(img->fname);
}



void read_header(t_abootimg* img)
{
  size_t rb = fread(&img->header, sizeof(boot_img_hdr), 1, img->stream);
  if ((rb!=1) || ferror(img->stream))
    abort_perror(img->fname);
  else if (feof(img->stream))
    abort_printf("%s: cannot read image header\n", img->fname);

  struct stat s;
  int fd = fileno(img->stream);
  if (fstat(fd, &s))
    abort_perror(img->fname);

  if (S_ISBLK(s.st_mode)) {
    unsigned long long bsize = 0;

    if (blkgetsize(fd, &bsize))
      abort_perror(img->fname);
    img->size = bsize;
    img->is_blkdev = 1;
  }
  else {
    img->size = s.st_size;
    img->is_blkdev = 0;
  }

  if (check_boot_img_header(img))
    abort_printf("%s: not a valid Android Boot Image.\n", img->fname);
}



void update_header_entry(t_abootimg* img, char* cmd)
{
  char *p;
  char *token;
  char *endtoken;
  char *value;

  p = strchr(cmd, '\n');
  if (p)
    *p  = '\0';

  p = cmd;
  p += strspn(p, " \t");
  token = p;
  
  p += strcspn(p, " =\t");
  endtoken = p;
  p += strspn(p, " \t");

  if (*p++ != '=')
    goto err;

  p += strspn(p, " \t");
  value = p;

  *endtoken = '\0';

  unsigned valuenum = strtoul(value, NULL, 0);
  
  if (!strcmp(token, "cmdline")) {
    unsigned len = strlen(value);
    if (len >= BOOT_ARGS_SIZE) 
      abort_printf("cmdline length (%d) is too long (max %d)", len, BOOT_ARGS_SIZE-1);
    memset(img->header.cmdline, 0, BOOT_ARGS_SIZE);
    strcpy((char*)(img->header.cmdline), value);
  }
  else if (!strncmp(token, "name", 4)) {
    strncpy((char*)(img->header.name), value, BOOT_NAME_SIZE);
    img->header.name[BOOT_NAME_SIZE-1] = '\0';
  }
  else if (!strncmp(token, "bootsize", 8)) {
    if (img->is_blkdev && (img->size != valuenum))
      abort_printf("%s: cannot change Boot Image size for a block device\n", img->fname);
    img->size = valuenum;
  }
  else if (!strncmp(token, "pagesize", 8)) {
    img->header.page_size = valuenum;
  }
  else if (!strncmp(token, "kerneladdr", 10)) {
    img->header.kernel_addr = valuenum;
  }
  else if (!strncmp(token, "ramdiskaddr", 11)) {
    img->header.ramdisk_addr = valuenum;
  }
  else if (!strncmp(token, "secondaddr", 10)) {
    img->header.second_addr = valuenum;
  }
  else if (!strncmp(token, "tagsaddr", 8)) {
    img->header.tags_addr = valuenum;
  }
  else
    goto err;
  return;

err:
  abort_printf("%s: bad config entry\n", token);
}


void update_header(t_abootimg* img)
{
  if (img->config_fname) {
    FILE* config_file = fopen(img->config_fname, "r");
    if (!config_file)
      abort_perror(img->config_fname);

    printf("reading config file %s\n", img->config_fname);

    char* line = NULL;
    size_t len = 0;
    int read;

    while ((read = getline(&line, &len, config_file)) != -1) {
      update_header_entry(img, line);
      free(line);
      line = NULL;
    }
    if (ferror(config_file))
      abort_perror(img->config_fname);
  }

  unsigned len = strlen(config_args);
  if (len) {
    FILE* config_file = fmemopen(config_args, len, "r");
    if  (!config_file)
      abort_perror("-c args");

    printf("reading config args\n");

    char* line = NULL;
    size_t len = 0;
    int read;

    while ((read = getline(&line, &len, config_file)) != -1) {
      update_header_entry(img, line);
      free(line);
      line = NULL;
    }
    if (ferror(config_file))
      abort_perror("-c args");
  }
}



void update_images(t_abootimg *img)
{
  unsigned page_size = img->header.page_size;
  unsigned ksize = img->header.kernel_size;
  unsigned rsize = img->header.ramdisk_size;
  unsigned ssize = img->header.second_size;
  unsigned dsize = img->header.dtbs_size;

  if (!page_size)
    abort_printf("%s: Image page size is null\n", img->fname);

  unsigned n = (ksize + page_size - 1) / page_size;
  unsigned m = (rsize + page_size - 1) / page_size;
  unsigned o = (ssize + page_size - 1) / page_size;
  unsigned p = (dsize + page_size - 1) / page_size;

  unsigned roffset = (1+n)*page_size;
  unsigned soffset = (1+n+m)*page_size;
  unsigned doffset = (1+n+m+o)*page_size;

  if (img->kernel_fname) {
    printf("reading kernel from %s\n", img->kernel_fname);
    FILE* stream = fopen(img->kernel_fname, "r");
    if (!stream)
      abort_perror(img->kernel_fname);
    struct stat st;
    if (fstat(fileno(stream), &st))
      abort_perror(img->kernel_fname);
    ksize = st.st_size;
    char* k = malloc(ksize);
    if (!k)
      abort_perror("");
    size_t rb = fread(k, ksize, 1, stream);
    if ((rb!=1) || ferror(stream))
      abort_perror(img->kernel_fname);
    else if (feof(stream))
      abort_printf("%s: cannot read kernel\n", img->kernel_fname);
    fclose(stream);
    img->header.kernel_size = ksize;
    img->kernel = k;
  }

  if (img->ramdisk_fname) {
    printf("reading ramdisk from %s\n", img->ramdisk_fname);
    FILE* stream = fopen(img->ramdisk_fname, "r");
    if (!stream)
      abort_perror(img->ramdisk_fname);
    struct stat st;
    if (fstat(fileno(stream), &st))
      abort_perror(img->ramdisk_fname);
    rsize = st.st_size;
    char* r = malloc(rsize);
    if (!r)
      abort_perror("");
    size_t rb = fread(r, rsize, 1, stream);
    if ((rb!=1) || ferror(stream))
      abort_perror(img->ramdisk_fname);
    else if (feof(stream))
      abort_printf("%s: cannot read ramdisk\n", img->ramdisk_fname);
    fclose(stream);
    img->header.ramdisk_size = rsize;
    img->ramdisk = r;
  }
  else if (img->kernel) {
    // if kernel is updated, copy the ramdisk from original image

    printf (" copy  ramdisk %u bytes from 0x%08x\n", rsize, roffset);

    char* r = malloc(rsize);
    if (!r)
      abort_perror("");
    if (fseek(img->stream, roffset, SEEK_SET))
      abort_perror(img->fname);
    size_t rb = fread(r, rsize, 1, img->stream);
    if ((rb!=1) || ferror(img->stream))
      abort_perror(img->fname);
    else if (feof(img->stream))
      abort_printf("%s: cannot read ramdisk\n", img->fname);
    img->ramdisk = r;
  }

  if (img->second_fname) {
    printf("reading second stage from %s\n", img->second_fname);
    FILE* stream = fopen(img->second_fname, "r");
    if (!stream)
      abort_perror(img->second_fname);
    struct stat st;
    if (fstat(fileno(stream), &st))
      abort_perror(img->second_fname);
    ssize = st.st_size;
    char* s = malloc(ssize);
    if (!s)
      abort_perror("");
    size_t rb = fread(s, ssize, 1, stream);
    if ((rb!=1) || ferror(stream))
      abort_perror(img->second_fname);
    else if (feof(stream))
      abort_printf("%s: cannot read second stage\n", img->second_fname);
    fclose(stream);
    img->header.second_size = ssize;
    img->second = s;
  }
  else if (img->ramdisk && img->header.second_size) {
    // if ramdisk is updated, copy the second stage from original image
    printf (" copy second %u bytes from 0x%08x\n", ssize, soffset);

    char* s = malloc(ssize);
    if (!s)
      abort_perror("");
    if (fseek(img->stream, soffset, SEEK_SET))
      abort_perror(img->fname);
    size_t rb = fread(s, ssize, 1, img->stream);
    if ((rb!=1) || ferror(img->stream))
      abort_perror(img->fname);
    else if (feof(img->stream))
      abort_printf("%s: cannot read second stage\n", img->fname);
    img->second = s;
  }

  if (img->dtbs_fname) {
    printf("reading dtbs ...\n");

    char dtbname[256] = {0};

    sprintf(dtbname, "%s.dtbh", img->dtbs_fname);

    printf(".. DTBH from %s\n",dtbname);

    // open dtbh file
    FILE* stream = fopen(dtbname, "r");
    if (!stream)
      abort_perror(dtbname);
    //get size of dtbh file
    struct stat st;
    if (fstat(fileno(stream), &st))
      abort_perror(dtbname);
    dsize = st.st_size;

    //alloc memmory and clear for dtbh (not more than 1 pagesize)
    //char* d = malloc(dsize);
    char* d = calloc(page_size,1);

    if (!d)
      abort_perror("");
    // read
    size_t rb = fread(d, dsize, 1, stream);
    if ((rb!=1) || ferror(stream))
      abort_perror(dtbname);
    else if (feof(stream))
      abort_printf("%s: cannot read DTBH\n", dtbname);

    //store DTBH pointer to image
    img->dtbh = d;


    // alloc and load each dtbs
    dtbs_t *dtbh = (dtbs_t *)d;

    // alloc ptr table for dtbs
    img->dtbs = (void **)malloc(dtbh->num_entries * sizeof(void*));

    // entryes
    dt_entry_t *dt = (dt_entry_t *)(d + sizeof(dtbs_t));

    int ientry;
    for (ientry = 0, p = 1; ientry<dtbh->num_entries; ientry++) {
      // generate dtb name
      sprintf(dtbname,"%s.dtb_p%d",img->dtbs_fname, ientry);

      //printf(".. dtb from %s\n",dtbname);
      printf (" .. dtb %s offset 0x%08x, size 0x%08x\n", dtbname, dt[ientry].offset, dt[ientry].dtb_size);

      FILE* dtbs_file = fopen(dtbname, "r");
      if (!dtbs_file)
        abort_perror(dtbname);

      //get size of dtb file
      struct stat st;
      if (fstat(fileno(dtbs_file), &st))
        abort_perror(dtbname);

      void* dp = (void *)malloc(st.st_size);

      // read dtb file
      size_t rb = fread(dp, st.st_size, 1, dtbs_file);

      if ((rb!=1) || ferror(dtbs_file))
        abort_perror(dtbname);
      else if (feof(dtbs_file))
        abort_printf("%s: cannot read DTB\n", dtbname);

      // store dtb to image table
      img->dtbs[ientry] = dp;

      // update size of dtb
      dt[ientry].offset = p * page_size;
      dt[ientry].dtb_size = st.st_size; // need to be alse page-multiple

      printf (" .. new offset 0x%08x, size 0x%08x\n", dt[ientry].offset, dt[ientry].dtb_size);

      // update header dtbs p pages count (for next loop)
      p+=  (st.st_size + page_size - 1) / page_size;
    }; // for loop

    // update header dtbs_size
    img->header.dtbs_size = p * page_size;
  }
  else if (img->header.dtbs_size) {
    printf (" copy  dtbs %u bytes from 0x%08x\n", dsize, doffset);

    // if *** is updated, copy the dtbs from original image
    char* d = malloc(dsize);
    if (!d)
      abort_perror("");
    if (fseek(img->stream, doffset, SEEK_SET))
      abort_perror(img->fname);
    size_t rb = fread(d, dsize, 1, img->stream);
    if ((rb!=1) || ferror(img->stream))
      abort_perror(img->fname);
    else if (feof(img->stream))
      abort_printf("%s: cannot read dtts\n", img->fname);


    // store dtb structure header
    img->dtbh = d;

    // populate dtbs table
    dtbs_t *dtbh = (dtbs_t *)d;

    // alloc ptr table for dtbs
    img->dtbs = (void **)malloc(dtbh->num_entries * sizeof(void*));

    // entryes
    dt_entry_t *dt = (dt_entry_t *)(d + sizeof(dtbs_t));

    int ientry;
    for (ientry = 0; ientry<dtbh->num_entries; ientry++) {

      // store dtb to image table
      img->dtbs[ientry] = (void*)(d + dt[ientry].offset);
    };
  }

  // update signature? (read from file, or memory)
  // offset is (1+n+m+o+p)
  memcpy(img->signature, "SEANDROIDENFORCE", sizeof("SEANDROIDENFORCE"));


  n = (img->header.kernel_size + page_size - 1) / page_size;
  m = (img->header.ramdisk_size + page_size - 1) / page_size;
  o = (img->header.second_size + page_size - 1) / page_size;
  p = (img->header.dtbs_size + page_size - 1) / page_size;

  unsigned total_size = (1+n+m+o+p+1)*page_size;

  if (!img->size)
    img->size = total_size;
  else if (total_size > img->size)
    abort_printf("%s: updated is too big for the Boot Image (%u vs %u bytes)\n", img->fname, total_size, img->size);
}



void write_bootimg(t_abootimg* img)
{
  unsigned psize;
  char* padding;

  printf ("Writing Boot Image %s\n", img->fname);

  psize = img->header.page_size;
  padding = calloc(psize, 1);
  if (!padding)
    abort_perror("");

  unsigned n = (img->header.kernel_size + psize - 1) / psize;
  unsigned m = (img->header.ramdisk_size + psize - 1) / psize;
  unsigned o = (img->header.second_size + psize - 1) / psize;
  unsigned p = (img->header.dtbs_size + psize - 1) / psize;

  printf ("   header %zu\n", sizeof(img->header));

  if (fseek(img->stream, 0, SEEK_SET))
    abort_perror(img->fname);

  fwrite(&img->header, sizeof(img->header), 1, img->stream);
  if (ferror(img->stream))
    abort_perror(img->fname);

  fwrite(padding, psize - sizeof(img->header), 1, img->stream);
  if (ferror(img->stream))
    abort_perror(img->fname);

  if (img->kernel) {
    printf ("   kernel %u at 0x%08x\n", img->header.kernel_size, psize);

    fwrite(img->kernel, img->header.kernel_size, 1, img->stream);
    if (ferror(img->stream))
      abort_perror(img->fname);

    fwrite(padding, psize - (img->header.kernel_size % psize), 1, img->stream);
    if (ferror(img->stream))
      abort_perror(img->fname);
  }

  if (img->ramdisk) {
    printf ("   ramdisk %u at 0x%08x\n", img->header.ramdisk_size, (1+n)*psize);

    if (fseek(img->stream, (1+n)*psize, SEEK_SET))
      abort_perror(img->fname);

    fwrite(img->ramdisk, img->header.ramdisk_size, 1, img->stream);
    if (ferror(img->stream))
      abort_perror(img->fname);

    fwrite(padding, psize - (img->header.ramdisk_size % psize), 1, img->stream);
    if (ferror(img->stream))
      abort_perror(img->fname);
  }

  if (img->header.second_size) {
    printf ("   second %u at 0x%08x\n", img->header.second_size, (1+n+m)*psize);

    if (fseek(img->stream, (1+n+m)*psize, SEEK_SET))
      abort_perror(img->fname);

    fwrite(img->second, img->header.second_size, 1, img->stream);
    if (ferror(img->stream))
      abort_perror(img->fname);

    fwrite(padding, psize - (img->header.second_size % psize), 1, img->stream);
    if (ferror(img->stream))
      abort_perror(img->fname);
  }

  // write dtbs to stream
  if (img->dtbh) {
    printf ("   dtbs %u at 0x%08x\n", img->header.dtbs_size, (1+n+m+o)*psize);

    // go to at beginig of DTBH
    if (fseek(img->stream, (1+n+m+o)*psize, SEEK_SET))
      abort_perror(img->fname);

    // write DTBH
    fwrite(img->dtbh, psize, 1, img->stream);
    if (ferror(img->stream))
      abort_perror(img->fname);

    // populate dtbs table
    dtbs_t *dtbh = (dtbs_t *)img->dtbh;

    // entryes
    dt_entry_t *dt = (dt_entry_t *)(((char*)img->dtbh) + sizeof(dtbs_t));

    int ientry;
    for (ientry = 0; ientry<dtbh->num_entries; ientry++) {

      // wtire dtb to stream
      fwrite(img->dtbs[ientry], dt[ientry].dtb_size, 1, img->stream);
      if (ferror(img->stream))
        abort_perror(img->fname);

      if ((dt[ientry].dtb_size % psize) > 0) {
        printf ("   . dtb padding for %u is %u because %u\n", dt[ientry].dtb_size, psize - (dt[ientry].dtb_size % psize), dt[ientry].dtb_size % psize);
        fwrite(padding, psize - (dt[ientry].dtb_size % psize), 1, img->stream);
      };

      if (ferror(img->stream))
        abort_perror(img->fname);
    };

  }


  // update signature
  printf ("   signature %zu at 0x%08x\n", sizeof(img->signature), (1+n+m+o+p)*psize);
  // write signature
  if (fseek(img->stream, (1+n+m+o+p)*psize, SEEK_SET))
    abort_perror(img->fname);

  fwrite(img->signature, sizeof(img->signature), 1, img->stream);
  if (ferror(img->stream))
    abort_perror(img->fname);

  fwrite(padding, psize - (sizeof(img->signature) % psize), 1, img->stream);
  if (ferror(img->stream))
    abort_perror(img->fname);


  //ftruncate (fileno(img->stream), img->size);

  free(padding);
}



void print_bootimg_info(t_abootimg* img)
{
  printf ("\nAndroid Boot Image Info:\n\n");

  printf ("* file name = %s %s\n\n", img->fname, img->is_blkdev ? "[block device]":"");

  printf ("* image size = %u bytes (%.2f MB)\n", img->size, (double)img->size/0x100000);
  //printf ("  page size  = %u bytes\n\n", img->header.page_size);

  printf ("\n<boot_img_hdr>\n");

  unsigned kernel_size = img->header.kernel_size;
  unsigned ramdisk_size = img->header.ramdisk_size;
  unsigned second_size = img->header.second_size;

  unsigned page_size  = img->header.page_size;

  unsigned dts_size = img->header.dtbs_size;

  // pages
  unsigned n_pages = (kernel_size + page_size-1) / page_size;
  unsigned m_pages = (ramdisk_size + page_size-1) / page_size;
  unsigned o_pages = (second_size + page_size-1) / page_size;
  unsigned p_pages = (dts_size + page_size-1) / page_size;


  //printf ("   magic:        %s\n", img->header.magic);

  printf ("   kernel_size:  %u bytes (%.2f MB), %u pages\n", kernel_size, (double)kernel_size/0x100000, n_pages);
  printf ("   kernel_addr:  0x%08x\n", img->header.kernel_addr); /* physical load addr */

  printf ("   ramdisk_size: %u bytes (%.2f MB), %u pages\n", ramdisk_size, (double)ramdisk_size/0x100000, m_pages);
  printf ("   ramdisk_addr: 0x%08x\n", img->header.ramdisk_addr); /* physical load addr */

  printf ("   second_size:  %u bytes (%.2f MB), %u pages\n", second_size, (double)second_size/0x100000, o_pages);
  printf ("   second_addr:  0x%08x\n", img->header.second_addr); /* physical load addr */

  printf ("   tags_addr:    0x%08x\n", img->header.tags_addr); /* physical addr for kernel tags */
  printf ("   page_size:    %u bytes\n", img->header.page_size); /* flash page size we assume */

  printf ("   dtbs_size:    %u bytes (%.2f MB), %u pages\n", dts_size, (double)dts_size/0x100000, p_pages);

  printf ("   unused[0]:    %u \n", img->header.unused[0]); /* future expansion: should be 0 */

  printf ("   name:         %s\n\n", img->header.name);

  //printf ("   cmdline: %s\n");
  if (img->header.cmdline[0])
    printf ("   cmdline:      %s\n\n", img->header.cmdline);
  else
    printf ("   cmdline       empty\n\n");

  printf ("   id[8] 0x%04X%04X%04X%04X%04X%04X%04X%04X\n",
    img->header.id[0],img->header.id[1],img->header.id[2],img->header.id[3],
    img->header.id[4],img->header.id[5],img->header.id[6],img->header.id[7]
  ); /* timestamp / checksum / sha1 / etc */

  printf ("\n<boot_img layout>\n");

  unsigned offset = ((sizeof(img->header) + page_size-1) / page_size); // initial offset of header, one page is more than enough

  unsigned kernel_offset = offset * page_size; offset += n_pages;
  unsigned ramdisk_offset = offset * page_size; offset += m_pages;
  unsigned second_offset = offset * page_size; offset += o_pages;
  unsigned dts_offset = offset * page_size; offset += p_pages; // for the signature
  unsigned signature_offset = offset * page_size;

  printf ("   kernel offset     0x%08x\n", kernel_offset);
  printf ("   ramdisk offset:   0x%08x\n", ramdisk_offset);
  printf ("   secondary offset: 0x%08x\n", second_offset);
  printf ("   dtbs offset:      0x%08x\n", dts_offset);
  printf ("   signature offset: 0x%08x\n", signature_offset);

  printf ("\n");
}


void print_dtbh_info(t_abootimg* img)
{
  unsigned page_size = img->header.page_size;

  unsigned ksize = img->header.kernel_size;
  unsigned rsize = img->header.ramdisk_size;
  unsigned ssize = img->header.second_size;
  unsigned dsize = img->header.dtbs_size;

  unsigned n = (ksize + page_size - 1) / page_size;
  unsigned m = (rsize + page_size - 1) / page_size;
  unsigned o = (ssize + page_size - 1) / page_size;
  //unsigned p = (dsize + page_size - 1) / page_size;

  //unsigned roffset = (1+n)*page_size;
  //unsigned soffset = (1+n+m)*page_size;
  unsigned doffset = (1+n+m+o)*page_size;

  //printf (" copy  dtbs %u bytes from 0x%08x\n", dsize, doffset);

  char* d = malloc(dsize);
  if (!d)
    abort_perror("");
  if (fseek(img->stream, doffset, SEEK_SET))
    abort_perror(img->fname);
  size_t rb = fread(d, dsize, 1, img->stream);
  if ((rb!=1) || ferror(img->stream))
    abort_perror(img->fname);
  else if (feof(img->stream))
    abort_printf("%s: cannot read dtbs\n", img->fname);

  dtbs_t *dtbh = (dtbs_t *)d;

  printf ("\n<dtbh_header Info>\n");
  printf ("  magic:0x%08x, version:0x%08x, num_entries:0x%08x\n",
    dtbh->magic, dtbh->version, dtbh->num_entries);

/*
  printf ("\n<device info>\n");
  printf ("        chip_id: 0x%08x\n", dtbh->device_info.chip_id);
  printf ("    platform_id: 0x%08x\n", dtbh->device_info.platform_id);
  printf ("     subtype_id: 0x%08x\n", dtbh->device_info.subtype_id);
  printf ("         hw_rev: 0x%08x\n", dtbh->device_info.hw_rev);
*/

  dt_entry_t *dt = (dt_entry_t *)(d + sizeof(dtbs_t));
  int ientry;
  for (ientry = 0; ientry<dtbh->num_entries; ientry++) {
    printf ("\ndt_entry[%02d]\n", ientry);
    printf ("        chip_id: 0x%08x\n", dt[ientry].chip_id);
    printf ("    platform_id: 0x%08x\n", dt[ientry].platform_id);
    printf ("     subtype_id: 0x%08x\n", dt[ientry].subtype_id);
    printf ("         hw_rev: 0x%08x\n", dt[ientry].hw_rev);
    printf ("     hw_rev_end: 0x%08x\n", dt[ientry].hw_rev_end);
    printf ("         offset: 0x%08x\n", dt[ientry].offset);
    printf ("       dtb size: 0x%08x\n", dt[ientry].dtb_size);
  }

  printf ("\n");
}



void write_bootimg_config(t_abootimg* img)
{
  printf ("writing boot image config in %s\n", img->config_fname);

  FILE* config_file = fopen(img->config_fname, "w");
  if (!config_file)
    abort_perror(img->config_fname);

  fprintf(config_file, "bootsize = 0x%x\n", img->size);
  fprintf(config_file, "pagesize = 0x%x\n", img->header.page_size);

  fprintf(config_file, "kerneladdr = 0x%x\n", img->header.kernel_addr);
  fprintf(config_file, "ramdiskaddr = 0x%x\n", img->header.ramdisk_addr);
  fprintf(config_file, "secondaddr = 0x%x\n", img->header.second_addr);
  fprintf(config_file, "tagsaddr = 0x%x\n", img->header.tags_addr);

  fprintf(config_file, "name = %s\n", img->header.name);
  fprintf(config_file, "cmdline = %s\n", img->header.cmdline);
  
  fclose(config_file);
}



void extract_kernel(t_abootimg* img)
{
  unsigned psize = img->header.page_size;
  unsigned ksize = img->header.kernel_size;

  printf ("extracting kernel in %s\n", img->kernel_fname);

  void* k = malloc(ksize);
  if (!k)
    abort_perror(NULL);

  if (fseek(img->stream, psize, SEEK_SET))
    abort_perror(img->fname);

  size_t rb = fread(k, ksize, 1, img->stream);
  if ((rb!=1) || ferror(img->stream))
    abort_perror(img->fname);
 
  FILE* kernel_file = fopen(img->kernel_fname, "w");
  if (!kernel_file)
    abort_perror(img->kernel_fname);

  fwrite(k, ksize, 1, kernel_file);
  if (ferror(kernel_file))
    abort_perror(img->kernel_fname);

  fclose(kernel_file);
  free(k);
}


void extract_ramdisk(t_abootimg* img)
{
  unsigned psize = img->header.page_size;
  unsigned ksize = img->header.kernel_size;
  unsigned rsize = img->header.ramdisk_size;

  unsigned n = (ksize + psize - 1) / psize;
  unsigned roffset = (1+n)*psize;

  printf ("extracting ramdisk in %s\n", img->ramdisk_fname);

  void* r = malloc(rsize);
  if (!r) 
    abort_perror(NULL);

  if (fseek(img->stream, roffset, SEEK_SET))
    abort_perror(img->fname);

  size_t rb = fread(r, rsize, 1, img->stream);
  if ((rb!=1) || ferror(img->stream))
    abort_perror(img->fname);
 
  FILE* ramdisk_file = fopen(img->ramdisk_fname, "w");
  if (!ramdisk_file)
    abort_perror(img->ramdisk_fname);

  fwrite(r, rsize, 1, ramdisk_file);
  if (ferror(ramdisk_file))
    abort_perror(img->ramdisk_fname);

  fclose(ramdisk_file);
  free(r);
}


void extract_second(t_abootimg* img)
{
  unsigned psize = img->header.page_size;
  unsigned ksize = img->header.kernel_size;
  unsigned rsize = img->header.ramdisk_size;
  unsigned ssize = img->header.second_size;

  if (!ssize) // Second Stage not present
    return;

  unsigned n = (rsize + ksize + psize - 1) / psize;
  unsigned soffset = (1+n)*psize;

  printf ("extracting second stage image in %s\n", img->second_fname);

  void* s = malloc(ssize);
  if (!s)
    abort_perror(NULL);

  if (fseek(img->stream, soffset, SEEK_SET))
    abort_perror(img->fname);

  size_t rb = fread(s, ssize, 1, img->stream);
  if ((rb!=1) || ferror(img->stream))
    abort_perror(img->fname);
 
  FILE* second_file = fopen(img->second_fname, "w");
  if (!second_file)
    abort_perror(img->second_fname);

  fwrite(s, ssize, 1, second_file);
  if (ferror(second_file))
    abort_perror(img->second_fname);

  fclose(second_file);
  free(s);
}


void extract_dtbs(t_abootimg* img)
{
  unsigned psize = img->header.page_size;
  unsigned ksize = img->header.kernel_size;
  unsigned rsize = img->header.ramdisk_size;
  unsigned ssize = img->header.second_size;
  unsigned dsize = img->header.dtbs_size;

  unsigned n = (ksize + rsize + ssize + psize-1) / psize;
  unsigned doffset = (1+n)*psize;

  printf ("extracting ");

  void* d = malloc(dsize);
  if (!d)
    abort_perror(NULL);

  if (fseek(img->stream, doffset, SEEK_SET))
    abort_perror(img->fname);

  size_t rb = fread(d, dsize, 1, img->stream);
  if ((rb!=1) || ferror(img->stream))
    abort_perror(img->fname);

  char dtbname[256] = {0};

  sprintf(dtbname,"%s.dtbh",img->dtbs_fname);

  printf ("DTBH %s\n", dtbname);

  // load header of dtbh
  dtbs_t *dtbh = (dtbs_t *)d;

  FILE* dtbs_file = fopen(dtbname, "w");
  if (!dtbs_file)
    abort_perror(dtbname);

  unsigned dtbhsize = sizeof(dtbs_t) + sizeof(dt_entry_t) * dtbh->num_entries;
  fwrite(d, dtbhsize, 1, dtbs_file);
  if (ferror(dtbs_file))
    abort_perror(dtbname);

  fclose(dtbs_file);

  dt_entry_t *dt = (dt_entry_t *)(d + sizeof(dtbs_t));

  int ientry;

  for (ientry = 0; ientry<dtbh->num_entries; ientry++) {
    sprintf(dtbname,"%s.dtb_p%d",img->dtbs_fname,ientry);

    printf (" .. dtb %s offset 0x%08x, size 0x%08x\n", dtbname, dt[ientry].offset, dt[ientry].dtb_size);

    FILE* dtbs_file = fopen(dtbname, "w");
    if (!dtbs_file)
      abort_perror(dtbname);

    fwrite((d + dt[ientry].offset), dt[ientry].dtb_size, 1, dtbs_file);
    if (ferror(dtbs_file))
      abort_perror(dtbname);
    fclose(dtbs_file);
  }
  free(d);
}


void extract_signature(t_abootimg* img)
{
/*  unsigned psize = img->header.page_size;
  unsigned zsize = 256;

  printf ("extracting signature in %s\n", img->signature_fname);

  void* k = malloc(ksize);
  if (!k)
    abort_perror(NULL);

  if (fseek(img->stream, psize, SEEK_SET))
    abort_perror(img->fname);

  size_t rb = fread(k, ksize, 1, img->stream);
  if ((rb!=1) || ferror(img->stream))
    abort_perror(img->fname);

  FILE* kernel_file = fopen(img->kernel_fname, "w");
  if (!kernel_file)
    abort_perror(img->kernel_fname);

  fwrite(k, ksize, 1, kernel_file);
  if (ferror(kernel_file))
    abort_perror(img->kernel_fname);

  fclose(kernel_file);
  free(k);*/
}

t_abootimg* new_bootimg()
{
  t_abootimg* img;

  img = calloc(sizeof(t_abootimg), 1);
  if (!img)
    abort_perror(NULL);

  img->config_fname = "bootimg.cfg";
  img->kernel_fname = "zImage";
  img->ramdisk_fname = "initrd.gz";
  img->second_fname = "stage2.img";
  img->dtbs_fname = "platform";
  img->signature_fname = "signature";

  memcpy(img->header.magic, BOOT_MAGIC, BOOT_MAGIC_SIZE);
  img->header.page_size = 2048;  // a sensible default page size

  return img;
}


int main(int argc, char** argv)
{
  t_abootimg* bootimg = new_bootimg();

  switch(parse_args(argc, argv, bootimg))
  {
    case none:
      printf("error - bad arguments\n\n");
      print_usage();
      break;

    case help:
      print_usage();
      break;

    case info:
      open_bootimg(bootimg, "r");
      read_header(bootimg);
      print_bootimg_info(bootimg);
      break;

    case extract:
      open_bootimg(bootimg, "r");
      read_header(bootimg);
      write_bootimg_config(bootimg);
      extract_kernel(bootimg);
      extract_ramdisk(bootimg);
      extract_second(bootimg);
      extract_dtbs(bootimg);
      extract_signature(bootimg);
      break;
    
    case update:
      open_bootimg(bootimg, "r+");
      read_header(bootimg);
      update_header(bootimg);
      update_images(bootimg);
      //print_bootimg_info(bootimg);
      write_bootimg(bootimg);
      break;

    case create:
      if (!bootimg->kernel_fname || !bootimg->ramdisk_fname) {
        print_usage();
        break;
      }
      check_if_block_device(bootimg);
      open_bootimg(bootimg, "w");
      update_header(bootimg);
      update_images(bootimg);
      if (check_boot_img_header(bootimg))
        abort_printf("%s: Sanity cheks failed", bootimg->fname);
      write_bootimg(bootimg);
      break;
    case dtbs:
      open_bootimg(bootimg, "r");
      read_header(bootimg);
      print_dtbh_info(bootimg);
      break;
  }

  return 0;
}


