/*
 * Copyright © 2019 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <fcntl.h>
#include <string.h>
#include <xf86drm.h>

#include <gtest/gtest.h>

#include "dev/gen_device_info.h"
#include "drm-uapi/i915_drm.h"
#include "genxml/gen_macros.h"
#include "util/macros.h"

class gen_mi_builder_test;

struct address {
   uint32_t gem_handle;
   uint32_t offset;
};

#define __gen_address_type struct address
#define __gen_user_data ::gen_mi_builder_test

uint64_t __gen_combine_address(gen_mi_builder_test *test, void *location,
                               struct address addr, uint32_t delta);
void * __gen_get_batch_dwords(gen_mi_builder_test *test, unsigned num_dwords);

struct address
__gen_address_offset(address addr, uint64_t offset)
{
   addr.offset += offset;
   return addr;
}

#if GEN_GEN >= 8 || GEN_IS_HASWELL
#define RSVD_TEMP_REG 0x2678 /* MI_ALU_REG15 */
#else
#define RSVD_TEMP_REG 0x2430 /* GEN7_3DPRIM_START_VERTEX */
#endif
#define GEN_MI_BUILDER_NUM_ALLOC_GPRS 15
#define INPUT_DATA_OFFSET 0
#define OUTPUT_DATA_OFFSET 2048

#define __genxml_cmd_length(cmd) cmd ## _length
#define __genxml_cmd_length_bias(cmd) cmd ## _length_bias
#define __genxml_cmd_header(cmd) cmd ## _header
#define __genxml_cmd_pack(cmd) cmd ## _pack

#include "genxml/genX_pack.h"
#include "gen_mi_builder.h"

#define emit_cmd(cmd, name)                                           \
   for (struct cmd name = { __genxml_cmd_header(cmd) },               \
        *_dst = (struct cmd *) emit_dwords(__genxml_cmd_length(cmd)); \
        __builtin_expect(_dst != NULL, 1);                            \
        __genxml_cmd_pack(cmd)(this, (void *)_dst, &name), _dst = NULL)

#include <vector>

class gen_mi_builder_test : public ::testing::Test {
public:
   gen_mi_builder_test();
   ~gen_mi_builder_test();

   void SetUp();

   void *emit_dwords(int num_dwords);
   void submit_batch();

   inline address in_addr(uint32_t offset)
   {
      address addr;
      addr.gem_handle = data_bo_handle;
      addr.offset = INPUT_DATA_OFFSET + offset;
      return addr;
   }

   inline address out_addr(uint32_t offset)
   {
      address addr;
      addr.gem_handle = data_bo_handle;
      addr.offset = OUTPUT_DATA_OFFSET + offset;
      return addr;
   }

   inline gen_mi_value in_mem64(uint32_t offset)
   {
      return gen_mi_mem64(in_addr(offset));
   }

   inline gen_mi_value in_mem32(uint32_t offset)
   {
      return gen_mi_mem32(in_addr(offset));
   }

   inline gen_mi_value out_mem64(uint32_t offset)
   {
      return gen_mi_mem64(out_addr(offset));
   }

   inline gen_mi_value out_mem32(uint32_t offset)
   {
      return gen_mi_mem32(out_addr(offset));
   }

   int fd;
   gen_device_info devinfo;

   uint32_t batch_bo_handle;
   uint32_t batch_offset;
   void *batch_map;

   std::vector<drm_i915_gem_relocation_entry> relocs;

   uint32_t data_bo_handle;
   void *data_map;
   char *input;
   char *output;
   uint64_t canary;

   gen_mi_builder b;
};

gen_mi_builder_test::gen_mi_builder_test() :
  fd(-1)
{ }

gen_mi_builder_test::~gen_mi_builder_test()
{
   close(fd);
}

// 1 MB of batch should be enough for anyone, right?
#define BATCH_BO_SIZE (256 * 4096)
#define DATA_BO_SIZE 4096

