/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <assert.h>
#include <stdint.h>

#include "dev/intel_device_info.h"
#include "util/macros.h"

#include "gen_enums.h"
#include "gen_types.h"
#include "gen_helpers.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Finish structured control flow instructions by filling in any missing
 * JIPs and UIPs.
 *
 * Any JIPs or UIPs already set by the caller must be relative byte
 * offsets, and will be respected as-is.
 *
 * The caller must ensure that the JIP for WHILE instructions is set.
 * It represents the "back-edge" and can't be inferred since there's no
 * DO instruction marking the start of a loop.
 *
 * Any other JIPs and UIPs will be inferred from the structure of the
 * program and filled in as relative byte offsets.
 *
 * If a final_halt_idx is provided, that will act as a final synchronization
 * point for the halts and JIPs filled in the instructions.
 */
bool gen_finish_structured_cf(gen_inst *insts, int num_insts, int final_halt_idx);


typedef struct gen_encode_params {
   const struct intel_device_info *devinfo;

   /* Use compacted encoded form for all instructions that support it. */
   bool compact_all;

   bool skip_validation;

   const gen_inst *insts;
   int             num_insts;

   /* Must be non-NULL, used for allocating `errors`. */
   void *mem_ctx;

   /* Must be non-NULL.  Represents a pre-allocated buffer of size
    * raw_bytes_size that will be filled in.
    *
    * The size will be updated to the actual size used.
    */
   void *raw_bytes;
   int   raw_bytes_size;

   /* Optional per-instruction encoded byte offsets.  When non-NULL, it must
    * have at least num_insts entries and will be filled with the start offset
    * of each instruction in the final encoded output.
    */
   int *encoded_offsets;

   gen_error *errors;
   int        num_errors;
} gen_encode_params;

bool gen_encode(gen_encode_params *params);


typedef struct gen_decode_params {
   const struct intel_device_info *devinfo;

   const void *raw_bytes;
   int         raw_bytes_size;

   /* Set true if the decode is only a subset of the program. If a subset is
    * decoded, then it is not possible to adjust jip/uip references properly.
    */
   bool        program_subset;

   /* Must be non-NULL, used for allocating the arrays below. */
   void *mem_ctx;

   gen_inst *insts;
   int       num_insts;

   gen_error *errors;
   int        num_errors;
} gen_decode_params;

bool gen_decode(gen_decode_params *params);


typedef struct gen_inst_layout {
   int offset;
   bool was_compacted;
} gen_inst_layout;

typedef struct gen_scan_raw_layout_params {
   const void *raw_bytes;
   int         raw_bytes_size;

   /* Optional per-instruction output array.  When non-NULL, must have at
    * least num_insts entries.  If the right num_insts is unknown, using an
    * array with size `raw_bytes_size/8` is always sufficient.
    *
    * When the call succeeds, `num_insts` will be updated with the actual
    * number of instructions.
    */
   gen_inst_layout *layouts;
   int              num_insts;

   /* If successful, will contain the byte offset from raw_bytes after the
    * last complete instruction.
    */
   int end_offset;
} gen_scan_raw_layout_params;

bool gen_scan_raw_layout(gen_scan_raw_layout_params *params);


typedef struct gen_validate_params {
   const struct intel_device_info *devinfo;

   const gen_inst *insts;
   int             num_insts;

   /* Must be non-NULL, used for allocating `errors`. */
   void *mem_ctx;

   gen_error *errors;
   int        num_errors;
} gen_validate_params;

bool gen_validate(gen_validate_params *params);


typedef enum gen_print_flags {
   GEN_PRINT_NONE = 0,

   /* Don't omit regions, types and other values that can be inferred. */
   GEN_PRINT_VERBOSE = 1 << 0,

   /* Print translated operations like LOAD and STORE instead of raw SENDs,
    * as the opcode.
    */
   GEN_PRINT_TRANSLATED_SENDS = 1 << 1,

   /* Prefix each instruction with address_base + encoded byte offset.
    * Requires raw_bytes so the instruction layout can be scanned.
    */
   GEN_PRINT_BYTE_OFFSETS = 1 << 2,

   /* Prefix each instruction with a hex dump of its encoded bytes.
    * Requires raw_bytes so the instruction layout can be scanned.
    */
   GEN_PRINT_HEX = 1 << 3,

   /* Suppress folding of environment-driven flags (e.g. INTEL_DEBUG=hex
    * implies GEN_PRINT_HEX) into the effective flags.
    */
   GEN_PRINT_IGNORE_ENV = 1 << 4,

   /* Don't synthesize labels for branch targets; print every jip/uip as an
    * explicit numeric delta.
    */
   GEN_PRINT_NO_LABELS = 1 << 5,
} gen_print_flags;

