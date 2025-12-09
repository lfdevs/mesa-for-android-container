/*
 * Copyright © 2010-2020 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "brw_cfg.h"
#include "brw_inst.h"
#include "util/bitset.h"

struct brw_shader;

/**
 * Bitset of state categories that can influence the result of IR analysis
 * passes.
 */
enum brw_analysis_dependency_class {
   /**
    * The analysis doesn't depend on the IR, its result is effectively a
    * constant during the compilation.
    */
   BRW_DEPENDENCY_NOTHING = 0,
   /**
    * The analysis depends on the set of instructions in the program and
    * their naming.  Note that because instructions are named sequentially
    * by IP this implies a dependency on the control flow edges between
    * instructions.  This will be signaled whenever instructions are
    * inserted, removed or reordered in the program.
    */
   BRW_DEPENDENCY_INSTRUCTION_IDENTITY = 0x1,
   /**
    * The analysis is sensitive to the detailed semantics of instructions
    * in the program, where "detailed" means any change in the instruction
    * data structures other than the linked-list pointers (which are
    * already covered by DEPENDENCY_INSTRUCTION_IDENTITY).  E.g. changing
    * the negate or abs flags of an instruction source would signal this
    * flag alone because it would preserve all other instruction dependency
    * classes.
    */
   BRW_DEPENDENCY_INSTRUCTION_DETAIL = 0x2,
   /**
    * The analysis depends on the set of data flow edges between
    * instructions.  This will be signaled whenever the dataflow relation
    * between instructions has potentially changed, e.g. when the VGRF
    * index of an instruction source or destination changes (in which case
    * it will appear in combination with DEPENDENCY_INSTRUCTION_DETAIL), or
    * when data-dependent instructions are reordered (in which case it will
    * appear in combination with DEPENDENCY_INSTRUCTION_IDENTITY).
    */
   BRW_DEPENDENCY_INSTRUCTION_DATA_FLOW = 0x4,
   /**
    * The analysis depends on all instruction dependency classes.  These
    * will typically be signaled simultaneously when inserting or removing
    * instructions in the program (or if you're feeling too lazy to read
    * through your optimization pass to figure out which of the instruction
    * dependency classes above it invalidates).
    */
   BRW_DEPENDENCY_INSTRUCTIONS = 0x7,
   /**
    * The analysis depends on the set of VGRFs in the program and their
    * naming.  This will be signaled when VGRFs are allocated or released.
    */
   BRW_DEPENDENCY_VARIABLES = 0x8,
   /**
    * The analysis depends on the set of basic blocks in the program, their
    * control flow edges and naming.
    */
   BRW_DEPENDENCY_BLOCKS = 0x10,
   /**
    * The analysis depends on the program being literally the same (good
    * luck...), any change in the input invalidates previous analysis
    * computations.
    */
   BRW_DEPENDENCY_EVERYTHING = ~0
};

inline brw_analysis_dependency_class
operator|(brw_analysis_dependency_class x, brw_analysis_dependency_class y)
{
   return static_cast<brw_analysis_dependency_class>(
      static_cast<unsigned>(x) | static_cast<unsigned>(y));
}

inline brw_analysis_dependency_class
operator|=(brw_analysis_dependency_class &x, brw_analysis_dependency_class y)
{
   return x = x | y;
}

/**
 * Instantiate a program analysis class \p L which can calculate an object of
 * type \p T as result.  \p C is a closure that encapsulates whatever
 * information is required as argument to run the analysis pass.  The purpose
 * of this class is to make sure that:
 *
 *  - The analysis pass is executed lazily whenever it's needed and multiple
 *    executions are optimized out as long as the cached result remains marked
 *    up-to-date.
 *
 *  - There is no way to access the cached analysis result without first
 *    calling L::require(), which makes sure that the analysis pass is rerun
 *    if necessary.
 *
 *  - The cached result doesn't become inconsistent with the program for as
 *    long as it remains marked up-to-date. (This is only enforced in debug
 *    builds for performance reasons)
 *
 * The requirements on \p T are the following:
 *
 *  - Constructible with a single argument, as in 'x = T(c)' for \p c of type
 *    \p C.
 *
 *  - 'x.dependency_class()' on const \p x returns a bitset of
 *    brw::analysis_dependency_class specifying the set of IR objects that are
 *    required to remain invariant for the cached analysis result to be
 *    considered valid.
 *
 *  - 'x.validate(c)' on const \p x returns a boolean result specifying
 *    whether the analysis result \p x is consistent with the input IR.  This
 *    is currently only used for validation in debug builds.
 */
template<class T, class C>
class brw_analysis {
public:
   /**
    * Construct a program analysis.  \p c is an arbitrary object
    * passed as argument to the constructor of the analysis result
    * object of type \p T.
    */
   brw_analysis(const C *c) : c(c), p(NULL) {}

   /**
    * Destroy a program analysis.
    */
   ~brw_analysis()
   {
      delete p;
   }