void
gen_mi_builder_test::SetUp()
{
   drmDevicePtr devices[8];
   int max_devices = drmGetDevices2(0, devices, 8);

   int i;
   for (i = 0; i < max_devices; i++) {
      if (devices[i]->available_nodes & 1 << DRM_NODE_RENDER &&
          devices[i]->bustype == DRM_BUS_PCI &&
          devices[i]->deviceinfo.pci->vendor_id == 0x8086) {
         fd = open(devices[i]->nodes[DRM_NODE_RENDER], O_RDWR | O_CLOEXEC);
         if (fd < 0)
            continue;

         /* We don't really need to do this when running on hardware because
          * we can just pull it from the drmDevice.  However, without doing
          * this, intel_dump_gpu gets a bit of heartburn and we can't use the
          * --device option with it.
          */
         int device_id;
         drm_i915_getparam getparam = drm_i915_getparam();
         getparam.param = I915_PARAM_CHIPSET_ID;
         getparam.value = &device_id;
         ASSERT_EQ(drmIoctl(fd, DRM_IOCTL_I915_GETPARAM,
                            (void *)&getparam), 0) << strerror(errno);

         ASSERT_TRUE(gen_get_device_info_from_pci_id(device_id, &devinfo));
         if (devinfo.gen != GEN_GEN || devinfo.is_haswell != GEN_IS_HASWELL) {
            close(fd);
            fd = -1;
            continue;
         }


         /* Found a device! */
         break;
      }
   }
   ASSERT_TRUE(i < max_devices) << "Failed to find a DRM device";

   // Create the batch buffer
   drm_i915_gem_create gem_create = drm_i915_gem_create();
   gem_create.size = BATCH_BO_SIZE;
   ASSERT_EQ(drmIoctl(fd, DRM_IOCTL_I915_GEM_CREATE,
                      (void *)&gem_create), 0) << strerror(errno);
   batch_bo_handle = gem_create.handle;

   drm_i915_gem_caching gem_caching = drm_i915_gem_caching();
   gem_caching.handle = batch_bo_handle;
   gem_caching.caching = I915_CACHING_CACHED;
   ASSERT_EQ(drmIoctl(fd, DRM_IOCTL_I915_GEM_SET_CACHING,
                      (void *)&gem_caching), 0) << strerror(errno);

   drm_i915_gem_mmap gem_mmap = drm_i915_gem_mmap();
   gem_mmap.handle = batch_bo_handle;
   gem_mmap.offset = 0;
   gem_mmap.size = BATCH_BO_SIZE;
   gem_mmap.flags = 0;
   ASSERT_EQ(drmIoctl(fd, DRM_IOCTL_I915_GEM_MMAP,
                      (void *)&gem_mmap), 0) << strerror(errno);
   batch_map = (void *)(uintptr_t)gem_mmap.addr_ptr;

   // Start the batch at zero
   batch_offset = 0;

   // Create the data buffer
   gem_create = drm_i915_gem_create();
   gem_create.size = DATA_BO_SIZE;
   ASSERT_EQ(drmIoctl(fd, DRM_IOCTL_I915_GEM_CREATE,
                      (void *)&gem_create), 0) << strerror(errno);
   data_bo_handle = gem_create.handle;

   gem_caching = drm_i915_gem_caching();
   gem_caching.handle = data_bo_handle;
   gem_caching.caching = I915_CACHING_CACHED;
   ASSERT_EQ(drmIoctl(fd, DRM_IOCTL_I915_GEM_SET_CACHING,
                      (void *)&gem_caching), 0) << strerror(errno);

   gem_mmap = drm_i915_gem_mmap();
   gem_mmap.handle = data_bo_handle;
   gem_mmap.offset = 0;
   gem_mmap.size = DATA_BO_SIZE;
   gem_mmap.flags = 0;
   ASSERT_EQ(drmIoctl(fd, DRM_IOCTL_I915_GEM_MMAP,
                      (void *)&gem_mmap), 0) << strerror(errno);
   data_map = (void *)(uintptr_t)gem_mmap.addr_ptr;
   input = (char *)data_map + INPUT_DATA_OFFSET;
   output = (char *)data_map + OUTPUT_DATA_OFFSET;

   // Fill the test data with garbage
   memset(data_map, 139, DATA_BO_SIZE);
   memset(&canary, 139, sizeof(canary));

   gen_mi_builder_init(&b, this);
}

