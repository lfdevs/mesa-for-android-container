/*
 * Copyright © 2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nv_device_info.h"
#include "nv_push.h"

#include "cl902d.h"

#include "cla040.h"
#include "cla140.h"

#include "cla097.h"
#include "cla197.h"
#include "cla297.h"
#include "clb097.h"
#include "clb197.h"
#include "clc097.h"
#include "clc197.h"
#include "clc397.h"
#include "clc597.h"
#include "clc697.h"
#include "clc797.h"
#include "clc997.h"
#include "clcb97.h"
#include "clcd97.h"
#include "clce97.h"

#include "cla06f.h"
#include "cla16f.h"
#include "cla26f.h"
#include "clb06f.h"
#include "clc06f.h"
#include "clc36f.h"
#include "clc46f.h"
#include "clc56f.h"

/* AMPERE_CHANNEL_GPFIFO_B has one typo and as we are replacing
 * headers with the ones from the open-gpu-doc repo, let's hack around this for
 * now
 * XXX: Remove this once it's fixed
 */
#define AmpereAControlGPFifo AmpereBControlGPFifo
#include "clc76f.h"
#undef AmpereAControlGPFifo

#include "clc86f.h"
#include "clc96f.h"
#include "clca6f.h"

#include "cla0b5.h"
#include "clb0b5.h"
#include "clc0b5.h"
#include "clc1b5.h"
#include "clc3b5.h"
#include "clc5b5.h"
#include "clc6b5.h"
#include "clc9b5.h"
#include "clcab5.h"

#include "cla0c0.h"
#include "cla1c0.h"
#include "clb0c0.h"
#include "clb1c0.h"
#include "clc0c0.h"
#include "clc1c0.h"
#include "clc3c0.h"
#include "clc5c0.h"
#include "clc6c0.h"
#include "clc7c0.h"
#include "clc9c0.h"
#include "clcbc0.h"
#include "clcdc0.h"
#include "clcec0.h"

#include "util/macros.h"

struct device_info {
   const char *gen_name;
   const char *alias_name;
   uint16_t cls_eng3d;
   uint16_t cls_compute;
   uint16_t cls_copy;
   uint16_t cls_m2mf;
   uint16_t cls_gpfifo;
};

struct device_info fake_devices[] = {
   {"KEPLER_A", "KEPLER", KEPLER_A, KEPLER_COMPUTE_A, KEPLER_DMA_COPY_A, KEPLER_INLINE_TO_MEMORY_A, KEPLER_CHANNEL_GPFIFO_A},
   {"KEPLER_B", NULL, KEPLER_B, KEPLER_COMPUTE_B, KEPLER_DMA_COPY_A, KEPLER_INLINE_TO_MEMORY_B, KEPLER_CHANNEL_GPFIFO_B},
   {"KEPLER_C", NULL, KEPLER_C, KEPLER_COMPUTE_B, KEPLER_DMA_COPY_A, KEPLER_INLINE_TO_MEMORY_B, KEPLER_CHANNEL_GPFIFO_C},
   {"MAXWELL_A", "MAXWELL", MAXWELL_A, MAXWELL_COMPUTE_A, MAXWELL_DMA_COPY_A, KEPLER_INLINE_TO_MEMORY_B, MAXWELL_CHANNEL_GPFIFO_A},
   {"MAXWELL_B", NULL, MAXWELL_B, MAXWELL_COMPUTE_B, MAXWELL_DMA_COPY_A, KEPLER_INLINE_TO_MEMORY_B, MAXWELL_CHANNEL_GPFIFO_A},
   {"PASCAL_A", "PASCAL", PASCAL_A, PASCAL_COMPUTE_A, PASCAL_DMA_COPY_A, KEPLER_INLINE_TO_MEMORY_B, PASCAL_CHANNEL_GPFIFO_A},
   {"PASCAL_B", NULL, PASCAL_B, PASCAL_COMPUTE_B, PASCAL_DMA_COPY_B, KEPLER_INLINE_TO_MEMORY_B, PASCAL_CHANNEL_GPFIFO_A},
   {"VOLTA_A", "VOLTA", VOLTA_A, VOLTA_COMPUTE_A, VOLTA_DMA_COPY_A, KEPLER_INLINE_TO_MEMORY_B, VOLTA_CHANNEL_GPFIFO_A},
   {"TURING_A", "TURING", TURING_A, TURING_COMPUTE_A, TURING_DMA_COPY_A, KEPLER_INLINE_TO_MEMORY_B, TURING_CHANNEL_GPFIFO_A},
   {"AMPERE_A", "AMPERE", AMPERE_A, AMPERE_COMPUTE_A, AMPERE_DMA_COPY_A, KEPLER_INLINE_TO_MEMORY_B, AMPERE_CHANNEL_GPFIFO_A},
   {"AMPERE_B", NULL, AMPERE_B, AMPERE_COMPUTE_B, AMPERE_DMA_COPY_A, KEPLER_INLINE_TO_MEMORY_B, AMPERE_CHANNEL_GPFIFO_B},
   {"ADA_A", "ADA", ADA_A, ADA_COMPUTE_A, AMPERE_DMA_COPY_A, KEPLER_INLINE_TO_MEMORY_B, AMPERE_CHANNEL_GPFIFO_B},
   {"HOPPER_A", "HOPPER", HOPPER_A, HOPPER_COMPUTE_A, AMPERE_DMA_COPY_A, KEPLER_INLINE_TO_MEMORY_B, HOPPER_CHANNEL_GPFIFO_A},
   {"BLACKWELL_A", NULL, BLACKWELL_A, BLACKWELL_COMPUTE_A, BLACKWELL_DMA_COPY_A, KEPLER_INLINE_TO_MEMORY_B, BLACKWELL_CHANNEL_GPFIFO_A},
   {"BLACKWELL_B", NULL, BLACKWELL_B, BLACKWELL_COMPUTE_B, BLACKWELL_DMA_COPY_B, KEPLER_INLINE_TO_MEMORY_B, BLACKWELL_CHANNEL_GPFIFO_B},
};

