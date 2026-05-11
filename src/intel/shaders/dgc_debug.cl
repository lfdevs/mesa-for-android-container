/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "libintel_shaders.h"

#define CMD(cmd_type, cmd_subtype, _opcode, _subopcode) \
   (((cmd_type) << 29) | \
    ((cmd_subtype) << 27) | \
    ((_opcode) << 24) | \
    ((_subopcode) << 16))
#define CMD5(cmd_type, cmd_subtype, _opcode, _subopcode, _variant) \
   (((cmd_type) << 29) | \
    ((cmd_subtype) << 27) | \
    ((_opcode) << 24) | \
    ((_subopcode) << 18) | \
    ((_variant) << 16))

void
genX(libanv_dgc_dump)(global uint32_t *cmd_base,
                      uint32_t n_dwords,
                      global void *call_addr)
{
   printf("call from 0x%016lx\n", call_addr);
   uint32_t bbs_count = 0;
   for (uint32_t i = 0; i < n_dwords && bbs_count < 2; ) {
      uint32_t n_dwords = cmd_base[i] & 0xff;
      uint32_t bias_dwords = 0;
      printf("0x%016lx: ", cmd_base + i);
      switch (cmd_base[i] & 0xffff0000) {
      case CMD(3, 3, 3, 0):
         printf("3DPRIMITIVE\n");
         bias_dwords = 2;
         break;
      case CMD(3, 3, 3, 2):
         printf("3DMESH_3D\n");
         bias_dwords = 2;
         break;
      case CMD(3, 3, 0, 21):
         printf("3DSTATE_CONSTANT_VS\n");
         bias_dwords = 2;
         break;
      case CMD(3, 3, 0, 25):
         printf("3DSTATE_CONSTANT_HS\n");
         bias_dwords = 2;
         break;
      case CMD(3, 3, 0, 26):
         printf("3DSTATE_CONSTANT_DS\n");
         bias_dwords = 2;
         break;
      case CMD(3, 3, 0, 22):
         printf("3DSTATE_CONSTANT_GS\n");
         bias_dwords = 2;
         break;
      case CMD(3, 3, 0, 23):
         printf("3DSTATE_CONSTANT_PS\n");
         bias_dwords = 2;
         break;
      case CMD(3, 3, 0, 109):
         printf("3DSTATE_CONSTANT_ALL\n");
         bias_dwords = 2;
         break;
      case CMD(3, 3, 0, 123):
         printf("3DSTATE_MESH_SHADER_DATA\n");
         bias_dwords = 2;
         break;
      case CMD(3, 3, 0, 126):
         printf("3DSTATE_TASK_SHADER_DATA\n");
         bias_dwords = 2;
         break;
      case CMD(3, 3, 0, 10):
         printf("3DSTATE_INDEX_BUFFER\n");
         bias_dwords = 2;
         break;
      case CMD(3, 3, 0, 8):
         printf("3DSTATE_VERTEX_BUFFERS\n");
         bias_dwords = 2;
         break;
      case CMD(3, 3, 0, 27):
         printf("3DSTATE_HS\n");
         bias_dwords = 2;
         break;
      case CMD(3, 3, 0, 29):
         printf("3DSTATE_DS\n");
         bias_dwords = 2;
         break;
      case CMD(3, 2, 0, 4):
         printf("MEDIA_STATE_FLUSH\n");
         bias_dwords = 2;
         break;
      case CMD(3, 2, 0, 0):
         printf("MEDIA_VFE_STATE\n");
         bias_dwords = 2;
         break;
      case CMD(3, 2, 0, 1):
         printf("MEDIA_CURBE_LOAD\n");
         bias_dwords = 2;
         break;
      case CMD(3, 2, 0, 2):
         printf("MEDIA_INTERFACE_DESCRIPTOR_LOAD\n");
         bias_dwords = 2;
         break;
      case CMD(3, 2, 1, 5):
         printf("GPGPU_WALKER\n");
         bias_dwords = 2;
         break;
      case CMD5(3, 2, 2, 2, 0):
         printf("COMPUTE_WALKER\n");
         bias_dwords = 2;
         break;
      case 0x10000000:
         printf("MI_STORE_DATA_IMM\n");
         bias_dwords = 2;
         break;
      case 0x02800000:
         printf("MI_ARB_CHECK\n");
         bias_dwords = 1;
         break;
      case 0x18800000:
         printf("MI_BATCH_BUFFER_START\n");
         bias_dwords = 2;
         bbs_count++;
         break;
      case 0x00000000:
         printf("MI_NOOP\n");
         bias_dwords = 1;
         break;
      default:
         printf("unknown : 0x%08x\n", cmd_base[i]);
         return;
      }

      printf("   ");
      for (uint32_t j = 0; j < (bias_dwords + n_dwords); j++) {
         if (j > 0 && (j % 8) == 0)
            printf("\n   ");
         printf("0x%08x ", cmd_base[i + j]);
      }
      printf("\n");

      i += bias_dwords + n_dwords;
   }
}