void *
gen_mi_builder_test::emit_dwords(int num_dwords)
{
   void *ptr = (void *)((char *)batch_map + batch_offset);
   batch_offset += num_dwords * 4;
   assert(batch_offset < BATCH_BO_SIZE);
   return ptr;
}

void
gen_mi_builder_test::submit_batch()
{
   gen_mi_builder_emit(&b, GENX(MI_BATCH_BUFFER_END), bbe);

   // Round batch up to an even number of dwords.
   if (batch_offset & 4)
      gen_mi_builder_emit(&b, GENX(MI_NOOP), noop);

   drm_i915_gem_exec_object2 objects[2];
   memset(objects, 0, sizeof(objects));

   objects[0].handle = data_bo_handle;
   objects[0].relocation_count = 0;
   objects[0].relocs_ptr = 0;
   objects[0].flags = EXEC_OBJECT_WRITE;
   objects[0].offset = -1;
   if (GEN_GEN >= 8)
      objects[0].flags |= EXEC_OBJECT_SUPPORTS_48B_ADDRESS;

   objects[1].handle = batch_bo_handle;
   objects[1].relocation_count = relocs.size();
   objects[1].relocs_ptr = (uintptr_t)(void *)&relocs[0];
   objects[1].flags = 0;
   objects[1].offset = -1;
   if (GEN_GEN >= 8)
      objects[1].flags |= EXEC_OBJECT_SUPPORTS_48B_ADDRESS;

   drm_i915_gem_execbuffer2 execbuf = drm_i915_gem_execbuffer2();
   execbuf.buffers_ptr = (uintptr_t)(void *)objects;
   execbuf.buffer_count = 2;
   execbuf.batch_start_offset = 0;
   execbuf.batch_len = batch_offset;
   execbuf.flags = I915_EXEC_HANDLE_LUT | I915_EXEC_RENDER;

   ASSERT_EQ(drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2,
                      (void *)&execbuf), 0) << strerror(errno);

   drm_i915_gem_wait gem_wait = drm_i915_gem_wait();
   gem_wait.bo_handle = batch_bo_handle;
   gem_wait.timeout_ns = INT64_MAX;
   ASSERT_EQ(drmIoctl(fd, DRM_IOCTL_I915_GEM_WAIT,
                      (void *)&gem_wait), 0) << strerror(errno);
}

uint64_t
__gen_combine_address(gen_mi_builder_test *test, void *location,
                      address addr, uint32_t delta)
{
   drm_i915_gem_relocation_entry reloc = drm_i915_gem_relocation_entry();
   reloc.target_handle = addr.gem_handle == test->data_bo_handle ? 0 : 1;
   reloc.delta = addr.offset + delta;
   reloc.offset = (char *)location - (char *)test->batch_map;
   reloc.presumed_offset = -1;
   test->relocs.push_back(reloc);

   return reloc.delta;
}

void *
__gen_get_batch_dwords(gen_mi_builder_test *test, unsigned num_dwords)
{
   return test->emit_dwords(num_dwords);
}

#include "genxml/genX_pack.h"
#include "gen_mi_builder.h"

TEST_F(gen_mi_builder_test, imm_mem)
{
   const uint64_t value = 0x0123456789abcdef;

   gen_mi_store(&b, out_mem64(0), gen_mi_imm(value));
   gen_mi_store(&b, out_mem32(8), gen_mi_imm(value));

   submit_batch();

   // 64 -> 64
   EXPECT_EQ(*(uint64_t *)(output + 0),  value);

   // 64 -> 32
   EXPECT_EQ(*(uint32_t *)(output + 8),  (uint32_t)value);
   EXPECT_EQ(*(uint32_t *)(output + 12), (uint32_t)canary);
}