   /**
    * Obtain the result of a program analysis.  This gives a
    * guaranteed up-to-date result, the analysis pass will be
    * rerun implicitly if it has become stale.
    */
   T &
   require()
   {
      if (p)
         assert(p->validate(c));
      else
         p = new T(c);

      return *p;
   }

   const T &
   require() const
   {
      return const_cast<brw_analysis<T, C> *>(this)->require();
   }

   /**
    * Report that dependencies of the analysis pass may have changed
    * since the last calculation and the cached analysis result may
    * have to be discarded.
    */
   void
   invalidate(brw_analysis_dependency_class c)
   {
      if (p && (c & p->dependency_class())) {
         delete p;
         p = NULL;
      }
   }

private:
   const C *c;
   T *p;
};

/**
 * Immediate dominator tree analysis of a shader.
 */
struct brw_idom_tree {
   brw_idom_tree(const brw_shader *s);
   ~brw_idom_tree();

   bool
   validate(const brw_shader *) const
   {
      /* FINISHME */
      return true;
   }

   brw_analysis_dependency_class
   dependency_class() const
   {
      return BRW_DEPENDENCY_BLOCKS;
   }

   const bblock_t *
   parent(const bblock_t *b) const
   {
      assert(unsigned(b->num) < num_parents);
      return parents[b->num];
   }

   bblock_t *
   parent(bblock_t *b) const
   {
      assert(unsigned(b->num) < num_parents);
      return parents[b->num];
   }

   bblock_t *
   intersect(bblock_t *b1, bblock_t *b2) const;

   /**
    * Returns true if block `a` dominates block `b`.
    */
   bool
   dominates(const bblock_t *a, const bblock_t *b) const
   {
      while (a != b) {
         if (b->num == 0)
            return false;

         b = parent(b);
      }
      return true;
   }

   void dump(FILE *file = stderr) const;

private:
   unsigned num_parents;
   bblock_t **parents;
};

struct brw_range {
   int start;
   int end;

   /* If range not empty, this is the last value inside the range. */
   inline int last() const
   {
      return end - 1;
   }

   inline bool is_empty() const
   {
      return end <= start;
   }

   inline int len() const
   {
      return end - start;
   }

   inline bool contains(int x) const
   {
      return start <= x && x < end;
   }

   inline bool contains(brw_range r) const
   {
      return start <= r.start && r.end <= end;
   }
};

inline brw_range
merge(brw_range a, brw_range b)
{
   if (a.is_empty())
      return b;
   if (b.is_empty())
      return a;
   return { MIN2(a.start, b.start), MAX2(a.end, b.end) };
}

inline brw_range
merge(brw_range r, int x)
{
   if (r.is_empty())
      return { x, x + 1 };
   return { MIN2(r.start, x), MAX2(r.end, x + 1) };
}

inline bool
overlaps(brw_range a, brw_range b)
{
   return a.start < b.end &&
          b.start < a.end;
}

inline brw_range
intersect(brw_range a, brw_range b)
{
   if (overlaps(a, b))
      return { MAX2(a.start, b.start),
               MIN2(a.end, b.end) };
   else
      return { 0, 0 };
}

inline brw_range
clip_end(brw_range r, int n)
{
   assert(n >= 0);
   return { r.start, r.end - n };
}

struct brw_ip_ranges {
   brw_ip_ranges(const brw_shader *s);
   ~brw_ip_ranges();

   bool validate(const brw_shader *) const;

   brw_analysis_dependency_class
   dependency_class() const
   {
      return BRW_DEPENDENCY_INSTRUCTION_IDENTITY |
             BRW_DEPENDENCY_BLOCKS;
   }

   brw_range range(const bblock_t *block) const {
      int start = start_ip[block->num];
      return { start, start + (int)block->num_instructions };
   }

private:
   int num_blocks;
   int *start_ip;
};

/**
 * Register pressure analysis of a shader.  Estimates how many registers
 * are live at any point of the program in GRF units.
 */
struct brw_register_pressure {
   brw_register_pressure(const brw_shader *v);
   ~brw_register_pressure();

   brw_analysis_dependency_class
   dependency_class() const
   {
      return (BRW_DEPENDENCY_INSTRUCTION_IDENTITY |
              BRW_DEPENDENCY_INSTRUCTION_DATA_FLOW |
              BRW_DEPENDENCY_VARIABLES);
   }

   bool
   validate(const brw_shader *) const
   {
      /* FINISHME */
      return true;
   }

   unsigned *regs_live_at_ip;
};

class brw_def_analysis {
public:
   brw_def_analysis(const brw_shader *v);
   ~brw_def_analysis();

   brw_inst *
   get(const brw_reg &reg) const
   {
      return reg.file == VGRF && reg.nr < def_count ?
             def_insts[reg.nr] : NULL;
   }

   uint32_t
   get_use_count(const brw_reg &reg) const
   {
      return reg.file == VGRF && reg.nr < def_count ?
             def_use_counts[reg.nr] : 0;
   }

   unsigned count() const { return def_count; }
   unsigned ssa_count() const;

   void print_stats(const brw_shader *) const;

