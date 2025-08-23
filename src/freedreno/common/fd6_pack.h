/*
 * Copyright © 2019 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef FD6_PACK_H
#define FD6_PACK_H

#include <stdint.h>

#include "fd6_hw.h"

struct fd_reg_pair {
   uint32_t reg;
   uint64_t value;
   struct fd_bo *bo;
   bool is_address;
   bool bo_write;
   uint32_t bo_offset;
   uint32_t bo_shift;
   uint32_t bo_low;
};

#define __bo_type struct fd_bo *

#include "a6xx-pack.xml.h"
#include "adreno-pm4-pack.xml.h"

#ifdef NDEBUG
#  define __assert_eq(a, b) do { (void)(a); (void)(b); } while (0)
#else
#  define __assert_eq(a, b)                                                    \
   do {                                                                        \
      if ((a) != (b)) {                                                        \
         fprintf(stderr, "assert failed: " #a " (0x%x) != " #b " (0x%x)\n", a, \
                 b);                                                           \
         assert((a) == (b));                                                   \
      }                                                                        \
   } while (0)
#endif

#if !FD_BO_NO_HARDPIN
#  error 'Hardpin unsupported'
#endif

static inline uint64_t
__reg_iova(const struct fd_reg_pair *reg)
{
   uint64_t iova = __reloc_iova((struct fd_bo *)reg->bo,
                                reg->bo_offset, 0,
                                -reg->bo_shift);
   return iova << reg->bo_low;
}

/* Special helper for building UBO descriptors inline with pkt7 */
#define A6XX_UBO_DESC(_i, _bo, _bo_offset, _size_vec4s) {       \
      .reg = 3 + (2 * _i),                                      \
      .value = (uint64_t)A6XX_UBO_1_SIZE(_size_vec4s) << 32,    \
      .bo = _bo, .bo_offset = _bo_offset,                       \
   }, {}

/**
 * Helper for various builders that use fd_ringbuffer.  Not for direct use.
 */
class fd_ringbuffer_builder {
protected:
   fd_ringbuffer_builder(struct fd_ringbuffer *ring) {
      ring_ = ring;
   }

   uint64_t reg_iova(struct fd_reg_pair reg_lo) {
      if (reg_lo.bo) {
         fd_ringbuffer_assert_attached(ring_, reg_lo.bo);
         return __reg_iova(&reg_lo) | reg_lo.value;
      } else {
         return reg_lo.value;
      }
   }

   struct fd_ringbuffer *ring_;

public:
   void attach_bo(struct fd_bo *bo) {
      fd_ringbuffer_attach_bo(ring_, bo);
   }

   friend class fd_cs;
   friend class fd_pkt;
   friend class fd_pkt4;
   friend class fd_pkt7;
   template <chip CHIP, typename Enable> friend class fd_ncrb;
};

class fd_pkt;

/**
 * A general command stream builder, which can mix CRB's for register writes
 * (via fd_crb) and other pkt7 packets.
 */
class fd_cs : public fd_ringbuffer_builder {
public:
   fd_cs(struct fd_ringbuffer *ring) : fd_ringbuffer_builder(ring) {
   }

   /* Constructor for streaming state tied to the submit: */
   fd_cs(struct fd_submit *submit, unsigned ndwords) :
      fd_ringbuffer_builder(fd_submit_new_ringbuffer(submit, ndwords, FD_RINGBUFFER_STREAMING))
   {}

   /* Constructor for long lived state objects: */
   fd_cs(struct fd_pipe *pipe, unsigned ndwords) :
      fd_ringbuffer_builder(fd_ringbuffer_new_object(pipe, ndwords))
   {}

   /* If this assert fails, the currently built packet has not gone out of
    * scope and hasn't been finalized.  This is not allowed when passing
    * thing underling ring buffer back to legacy cmdstream builders, or
    * when starting a new pm4 packet.
    *
    * In cases where you need to delineate scope, the with_xyz() macros
    * can be used, for example:
    *
    *   with_crb (cs, 7) {
    *      set_window_offset<CHIP>(crb, x1, y1);
    *
    *      set_bin_size<CHIP>(crb, gmem, {
    *            .render_mode = RENDERING_PASS,
    *            .force_lrz_write_dis = !screen->info->props.has_lrz_feedback,
    *            .buffers_location = BUFFERS_IN_GMEM,
    *            .lrz_feedback_zmode_mask = screen->info->props.has_lrz_feedback
    *                                          ? LRZ_FEEDBACK_EARLY_Z_LATE_Z
    *                                          : LRZ_FEEDBACK_NONE,
    *      });
    *   }
    *
    *   fd_pkt7(cs, CP_SET_MODE, 1)
    *      .add(0x0);
    *
    */
   void check_flush(void) {
      assert(!pkt_);
   }