/* mem -> mem copies are only supported on HSW+ */
#if GEN_GEN >= 8 || GEN_IS_HASWELL
TEST_F(gen_mi_builder_test, mem_mem)
{
   const uint64_t value = 0x0123456789abcdef;
   *(uint64_t *)input = value;

   gen_mi_store(&b, out_mem64(0),   in_mem64(0));
   gen_mi_store(&b, out_mem32(8),   in_mem64(0));
   gen_mi_store(&b, out_mem32(16),  in_mem32(0));
   gen_mi_store(&b, out_mem64(24),  in_mem32(0));

   submit_batch();

   // 64 -> 64
   EXPECT_EQ(*(uint64_t *)(output + 0),  value);

   // 64 -> 32
   EXPECT_EQ(*(uint32_t *)(output + 8),  (uint32_t)value);
   EXPECT_EQ(*(uint32_t *)(output + 12), (uint32_t)canary);

   // 32 -> 32
   EXPECT_EQ(*(uint32_t *)(output + 16), (uint32_t)value);
   EXPECT_EQ(*(uint32_t *)(output + 20), (uint32_t)canary);

   // 32 -> 64
   EXPECT_EQ(*(uint64_t *)(output + 24), (uint64_t)(uint32_t)value);
}
#endif

TEST_F(gen_mi_builder_test, imm_reg)
{
   const uint64_t value = 0x0123456789abcdef;

   gen_mi_store(&b, gen_mi_reg64(RSVD_TEMP_REG), gen_mi_imm(canary));
   gen_mi_store(&b, gen_mi_reg64(RSVD_TEMP_REG), gen_mi_imm(value));
   gen_mi_store(&b, out_mem64(0), gen_mi_reg64(RSVD_TEMP_REG));

   gen_mi_store(&b, gen_mi_reg64(RSVD_TEMP_REG), gen_mi_imm(canary));
   gen_mi_store(&b, gen_mi_reg32(RSVD_TEMP_REG), gen_mi_imm(value));
   gen_mi_store(&b, out_mem64(8), gen_mi_reg64(RSVD_TEMP_REG));

   submit_batch();

   // 64 -> 64
   EXPECT_EQ(*(uint64_t *)(output + 0),  value);

   // 64 -> 32
   EXPECT_EQ(*(uint32_t *)(output + 8),  (uint32_t)value);
   EXPECT_EQ(*(uint32_t *)(output + 12), (uint32_t)canary);
}

TEST_F(gen_mi_builder_test, mem_reg)
{
   const uint64_t value = 0x0123456789abcdef;
   *(uint64_t *)input = value;

   gen_mi_store(&b, gen_mi_reg64(RSVD_TEMP_REG), gen_mi_imm(canary));
   gen_mi_store(&b, gen_mi_reg64(RSVD_TEMP_REG), in_mem64(0));
   gen_mi_store(&b, out_mem64(0), gen_mi_reg64(RSVD_TEMP_REG));

   gen_mi_store(&b, gen_mi_reg64(RSVD_TEMP_REG), gen_mi_imm(canary));
   gen_mi_store(&b, gen_mi_reg32(RSVD_TEMP_REG), in_mem64(0));
   gen_mi_store(&b, out_mem64(8), gen_mi_reg64(RSVD_TEMP_REG));

   gen_mi_store(&b, gen_mi_reg64(RSVD_TEMP_REG), gen_mi_imm(canary));
   gen_mi_store(&b, gen_mi_reg32(RSVD_TEMP_REG), in_mem32(0));
   gen_mi_store(&b, out_mem64(16), gen_mi_reg64(RSVD_TEMP_REG));

   gen_mi_store(&b, gen_mi_reg64(RSVD_TEMP_REG), gen_mi_imm(canary));
   gen_mi_store(&b, gen_mi_reg64(RSVD_TEMP_REG), in_mem32(0));
   gen_mi_store(&b, out_mem64(24), gen_mi_reg64(RSVD_TEMP_REG));

   submit_batch();

   // 64 -> 64
   EXPECT_EQ(*(uint64_t *)(output + 0),  value);

   // 64 -> 32
   EXPECT_EQ(*(uint32_t *)(output + 8),  (uint32_t)value);
   EXPECT_EQ(*(uint32_t *)(output + 12), (uint32_t)canary);

   // 32 -> 32
   EXPECT_EQ(*(uint32_t *)(output + 16), (uint32_t)value);
   EXPECT_EQ(*(uint32_t *)(output + 20), (uint32_t)canary);

   // 32 -> 64
   EXPECT_EQ(*(uint64_t *)(output + 24), (uint64_t)(uint32_t)value);
}