typedef struct gen_print_params {
   const struct intel_device_info *devinfo;

   /* When NULL, uses stderr. */
   FILE *fp;

   gen_print_flags flags;

   /* Optional decoded instructions to print.  When NULL, gen_print() decodes
    * raw_bytes internally and prints the decoded program.
    */
   gen_inst *insts;
   int       num_insts;

   /* Optional errors to print inline.  When NULL and validate is true,
    * gen_print() validates the instruction stream it prints and emits the
    * resulting errors inline.
    */
   const gen_error *errors;
   int              num_errors;

   /* Optional per-instruction information.  When non-NULL, the array
    * must have `num_insts` elements.
    */
   const char *const *annotations;

   /* Optional per-label information keyed by instruction index.  When
    * non-NULL, the array must have `num_insts` elements.
    *
    * If the instruction index already has a label, the text is printed inline
    * with the label.  Otherwise it is printed as an extra annotation before
    * the instruction.
    */
   const char *const *label_annotations;

   /* Optional raw encoded bytes for the program being printed.  When
    * non-NULL, gen_print() uses them to determine instruction layout for
    * compacted annotations and byte-offset prefixes.  When insts is NULL,
    * raw_bytes is decoded internally.
    */
   const void *raw_bytes;
   int         raw_bytes_size;

   /* Validate the instruction stream being printed when errors is NULL.
    * Internally generated validation errors are printed inline and are not
    * returned to the caller.
    */
   bool validate;

   /* Passed through to gen_decode() when decoding raw_bytes internally. */
   bool program_subset;

   /* Base address used by GEN_PRINT_BYTE_OFFSETS. */
   uint64_t address_base;
} gen_print_params;

bool gen_print(gen_print_params *params);

void gen_print_inst(const struct intel_device_info *devinfo,
                    FILE *fp,
                    const gen_inst *inst,
                    gen_print_flags flags);

/* Print a software scoreboard annotation in the canonical
 * "<pipe>@<regdist> $<sbid>[.dst|.src]" form.  No brackets, no leading or
 * trailing whitespace; nothing is printed when \p swsb has neither a RegDist
 * nor an SBID dependency.
 *
 * When \p devinfo is non-NULL and the platform has a single in-order pipe
 * (verx10 < 125), the pipe letter is suppressed.  When \p devinfo is NULL the
 * pipe letter is always emitted.
 */
void gen_print_swsb(const struct intel_device_info *devinfo,
                    FILE *fp, gen_swsb swsb);

const char *gen_opcode_to_string(gen_opcode op);


typedef struct gen_parse_params {
   const struct intel_device_info *devinfo;

   const char *text;
   int         text_size;

   /* Must be non-NULL, used for allocating `insts` and `errors`. */
   void *mem_ctx;

   gen_inst *insts;
   int       num_insts;

   /* For parse errors, `index` is the 1-based input line number. */
   gen_error *errors;
   int        num_errors;
} gen_parse_params;

bool gen_parse(gen_parse_params *params);


gen_lsc_desc gen_lsc_desc_decode(const struct intel_device_info *devinfo,
                                 uint32_t desc);

uint32_t gen_lsc_desc_encode(const struct intel_device_info *devinfo,
                             const gen_lsc_desc *desc);

gen_lsc_ex_desc gen_lsc_ex_desc_decode(const struct intel_device_info *devinfo,
                                       enum lsc_opcode op,
                                       enum lsc_addr_surface_type addr_type,
                                       uint32_t ex_desc,
                                       uint32_t ex_desc_imm_extra);

uint32_t gen_lsc_ex_desc_encode(const struct intel_device_info *devinfo,
                                enum lsc_opcode op,
                                const gen_lsc_ex_desc *ex_desc,
                                uint32_t *ex_desc_imm_extra_out);

gen_urb_desc gen_urb_desc_decode(const struct intel_device_info *devinfo,
                                 uint32_t desc);

uint32_t gen_urb_desc_encode(const struct intel_device_info *devinfo,
                             const gen_urb_desc *desc);

gen_sampler_desc gen_sampler_desc_decode(const struct intel_device_info *devinfo,
                                         uint32_t desc);

uint32_t gen_sampler_desc_encode(const struct intel_device_info *devinfo,
                                 const gen_sampler_desc *desc);

gen_hdc_desc gen_hdc_desc_decode(const struct intel_device_info *devinfo,
                                 uint32_t desc);

uint32_t gen_hdc_desc_encode(const struct intel_device_info *devinfo,
                             const gen_hdc_desc *desc);

gen_render_desc gen_render_desc_decode(const struct intel_device_info *devinfo,
                                       uint32_t desc);

uint32_t gen_render_desc_encode(const struct intel_device_info *devinfo,
                                const gen_render_desc *desc);

static inline uint32_t
lsc_msg_desc(const struct intel_device_info *devinfo,
             enum lsc_opcode opcode,
             enum lsc_addr_surface_type addr_type,
             enum lsc_addr_size addr_sz,
             enum lsc_data_size data_sz, unsigned num_channels_or_cmask,
             bool transpose, unsigned cache_ctrl)
{
   assert(devinfo->has_lsc);
   assert(!transpose || lsc_opcode_has_transpose(opcode));

   gen_lsc_desc desc = {};
   desc.op = opcode;
   desc.addr_type = addr_type;
   desc.addr_size = addr_sz;
   desc.data_size = data_sz;
   desc.cache_ctrl = cache_ctrl;

   if (lsc_opcode_has_cmask(opcode)) {
      desc.cmask = (enum lsc_cmask)num_channels_or_cmask;
   } else {
      desc.vect_size = lsc_vect_size(num_channels_or_cmask);
      desc.transpose = transpose;
   }

   return gen_lsc_desc_encode(devinfo, &desc);
}

/* Returns the encoded shader size in bytes starting at `start`.
 *
 * If `end_bound` is non-zero, it is treated as an absolute byte offset from
 * `assembly` and scanning stops before reading past it.  Pass 0 for the old
 * unbounded behavior.
 */
int gen_find_shader_size(const struct intel_device_info *devinfo,
                         const void *assembly,
                         int start,
                         int end_bound);

#ifdef __cplusplus
} /* extern "C" */
#endif
