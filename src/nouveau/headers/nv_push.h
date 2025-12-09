#ifndef NV_PUSH_H
#define NV_PUSH_H

#include "nvtypes.h"
#include "util/macros.h"

#include "drf.h"
#include "cl906f.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

struct nv_device_info;

struct nv_push {
   uint32_t *start;
   uint32_t *end;
   uint32_t *limit;

   /* A pointer to the last method header */
   uint32_t *last_hdr;

   /* The value in the last method header, used to avoid read-back */
   uint32_t last_hdr_dw;

#ifndef NDEBUG
   /* A mask of valid subchannels */
   uint8_t subc_mask;
#endif
};

#define SUBC_MASK_ALL 0xff

static inline void
nv_push_init(struct nv_push *push, uint32_t *start,
             size_t dw_count, uint8_t subc_mask)
{
   push->start = start;
   push->end = start;
   push->limit = start + dw_count;
   push->last_hdr = NULL;
   push->last_hdr_dw = 0;

#ifndef NDEBUG
   push->subc_mask = subc_mask;
#endif
}

static inline size_t
nv_push_dw_count(const struct nv_push *push)
{
   assert(push->start <= push->end);
   assert(push->end <= push->limit);
   return push->end - push->start;
}

#ifndef NDEBUG
void nv_push_validate(struct nv_push *push);
#else
static inline void nv_push_validate(struct nv_push *push) { }
#endif

void vk_push_print(FILE *fp, const struct nv_push *push,
                   const struct nv_device_info *devinfo) ATTRIBUTE_COLD;

#define SUBC_NV9097 0
#define SUBC_NVA097 0
#define SUBC_NVB097 0
#define SUBC_NVB197 0
#define SUBC_NVC097 0
#define SUBC_NVC397 0
#define SUBC_NVC597 0
#define SUBC_NVC797 0
#define SUBC_NVC86F 0
#define SUBC_NVCB97 0
#define SUBC_NVCD97 0

#define SUBC_NV90C0 1
#define SUBC_NVA0C0 1
#define SUBC_NVB0C0 1
#define SUBC_NVB1C0 1
#define SUBC_NVC0C0 1
#define SUBC_NVC3C0 1
#define SUBC_NVC6C0 1
#define SUBC_NVC7C0 1

#define SUBC_NV9039 2

#define SUBC_NV902D 3

#define SUBC_NV90B5 4
#define SUBC_NVC1B5 4

/* video decode will get push on sub channel 4 */
#define SUBC_NVC5B0 4

static inline uint8_t
NVC0_FIFO_SUBC_FROM_PKHDR(uint32_t hdr)
{
   return NVVAL_GET(hdr, NV906F, DMA, METHOD_SUBCHANNEL);
}

static inline uint32_t
NVC0_FIFO_PKHDR_SQ(int subc, int mthd, unsigned size)
{
   return NVDEF(NV906F, DMA_INCR, OPCODE, VALUE) |
          NVVAL(NV906F, DMA_INCR, COUNT, size) |
          NVVAL(NV906F, DMA_INCR, SUBCHANNEL, subc) |
          NVVAL(NV906F, DMA_INCR, ADDRESS, mthd >> 2);
}

static inline void
__push_verify(struct nv_push *push)
{
   if (push->last_hdr == NULL)
      return;

   /* check for immd */
   if (NVDEF_TEST(push->last_hdr_dw, NV906F, DMA, SEC_OP, ==, IMMD_DATA_METHOD))
      return;

   ASSERTED uint32_t last_count =
      NVVAL_GET(push->last_hdr_dw, NV906F, DMA, METHOD_COUNT);
   assert(last_count);
}

static inline void
__push_hdr(struct nv_push *push, uint32_t hdr)
{
   __push_verify(push);

   ASSERTED const uint32_t subc = NVC0_FIFO_SUBC_FROM_PKHDR(hdr);
   assert(push->subc_mask & BITFIELD_BIT(subc));

   *push->end = hdr;
   push->last_hdr_dw = hdr;
   push->last_hdr = push->end;
   push->end++;
}

static inline void
__push_mthd_size(struct nv_push *push, int subc, uint32_t mthd, unsigned size)
{
   __push_hdr(push, NVC0_FIFO_PKHDR_SQ(subc, mthd, size));
}

static inline void
__push_mthd(struct nv_push *push, int subc, uint32_t mthd)
{
   __push_mthd_size(push, subc, mthd, 0);
}

#define P_MTHD(push, class, mthd) __push_mthd(push, SUBC_##class, class##_##mthd)

static inline uint32_t
NVC0_FIFO_PKHDR_IL(int subc, int mthd, uint16_t data)
{
   assert(!(data & ~DRF_MASK(NV906F_DMA_IMMD_DATA)));
   return NVDEF(NV906F, DMA_IMMD, OPCODE, VALUE) |
          NVVAL(NV906F, DMA_IMMD, DATA, data) |
          NVVAL(NV906F, DMA_IMMD, SUBCHANNEL, subc) |
          NVVAL(NV906F, DMA_IMMD, ADDRESS, mthd >> 2);
}

static inline void
__push_immd(struct nv_push *push, int subc, uint32_t mthd, uint32_t val)
{
   __push_hdr(push, NVC0_FIFO_PKHDR_IL(subc, mthd, val));
}