TEST_F(gen_mi_builder_test, memset)
{
   const unsigned memset_size = 256;

   gen_mi_memset(&b, out_addr(0), 0xdeadbeef, memset_size);

   submit_batch();

   uint32_t *out_u32 = (uint32_t *)output;
   for (unsigned i = 0; i <  memset_size / sizeof(*out_u32); i++)
      EXPECT_EQ(out_u32[i], 0xdeadbeef);
}

TEST_F(gen_mi_builder_test, memcpy)
{
   const unsigned memcpy_size = 256;

   uint8_t *in_u8 = (uint8_t *)input;
   for (unsigned i = 0; i < memcpy_size; i++)
      in_u8[i] = i;

   gen_mi_memcpy(&b, out_addr(0), in_addr(0), 256);

   submit_batch();

   uint8_t *out_u8 = (uint8_t *)output;
   for (unsigned i = 0; i < memcpy_size; i++)
      EXPECT_EQ(out_u8[i], i);
}

/* Start of MI_MATH section */
#if GEN_GEN >= 8 || GEN_IS_HASWELL

/* Test adding of immediates of all kinds including
 *
 *  - All zeroes
 *  - All ones
 *  - inverted constants
 */
TEST_F(gen_mi_builder_test, add_imm)
{
   const uint64_t value = 0x0123456789abcdef;
   const uint64_t add = 0xdeadbeefac0ffee2;
   memcpy(input, &value, sizeof(value));

   gen_mi_store(&b, out_mem64(0),
                gen_mi_iadd(&b, in_mem64(0), gen_mi_imm(0)));
   gen_mi_store(&b, out_mem64(8),
                gen_mi_iadd(&b, in_mem64(0), gen_mi_imm(-1)));
   gen_mi_store(&b, out_mem64(16),
                gen_mi_iadd(&b, in_mem64(0), gen_mi_inot(&b, gen_mi_imm(0))));
   gen_mi_store(&b, out_mem64(24),
                gen_mi_iadd(&b, in_mem64(0), gen_mi_inot(&b, gen_mi_imm(-1))));
   gen_mi_store(&b, out_mem64(32),
                gen_mi_iadd(&b, in_mem64(0), gen_mi_imm(add)));
   gen_mi_store(&b, out_mem64(40),
                gen_mi_iadd(&b, in_mem64(0), gen_mi_inot(&b, gen_mi_imm(add))));
   gen_mi_store(&b, out_mem64(48),
                gen_mi_iadd(&b, gen_mi_imm(0), in_mem64(0)));
   gen_mi_store(&b, out_mem64(56),
                gen_mi_iadd(&b, gen_mi_imm(-1), in_mem64(0)));
   gen_mi_store(&b, out_mem64(64),
                gen_mi_iadd(&b, gen_mi_inot(&b, gen_mi_imm(0)), in_mem64(0)));
   gen_mi_store(&b, out_mem64(72),
                gen_mi_iadd(&b, gen_mi_inot(&b, gen_mi_imm(-1)), in_mem64(0)));
   gen_mi_store(&b, out_mem64(80),
                gen_mi_iadd(&b, gen_mi_imm(add), in_mem64(0)));
   gen_mi_store(&b, out_mem64(88),
                gen_mi_iadd(&b, gen_mi_inot(&b, gen_mi_imm(add)), in_mem64(0)));

   // And som add_imm just for good measure
   gen_mi_store(&b, out_mem64(96), gen_mi_iadd_imm(&b, in_mem64(0), 0));
   gen_mi_store(&b, out_mem64(104), gen_mi_iadd_imm(&b, in_mem64(0), add));

   submit_batch();

   EXPECT_EQ(*(uint64_t *)(output + 0),   value);
   EXPECT_EQ(*(uint64_t *)(output + 8),   value - 1);
   EXPECT_EQ(*(uint64_t *)(output + 16),  value - 1);
   EXPECT_EQ(*(uint64_t *)(output + 24),  value);
   EXPECT_EQ(*(uint64_t *)(output + 32),  value + add);
   EXPECT_EQ(*(uint64_t *)(output + 40),  value + ~add);
   EXPECT_EQ(*(uint64_t *)(output + 48),  value);
   EXPECT_EQ(*(uint64_t *)(output + 56),  value - 1);
   EXPECT_EQ(*(uint64_t *)(output + 64),  value - 1);
   EXPECT_EQ(*(uint64_t *)(output + 72),  value);
   EXPECT_EQ(*(uint64_t *)(output + 80),  value + add);
   EXPECT_EQ(*(uint64_t *)(output + 88),  value + ~add);
   EXPECT_EQ(*(uint64_t *)(output + 96),  value);
   EXPECT_EQ(*(uint64_t *)(output + 104), value + add);
}