   brw_analysis_dependency_class
   dependency_class() const
   {
      return BRW_DEPENDENCY_INSTRUCTION_IDENTITY |
             BRW_DEPENDENCY_INSTRUCTION_DATA_FLOW |
             BRW_DEPENDENCY_VARIABLES |
             BRW_DEPENDENCY_BLOCKS;
   }

   bool validate(const brw_shader *) const;

private:
   void mark_invalid(int);
   bool fully_defines(const brw_shader *v, brw_inst *);
   void update_for_reads(const brw_idom_tree &idom, brw_inst *);
   void update_for_write(const brw_shader *v, brw_inst *);

   brw_inst **def_insts;
   uint32_t *def_use_counts;
   unsigned def_count;
};

class brw_live_variables {
public:
   struct block_data {
      /**
       * Which variables are defined before being used in the block.
       *
       * Note that for our purposes, "defined" means unconditionally, completely
       * defined.
       */
      BITSET_WORD *def;

      /**
       * Which variables are used before being defined in the block.
       */
      BITSET_WORD *use;

      /** Which defs reach the entry point of the block. */
      BITSET_WORD *livein;

      /** Which defs reach the exit point of the block. */
      BITSET_WORD *liveout;

      /**
       * Variables such that the entry point of the block may be reached from any
       * of their definitions.
       */
      BITSET_WORD *defin;

      /**
       * Variables such that the exit point of the block may be reached from any
       * of their definitions.
       */
      BITSET_WORD *defout;

      BITSET_WORD flag_def[1];
      BITSET_WORD flag_use[1];
      BITSET_WORD flag_livein[1];
      BITSET_WORD flag_liveout[1];

      brw_range ip_range;
   };

   brw_live_variables(const brw_shader *s);
   ~brw_live_variables();

   bool validate(const brw_shader *s) const;

   brw_analysis_dependency_class
   dependency_class() const
   {
      return (BRW_DEPENDENCY_INSTRUCTION_IDENTITY |
              BRW_DEPENDENCY_INSTRUCTION_DATA_FLOW |
              BRW_DEPENDENCY_VARIABLES);
   }

   bool vars_interfere(int a, int b) const;
   bool vgrfs_interfere(int a, int b) const;
   int var_from_reg(const brw_reg &reg) const
   {
      return var_from_vgrf[reg.nr] + reg.offset / REG_SIZE;
   }

   /** Map from virtual GRF number to index in block_data arrays. */
   int *var_from_vgrf;

   /**
    * Map from any index in block_data to the virtual GRF containing it.
    *
    * For alloc.sizes of [1, 2, 3], vgrf_from_var would contain
    * [0, 1, 1, 2, 2, 2].
    */
   int *vgrf_from_var;

   int num_vars;
   int num_vgrfs;
   int bitset_words;

   unsigned max_vgrf_size;

   /** @{
    * Final computed live ranges for each var (each component of each virtual
    * GRF).
    */
   brw_range *vars_range;
   /** @} */

   /** @{
    * Final computed live ranges for each VGRF.
    */
   brw_range *vgrf_range;
   /** @} */

   /** Per-basic-block information on live variables */
   struct block_data *block_data;

protected:
   void setup_def_use();
   void setup_one_read(struct block_data *bd, int ip, const brw_reg &reg);
   void setup_one_write(struct block_data *bd, brw_inst *inst, int ip,
                        const brw_reg &reg);
   void compute_live_variables();
   void compute_start_end();

   const struct intel_device_info *devinfo;
   const cfg_t *cfg;
   void *mem_ctx;
};

/**
 * Various estimates of the performance of a shader based on static
 * analysis.
 */
struct brw_performance {
   brw_performance(const brw_shader *v);
   ~brw_performance();

   brw_analysis_dependency_class
   dependency_class() const
   {
      return (BRW_DEPENDENCY_INSTRUCTIONS |
              BRW_DEPENDENCY_BLOCKS);
   }

   bool
   validate(const brw_shader *) const
   {
      return true;
   }

   /**
    * Array containing estimates of the runtime of each basic block of the
    * program in cycle units.
    */
   unsigned *block_latency;

   /**
    * Estimate of the runtime of the whole program in cycle units assuming
    * uncontended execution.
    */
   unsigned latency;

   /**
    * Estimate of the throughput of the whole program in
    * invocations-per-cycle-per-EU units.
    *
    * This gives the expected throughput of a whole EU under the
    * heuristic assumption that it is fully loaded instead of the
    * throughput of a single thread, this is in order to be able to
    * account for the reduction in parallelism that xe3+ EUs
    * experience with increasing register use.  Earlier platforms use
    * a fixed factor as EU thread count instead.
    *
    * Note that this number might be lower than expected from the
    * reciprocal of the latency estimate in cases where performance
    * doesn't scale without limits as a function of its thread
    * parallelism, e.g. due to the existence of a bottleneck in a
    * shared function.
    */
   float throughput;

private:
   brw_performance(const brw_performance &perf);
   brw_performance &
   operator=(brw_performance u);
};