#define P_IMMD(push, class, mthd, args...) do {                         \
   uint32_t __val;                                                      \
   VA_##class##_##mthd(__val, args);                                    \
   if (__builtin_constant_p(__val & ~0x1fff) && !(__val & ~0x1fff)) {   \
      __push_immd(push, SUBC_##class, class##_##mthd, __val);           \
   } else {                                                             \
      __push_mthd_size(push, SUBC_##class, class##_##mthd, 0);          \
      nv_push_val(push, class##_##mthd, __val);                        \
   }                                                                    \
} while(0)

static inline uint32_t
NVC0_FIFO_PKHDR_1I(int subc, int mthd, unsigned size)
{
   return NVDEF(NV906F, DMA_ONEINCR, OPCODE, VALUE) |
          NVVAL(NV906F, DMA_ONEINCR, COUNT, size) |
          NVVAL(NV906F, DMA_ONEINCR, SUBCHANNEL, subc) |
          NVVAL(NV906F, DMA_ONEINCR, ADDRESS, mthd >> 2);
}

static inline void
__push_1inc(struct nv_push *push, int subc, uint32_t mthd)
{
   __push_hdr(push, NVC0_FIFO_PKHDR_1I(subc, mthd, 0));
}

#define P_1INC(push, class, mthd) __push_1inc(push, SUBC_##class, class##_##mthd)

static inline uint32_t
NVC0_FIFO_PKHDR_0I(int subc, int mthd, unsigned size)
{
   return NVDEF(NV906F, DMA_NONINCR, OPCODE, VALUE) |
          NVVAL(NV906F, DMA_NONINCR, COUNT, size) |
          NVVAL(NV906F, DMA_NONINCR, SUBCHANNEL, subc) |
          NVVAL(NV906F, DMA_NONINCR, ADDRESS, mthd >> 2);
}

static inline void
__push_0inc(struct nv_push *push, int subc, uint32_t mthd)
{
   __push_hdr(push, NVC0_FIFO_PKHDR_0I(subc, mthd, 0));
}

#define P_0INC(push, class, mthd) __push_0inc(push, SUBC_##class, class##_##mthd)

#define NV_PUSH_MAX_COUNT DRF_MASK(NV906F_DMA_METHOD_COUNT)

static inline bool
nv_push_update_count(struct nv_push *push, uint16_t count)
{
   assert(push->last_hdr != NULL);

   assert(count <= NV_PUSH_MAX_COUNT);
   if (count > NV_PUSH_MAX_COUNT)
      return false;

   uint32_t hdr_dw = push->last_hdr_dw;

   uint32_t old_count = NVVAL_GET(hdr_dw, NV906F, DMA, METHOD_COUNT);
   uint32_t new_count = (count + old_count) & NV_PUSH_MAX_COUNT;
   bool overflow = new_count < count;
   /* if we would overflow, don't change anything and just let it be */
   assert(!overflow);
   if (overflow)
      return false;

   hdr_dw = NVVAL_SET(hdr_dw, NV906F, DMA, METHOD_COUNT, new_count);
   push->last_hdr_dw = hdr_dw;
   *push->last_hdr = hdr_dw;
   return true;
}

static inline void
P_INLINE_DATA(struct nv_push *push, uint32_t value)
{
   assert(push->end < push->limit);
   if (nv_push_update_count(push, 1)) {
      /* push new value */
      *push->end = value;
      push->end++;
   }
}

static inline void
P_INLINE_FLOAT(struct nv_push *push, float value)
{
   assert(push->end < push->limit);
   if (nv_push_update_count(push, 1)) {
      /* push new value */
      *(float *)push->end = value;
      push->end++;
   }
}

static inline void
P_INLINE_ARRAY(struct nv_push *push, const uint32_t *data, int num_dw)
{
   assert(push->end + num_dw <= push->limit);
   if (nv_push_update_count(push, num_dw)) {
      /* push new value */
      memcpy(push->end, data, num_dw * 4);
      push->end += num_dw;
   }
}

/* internally used by generated inlines. */
static inline void
nv_push_val(struct nv_push *push, uint32_t idx, uint32_t val)
{
   ASSERTED uint32_t last_hdr_dw = push->last_hdr_dw;
   ASSERTED uint32_t type = NVVAL_GET(last_hdr_dw, NV906F, DMA, SEC_OP);
   ASSERTED bool is_0inc = type == NV906F_DMA_SEC_OP_NON_INC_METHOD;
   ASSERTED bool is_1inc = type == NV906F_DMA_SEC_OP_ONE_INC;
   ASSERTED bool is_immd = type == NV906F_DMA_SEC_OP_IMMD_DATA_METHOD;
   ASSERTED uint16_t last_method =
      NVVAL_GET(last_hdr_dw, NV906F, DMA, METHOD_ADDRESS) << 2;

   uint16_t distance = push->end - push->last_hdr - 1;
   if (is_0inc)
      distance = 0;
   else if (is_1inc)
      distance = MIN2(1, distance);
   last_method += distance * 4;

   /* can't have empty headers ever */
   assert(last_hdr_dw);
   assert(!is_immd);
   assert(last_method == idx);
   assert(push->end < push->limit);

   P_INLINE_DATA(push, val);
}

static inline void
nv_push_raw(struct nv_push *push, uint32_t *raw_dw, uint32_t dw_count)
{
   assert(push->end + dw_count <= push->limit);
   memcpy(push->end, raw_dw, dw_count * 4);
   push->end += dw_count;
   push->last_hdr = NULL;
}

#endif /* NV_PUSH_H */