TEST_F(gen_mi_builder_test, ilt_uge)
{
   uint64_t values[8] = {
      0x0123456789abcdef,
      0xdeadbeefac0ffee2,
      (uint64_t)-1,
      1,
      0,
      1049571,
      (uint64_t)-240058,
      20204184,
   };
   memcpy(input, values, sizeof(values));

   for (unsigned i = 0; i < ARRAY_SIZE(values); i++) {
      for (unsigned j = 0; j < ARRAY_SIZE(values); j++) {
         gen_mi_store(&b, out_mem32(i * 64 + j * 8 + 0),
                      gen_mi_ult(&b, in_mem64(i * 8), in_mem64(j * 8)));
         gen_mi_store(&b, out_mem32(i * 64 + j * 8 + 4),
                      gen_mi_uge(&b, in_mem64(i * 8), in_mem64(j * 8)));
      }
   }

   submit_batch();

   for (unsigned i = 0; i < ARRAY_SIZE(values); i++) {
      for (unsigned j = 0; j < ARRAY_SIZE(values); j++) {
         uint32_t *out_u32 = (uint32_t *)(output + i * 64 + j * 8);
         EXPECT_EQ(out_u32[0], values[i] < values[j] ? ~0u : 0u);
         EXPECT_EQ(out_u32[1], values[i] >= values[j] ? ~0u : 0u);
      }
   }
}

TEST_F(gen_mi_builder_test, iand)
{
   const uint64_t values[2] = {
      0x0123456789abcdef,
      0xdeadbeefac0ffee2,
   };
   memcpy(input, values, sizeof(values));

   gen_mi_store(&b, out_mem64(0), gen_mi_iand(&b, in_mem64(0), in_mem64(8)));

   submit_batch();

   EXPECT_EQ(*(uint64_t *)output, values[0] & values[1]);
}

TEST_F(gen_mi_builder_test, imul_imm)
{
   uint64_t lhs[2] = {
      0x0123456789abcdef,
      0xdeadbeefac0ffee2,
   };
   memcpy(input, lhs, sizeof(lhs));

    /* Some random 32-bit unsigned integers.  The first four have been
     * hand-chosen just to ensure some good low integers; the rest were
     * generated with a python script.
     */
   uint32_t rhs[20] = {
      1,       2,       3,       5,
      10800,   193,     64,      40,
      3796,    256,     88,      473,
      1421,    706,     175,     850,
      39,      38985,   1941,    17,
   };

   for (unsigned i = 0; i < ARRAY_SIZE(lhs); i++) {
      for (unsigned j = 0; j < ARRAY_SIZE(rhs); j++) {
         gen_mi_store(&b, out_mem64(i * 160 + j * 8),
                      gen_mi_imul_imm(&b, in_mem64(i * 8), rhs[j]));
      }
   }

   submit_batch();

   for (unsigned i = 0; i < ARRAY_SIZE(lhs); i++) {
      for (unsigned j = 0; j < ARRAY_SIZE(rhs); j++) {
         EXPECT_EQ(*(uint64_t *)(output + i * 160 + j * 8), lhs[i] * rhs[j]);
      }
   }
}