   /* bridge back to the legacy world: */
   operator struct fd_ringbuffer *() {
      check_flush();
      return ring_;
   }

   friend class fd_crb;
   friend class fd_pkt;

private:
   /* Disallow copy constructor to prevent mistakes with using fd_cs instead
    * of fd_cs& as function param:
    */
   fd_cs(const fd_cs &);

   /* Non-NULL if currently building a pkt, which needs flushing: */
   fd_pkt *pkt_ = NULL;
};

/**
 * A builder for pkt4 packets.
 *
 * It would be nice to re-use fd_pkt base class for this, but the extra
 * conditionals in flush() make the generated code worse.  So this is
 * more limited, and only intended to be used like:
 *
 *    fd_pkt4(cs, 3)
 *       .add(REG1)
 *       .add(REG2)
 *       .add(REG3);
 *
 * Where possible (ie. 3d context regs) prefer fd_crb instead.
 */
class fd_pkt4 : public fd_ringbuffer_builder {
public:
   fd_pkt4(fd_cs &cs, unsigned nregs) : fd_ringbuffer_builder(cs) {
      init(nregs);
   }

   ~fd_pkt4() {
      flush();
   }

   /* Append a <reg32> to PKT4: */
   fd_pkt4& add(struct fd_reg_pair reg) {
      if (reg_)
         __assert_eq(reg.reg, reg_ + 1);
      reg_ = reg.reg;
      append(reg.value);
      return *this;
   }

   /* Append a <reg64> to PKT4: */
   fd_pkt4& add(struct fd_reg_pair reg_lo, struct fd_reg_pair reg_hi) {
      __assert_eq(reg_hi.reg, 0);
      if (reg_)
         __assert_eq(reg_lo.reg, reg_ + 1);
      reg_ = reg_lo.reg + 1;
      uint64_t val = reg_iova(reg_lo);
      append(val);
      append(val >> 32);
      return *this;
   }

protected:
   fd_pkt4(struct fd_ringbuffer *ring) : fd_ringbuffer_builder(ring) {}

   void reinit(void) {
      struct fd_ringbuffer *ring = ring_;
      cur_ = ring->cur + 1;
      start_ = ring->cur;
      reg_ = 0;
   }

   void init(unsigned nregs) {
      struct fd_ringbuffer *ring = ring_;
      BEGIN_RING(ring, 1 + nregs);
      reinit();
      ndwords_ = nregs;
   }

   void flush() {
      uint32_t *cur = cur_;

      /* Catch any use-after-flush: */
      cur_ = NULL;

      unsigned cnt = cur - ring_->cur - 1;

      assert(cnt > 0);

      /* Check for under-estimate of dwords emitted: */
      assert(cnt <= ndwords_);
      (void)ndwords_;

      /* Check for any other direct use of ringbuffer while building the CRB: */
      assert(ring_->cur == start_);
      (void)start_;

      *ring_->cur = pm4_pkt4_hdr(reg_ - cnt + 1, cnt);
      ring_->cur = cur;
   }

   void append(uint32_t dword) {
      *(cur_++) = dword;
   }

   uint32_t reg_ = 0;

private:
   uint32_t *cur_ = NULL, *start_ = NULL;
   uint32_t ndwords_ = 0;
};

/**
 * Helper base class for any pkt building.
 */
class fd_pkt : public fd_ringbuffer_builder {
protected:
   /* Initializer to use with fd_cs: */
   void init(fd_cs &cs, enum adreno_pm4_type3_packets pkt, unsigned ndwords) {
      cs.check_flush();
      BEGIN_RING(cs.ring_, ndwords + 1);
      init(pkt, ndwords);
      assert(!cs.pkt_);
      cs_ = &cs;
#if !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wdangling-pointer="
#endif
      /* Safe because destructor calls flush() which clears the potentially
       * dangling pointer:
       */
      cs.pkt_ = this;
#if !defined(__clang__)
#pragma GCC diagnostic pop
#endif
   }

   ~fd_pkt() {
      flush();
   }

   /* Initializer to use directly with ring (for fd_crb stateobjs): */
   void init(enum adreno_pm4_type3_packets pkt, unsigned ndwords) {
      cur_ = ring_->cur + 1;
      start_ = ring_->cur;
      pkt_ = pkt;
      ndwords_ = ndwords;
   }

