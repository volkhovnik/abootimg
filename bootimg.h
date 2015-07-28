/* tools/mkbootimg/bootimg.h
**
** Copyright 2007, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License"); 
** you may not use this file except in compliance with the License. 
** You may obtain a copy of the License at 
**
**     http://www.apache.org/licenses/LICENSE-2.0 
**
** Unless required by applicable law or agreed to in writing, software 
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
** See the License for the specific language governing permissions and 
** limitations under the License.
*/

#ifndef _BOOT_IMAGE_H_
#define _BOOT_IMAGE_H_

typedef struct boot_img_hdr boot_img_hdr;

#define BOOT_MAGIC "ANDROID!"
#define BOOT_MAGIC_SIZE 8
#define BOOT_NAME_SIZE 16
#define BOOT_ARGS_SIZE 512

struct boot_img_hdr
{
    unsigned char magic[BOOT_MAGIC_SIZE];

    unsigned kernel_size;  /* size in bytes */
    unsigned kernel_addr;  /* physical load addr */

    unsigned ramdisk_size; /* size in bytes */
    unsigned ramdisk_addr; /* physical load addr */

    unsigned second_size;  /* size in bytes */
    unsigned second_addr;  /* physical load addr */

    unsigned tags_addr;    /* physical addr for kernel tags */
    unsigned page_size;    /* flash page size we assume */

    unsigned dtbs_size;    /* size in bytes */

    unsigned unused[1];    /* future expansion: should be 0 */

    unsigned char name[BOOT_NAME_SIZE]; /* asciiz product name */
    
    unsigned char cmdline[BOOT_ARGS_SIZE];

    unsigned id[8]; /* timestamp / checksum / sha1 / etc */
};

/*
** +-----------------+ 
** | boot header     | 1 page
** +-----------------+
** | kernel          | n pages  
** +-----------------+
** | ramdisk         | m pages  
** +-----------------+
** | second stage    | o pages
** +-----------------+
** | device trees    | p pages
** +-----------------+
** | signature       | 1 pages
** +-----------------+
**
** n = (kernel_size + page_size - 1) / page_size
** m = (ramdisk_size + page_size - 1) / page_size
** o = (second_size + page_size - 1) / page_size
** p = (dtbs_size + page_size - 1) / page_size
**
** 0. all entities are page_size aligned in flash
** 1. kernel and ramdisk are required (size != 0)
** 2. second is optional (second_size == 0 -> no second)
** 3. load each element (kernel, ramdisk, second) at
**    the specified physical address (kernel_addr, etc)
** 4. prepare tags at tag_addr.  kernel_args[] is
**    appended to the kernel commandline in the tags.
** 5. r0 = 0, r1 = MACHINE_TYPE, r2 = tags_addr
** 6. if second_size != 0: jump to second_addr
**    else: jump to kernel_addr
*/

#if 0
typedef struct ptentry ptentry;

struct ptentry {
    char name[16];      /* asciiz partition name    */
    unsigned start;     /* starting block number    */
    unsigned length;    /* length in blocks         */
    unsigned flags;     /* set to zero              */
};

/* MSM Partition Table ATAG
**
** length: 2 + 7 * n
** atag:   0x4d534d70
**         <ptentry> x n
*/
#endif


typedef struct device_info_t device_info_t;

struct device_info_t {
  unsigned chip_id;
  unsigned platform_id;
  unsigned subtype_id;
  unsigned hw_rev;
};

typedef struct dt_entry_t dt_entry_t;
struct dt_entry_t {
  unsigned chip_id;
  unsigned platform_id;
  unsigned subtype_id;
  unsigned hw_rev;
  unsigned hw_rev_end;
  unsigned offset;
  unsigned dtb_size;
  char padding[2];
};

typedef struct dtbs_t dtbs_t;

struct dtbs_t {
  unsigned magic;
  unsigned version;
  unsigned num_entries;
  //device_info_t device_info;
};


/*<dtbh_header Info>
    magic:0x48425444, version:0x00000002, num_entries:0x00000009

<device info>
        chip_id: 0x00001cfc
    platform_id: 0x000050a6
     subtype_id: 0x217584da
         hw_rev: 0x0000000b

dt_entry[00]
        chip_id: 0x00001cfc
    platform_id: 0x000050a6
     subtype_id: 0x217584da
         hw_rev: 0x00000000
     hw_rev_end: 0x00000000
         offset: 0x00000800
       dtb size: 0x0002b000
dt_entry[01]
        chip_id: 0x00001cfc
    platform_id: 0x000050a6
     subtype_id: 0x217584da
         hw_rev: 0x00000003
     hw_rev_end: 0x00000003
         offset: 0x0002b800
       dtb size: 0x0002b800
dt_entry[02]
        chip_id: 0x00001cfc
    platform_id: 0x000050a6
     subtype_id: 0x217584da
         hw_rev: 0x00000005
     hw_rev_end: 0x00000005
         offset: 0x00057000
       dtb size: 0x0002c000
dt_entry[03]
        chip_id: 0x00001cfc
    platform_id: 0x000050a6
     subtype_id: 0x217584da
         hw_rev: 0x00000006
     hw_rev_end: 0x00000006
         offset: 0x00083000
       dtb size: 0x0002c000
dt_entry[04]
        chip_id: 0x00001cfc
    platform_id: 0x000050a6
     subtype_id: 0x217584da
         hw_rev: 0x00000007
     hw_rev_end: 0x00000007
         offset: 0x000af000
       dtb size: 0x0002c000
dt_entry[05]
        chip_id: 0x00001cfc
    platform_id: 0x000050a6
     subtype_id: 0x217584da
         hw_rev: 0x00000008
     hw_rev_end: 0x00000009
         offset: 0x000db000
       dtb size: 0x0002c000
dt_entry[06]
        chip_id: 0x00001cfc
    platform_id: 0x000050a6
     subtype_id: 0x217584da
         hw_rev: 0x0000000a
     hw_rev_end: 0x0000000b
         offset: 0x00107000
       dtb size: 0x0002c000
Selected entry hw_ver : 11
dt_entry of hw_rev 10 is loaded at 0x4a000000.(180224 Bytes)
*/

#endif