TEST_F(gen_mi_builder_test, ishl_imm)
{
   const uint64_t value = 0x0123456789abcdef;
   memcpy(input, &value, sizeof(value));

   const unsigned max_shift = 64;

   for (unsigned i = 0; i <= max_shift; i++)
      gen_mi_store(&b, out_mem64(i * 8), gen_mi_ishl_imm(&b, in_mem64(0), i));

   submit_batch();

   for (unsigned i = 0; i <= max_shift; i++) {
      if (i >= 64) {
         EXPECT_EQ(*(uint64_t *)(output + i * 8), 0);
      } else {
         EXPECT_EQ(*(uint64_t *)(output + i * 8), value << i);
      }
   }
}

TEST_F(gen_mi_builder_test, ushr32_imm)
{
   const uint64_t value = 0x0123456789abcdef;
   memcpy(input, &value, sizeof(value));

   const unsigned max_shift = 64;

   for (unsigned i = 0; i <= max_shift; i++)
      gen_mi_store(&b, out_mem64(i * 8), gen_mi_ushr32_imm(&b, in_mem64(0), i));

   submit_batch();

   for (unsigned i = 0; i <= max_shift; i++) {
      if (i >= 64) {
         EXPECT_EQ(*(uint64_t *)(output + i * 8), 0);
      } else {
         EXPECT_EQ(*(uint64_t *)(output + i * 8), (value >> i) & UINT32_MAX);
      }
   }
}

TEST_F(gen_mi_builder_test, udiv32_imm)
{
    /* Some random 32-bit unsigned integers.  The first four have been
     * hand-chosen just to ensure some good low integers; the rest were
     * generated with a python script.
     */
   uint32_t values[20] = {
      1,       2,       3,       5,
      10800,   193,     64,      40,
      3796,    256,     88,      473,
      1421,    706,     175,     850,
      39,      38985,   1941,    17,
   };
   memcpy(input, values, sizeof(values));

   for (unsigned i = 0; i < ARRAY_SIZE(values); i++) {
      for (unsigned j = 0; j < ARRAY_SIZE(values); j++) {
         gen_mi_store(&b, out_mem32(i * 80 + j * 4),
                      gen_mi_udiv32_imm(&b, in_mem32(i * 4), values[j]));
      }
   }

   submit_batch();

   for (unsigned i = 0; i < ARRAY_SIZE(values); i++) {
      for (unsigned j = 0; j < ARRAY_SIZE(values); j++) {
         EXPECT_EQ(*(uint32_t *)(output + i * 80 + j * 4),
                   values[i] / values[j]);
      }
   }
}

TEST_F(gen_mi_builder_test, store_if)
{
   uint64_t u64 = 0xb453b411deadc0deull;
   uint32_t u32 = 0x1337d00d;

   /* Write values with the predicate enabled */
   emit_cmd(GENX(MI_PREDICATE), mip) {
      mip.LoadOperation    = LOAD_LOAD;
      mip.CombineOperation = COMBINE_SET;
      mip.CompareOperation = COMPARE_TRUE;
   }

   gen_mi_store_if(&b, out_mem64(0), gen_mi_imm(u64));
   gen_mi_store_if(&b, out_mem32(8), gen_mi_imm(u32));

   /* Set predicate to false, write garbage that shouldn't land */
   emit_cmd(GENX(MI_PREDICATE), mip) {
      mip.LoadOperation    = LOAD_LOAD;
      mip.CombineOperation = COMBINE_SET;
      mip.CompareOperation = COMPARE_FALSE;
   }

   gen_mi_store_if(&b, out_mem64(0), gen_mi_imm(0xd0d0d0d0d0d0d0d0ull));
   gen_mi_store_if(&b, out_mem32(8), gen_mi_imm(0xc000c000));

   submit_batch();

   EXPECT_EQ(*(uint64_t *)(output + 0), u64);
   EXPECT_EQ(*(uint32_t *)(output + 8), u32);
   EXPECT_EQ(*(uint32_t *)(output + 12), (uint32_t)canary);
}

#endif /* GEN_GEN >= 8 || GEN_IS_HASWELL */