   void append(uint32_t dword) {
      *(cur_++) = dword;
   }

   void append(const uint32_t *dwords, uint32_t sizedwords) {
      memcpy(cur_, dwords, 4 * sizedwords);
      cur_ += sizedwords;
   }

   fd_pkt(struct fd_ringbuffer *ring) : fd_ringbuffer_builder(ring) {};

   enum adreno_pm4_type3_packets pkt_;

public:
   /* Bridge to the legacy world, ideally we can remove this eventually: */
   operator struct fd_ringbuffer *() {
      return ring_;
   }

   void flush() {
      bool allow_empty = (pkt_ != CP_CONTEXT_REG_BUNCH);
      uint32_t *cur = cur_;

      assert(cur);

      if (cs_) {
         assert(cs_->pkt_ == this);
         cs_->pkt_ = NULL;
      }

      /* Catch any use-after-flush: */
      cur_ = NULL;

      unsigned cnt = cur - ring_->cur - 1;

      /* Check for under-estimate of dwords emitted: */
      assert(cnt <= ndwords_);
      (void)ndwords_;

      /* Check for any other direct use of ringbuffer while building the CRB: */
      assert(ring_->cur == start_);
      (void)start_;

      if (!allow_empty && !cnt)
         return;

      *ring_->cur = pm4_pkt7_hdr(pkt_, cnt);
      ring_->cur = cur;
   }

private:
   uint32_t *cur_ = NULL, *start_ = NULL;
   uint32_t ndwords_ = 0;
   fd_cs *cs_ = NULL;
};

/**
 * A builder for CP_CONTEXT_REG_BUNCH.  This packet can write an
 * arbitrary sequence of registers (payload consists of pairs of
 * offset,value).  It should be as fast as a pkt4 packet writing
 * a consecutive sequence of registers, without the constraint of
 * the registers being sequential, making it easier to use when
 * cmdstream emit involves if/else/loops.  And should be less
 * brittle if registers shift around between generations.  This
 * builder intentionally encourages use of fd_reg_pair.
 */
class fd_crb : public fd_pkt {
public:
   /* Constructor for streaming state tied to the submit: */
   fd_crb(struct fd_submit *submit, unsigned nregs) :
      fd_pkt(fd_submit_new_ringbuffer(submit, cs_size(nregs), FD_RINGBUFFER_STREAMING))
   {
      init(CP_CONTEXT_REG_BUNCH, nregs * 2);
   }

   /* Constructor for long lived state objects: */
   fd_crb(struct fd_pipe *pipe, unsigned nregs) :
      fd_pkt(fd_ringbuffer_new_object(pipe, cs_size(nregs)))
   {
      init(CP_CONTEXT_REG_BUNCH, nregs * 2);
   }

   /* Constructor to use with fd_cs: */
   fd_crb(fd_cs &cs, unsigned nregs) : fd_pkt(cs.ring_) {
      init(cs, CP_CONTEXT_REG_BUNCH, nregs * 2);
   }

   /* Append a <reg32> to CRB: */
   fd_crb& add(struct fd_reg_pair reg) {
      append(reg.reg);
      append(reg.value);
      return *this;
   }

   /* Append a <reg64> to CRB: */
   fd_crb& add(struct fd_reg_pair reg_lo, struct fd_reg_pair reg_hi) {
      __assert_eq(reg_hi.reg, 0);
      uint64_t val = reg_iova(reg_lo);
      append(reg_lo.reg);
      append(val);
      append(reg_lo.reg + 1);
      append(val >> 32);
      return *this;
   }

   bool first = true;

protected:
   fd_crb(struct fd_ringbuffer *ring) : fd_pkt(ring) {}

private:
   /* Disallow copy constructor to prevent mistakes with using fd_crb instead
    * of fd_crb& as function param:
    */
   fd_crb(const fd_crb &);

   static unsigned cs_size(unsigned nregs) {
      /* 1 dword hdr plus 2 dword per reg (offset, value pairs) */
      return 4 * (1 + (nregs * 2));
   }
};

#define with_crb(...) \
   for (fd_crb crb(__VA_ARGS__); crb.first; crb.first = false)

/**
 * A builder for writing non-context regs, which is implemented differently
 * depending on generation (A6XX doesn't have CP_NON_CONTEXT_REG_BUNCH)
 */
template <chip_range_support>
class fd_ncrb {
public:
   fd_ncrb(fd_cs &cs, unsigned nregs);