static struct nv_device_info
get_fake_device_info(const char *arch_name)
{
   struct nv_device_info info;

   memset(&info, 0, sizeof(info));
   info.cls_eng2d = FERMI_TWOD_A;

   for (int i = 0; i < ARRAY_SIZE(fake_devices); i++) {
      const struct device_info *fake_device = &fake_devices[i];

      if ((fake_device->alias_name && !strcmp(arch_name, fake_device->alias_name)) ||
          !strcmp(arch_name, fake_device->gen_name)) {
         info.cls_eng3d = fake_device->cls_eng3d;
         info.cls_compute = fake_device->cls_compute;
         info.cls_copy = fake_device->cls_copy;
         info.cls_m2mf = fake_device->cls_m2mf;
         info.cls_gpfifo = fake_device->cls_gpfifo;

         return info;
      }
   }

   fprintf(stderr, "Unknown architecture \"%s\", defaulting to Turing",
           arch_name);
   info.cls_eng3d = TURING_A;
   info.cls_compute = TURING_COMPUTE_A;
   info.cls_copy = TURING_DMA_COPY_A;
   info.cls_gpfifo = TURING_CHANNEL_GPFIFO_A;
   info.cls_m2mf = KEPLER_INLINE_TO_MEMORY_B;

   return info;
}

int
main(int argc, char **argv)
{
   const char *arch_name;
   const char *file_name;
   FILE *file;
   long file_size;
   uint32_t *data;
   struct nv_device_info device_info;
   struct nv_push pushbuf;

   if (argc != 3) {
      fprintf(stderr, "Usage: nv_push_dump file.bin "
                      "<KEPLER|MAXWELL|VOLTA|TURING|AMPERE|ADA|HOPPER|BLACKWELL_A|BLACKWELL_B>\n");
      return 1;
   }

   file_name = argv[1];
   arch_name = argv[2];

   device_info = get_fake_device_info(arch_name);

   file = fopen(file_name, "r");

   if (file == NULL) {
      fprintf(stderr, "couldn't open file \"%s\"\n", file_name);
      return 1;
   }

   fseek(file, 0L, SEEK_END);
   file_size = ftell(file);
   fseek(file, 0L, SEEK_SET);

   if (file_size % 4 != 0) {
      fclose(file);

      fprintf(stderr, "invalid file, data isn't aligned to 4 bytes\n");
      return 1;
   }

   data = malloc(file_size);

   if (data == NULL) {
      fclose(file);

      fprintf(stderr, "memory allocation failed\n");
      return 1;
   }

   fread(data, file_size, 1, file);
   fclose(file);

   nv_push_init(&pushbuf, data, file_size / 4, SUBC_MASK_ALL);
   pushbuf.end = pushbuf.limit;

   vk_push_print(stdout, &pushbuf, &device_info);

   free(data);

   return 0;
}