   /* Append a <reg32> to NCRB: */
   fd_ncrb& add(struct fd_reg_pair reg);

   /* Append a <reg64> to NCRB: */
   fd_ncrb& add(struct fd_reg_pair reg_lo, struct fd_reg_pair reg_hi);
};

#define with_ncrb(cs, nregs) \
   for (fd_ncrb<CHIP> ncrb(cs, nregs); ncrb.first; ncrb.first = false)

/**
 * A6XX does not have CP_NON_CONTEXT_REG_BUNCH, so the builder is implemented
 * as a sequence of pkt4's
 */
template <chip CHIP>
class fd_ncrb<chip_range(CHIP == A6XX)> : fd_pkt4 {
public:
   fd_ncrb(fd_cs &cs, unsigned nregs) : fd_pkt4(cs.ring_) {
      /* worst case, one pkt4 per reg: */
      init(nregs * 2);
   }

   /* Append a <reg32>: */
   fd_ncrb& add(struct fd_reg_pair reg) {
      check_restart(reg);
      fd_pkt4::add(reg);
      return *this;
   }

   /* Append a <reg64>: */
   fd_ncrb& add(struct fd_reg_pair reg_lo, struct fd_reg_pair reg_hi) {
      check_restart(reg_lo);
      fd_pkt4::add(reg_lo, reg_hi);
      return *this;
   }

   bool first = true;

private:
   void check_restart(struct fd_reg_pair reg) {
      if (reg_ && (reg.reg != reg_ + 1)) {
         /* Start a new pkt4: */
         flush();
         reinit();
      }
   }
};

/**
 * Builder to write non-context regs for A7XX+, which uses CP_NON_CONTEXT_REG_BUNCH
 */
template <chip CHIP>
class fd_ncrb<chip_range(CHIP >= A7XX)> : fd_crb {
public:
   fd_ncrb(fd_cs &cs, unsigned nregs) : fd_crb(cs.ring_) {
      init(cs, CP_NON_CONTEXT_REG_BUNCH, 2 + (nregs * 2));
      append(1);
      append(0);
   }

   /* Append a <reg32>: */
   fd_ncrb& add(struct fd_reg_pair reg) {
      fd_crb::add(reg);
      return *this;
   }

   /* Append a <reg64>: */
   fd_ncrb& add(struct fd_reg_pair reg_lo, struct fd_reg_pair reg_hi) {
      fd_crb::add(reg_lo, reg_hi);
      return *this;
   }

   bool first = true;
};

/**
 * A builder for an arbitrary PKT7 (for CRB, use fd_crb instead)
 */
class fd_pkt7 final : public fd_pkt {
public:
   fd_pkt7(fd_cs &cs, enum adreno_pm4_type3_packets pkt, unsigned ndwords) :
      fd_pkt(cs.ring_)
   {
      init(cs, pkt, ndwords);
   }

   /* Allow appending a "naked" dwords: */
   fd_pkt7& add(uint32_t val) {
      append(val);
      off_++;
      return *this;
   }

   /* Append a <reg32> to CRB: */
   fd_pkt7& add(struct fd_reg_pair reg) {
      __assert_eq(off_, reg.reg);
      off_ = reg.reg + 1;
      append(reg.value);
      return *this;
   }

   /* Append a <reg64> to CRB: */
   fd_pkt7& add(struct fd_reg_pair reg_lo, struct fd_reg_pair reg_hi) {
      __assert_eq(reg_hi.reg, 0);
      __assert_eq(off_, reg_lo.reg);
      off_ = reg_lo.reg + 2;
      uint64_t val = reg_iova(reg_lo);
      append(val);
      append(val >> 32);
      return *this;
   }

   fd_pkt7& add(const uint32_t *dwords, uint32_t sizedwords) {
      append(dwords, sizedwords);
      return *this;
   }

   fd_pkt7& add(struct fd_ringbuffer *target, uint32_t cmd_idx, uint32_t *dwords) {
      uint64_t iova;
      uint32_t size;

      size = fd_ringbuffer_attach_ring(ring_, target, cmd_idx, &iova);
      append(iova);
      append(iova >> 32);
      off_ += 2;

      if (dwords)
         *dwords = size / 4;

      return *this;
   }

private:
   /* Disallow copy constructor to prevent mistakes with using fd_pkt7 instead
    * of fd_pkt7& as function param:
    */
   fd_pkt7(const fd_pkt7 &);

   /* for debugging: */
   unsigned off_ = 0;
};

#endif /* FD6_PACK_H */
