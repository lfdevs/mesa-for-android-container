/*
 * Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "util/ralloc.h"

#include "gen.h"
#include "gen_private.h"

namespace {


static void PRINTFLIKE(1, 2) NORETURN
failf(const char *fmt, ...)
{
   fflush(stdout);
   fprintf(stderr, "ERROR: ");

   va_list args;
   va_start(args, fmt);
   vfprintf(stderr, fmt, args);
   va_end(args);

   fputc('\n', stderr);
   exit(1);
}

static void
print_top_help(FILE *fp)
{
   fprintf(fp,
           "Usage: gentool COMMAND [OPTIONS] [INPUT]\n"
           "\n"
           "Commands:\n"
           "  asm      Assemble Gen instructions into binary output\n"
           "  disasm   Disassemble Gen instruction binaries\n"
           "  region   Show register-region diagrams for a single instruction\n"
           "  help     Show help for a command or help topic\n"
           "\n"
           "Help topics:\n"
           "  asm, disasm, region, platforms, syntax, lsc\n"
           "\n"
           "Supports Gfx9+ platforms.\n"
           "\n"
           "Use 'gentool help TOPIC' for help topics, or 'gentool COMMAND --help'\n"
           "for command-specific help.\n");
}

static void
print_asm_help(FILE *fp)
{
   fprintf(fp,
           "Usage: gentool asm [OPTIONS] [INPUT]\n"
           "\n"
           "Assemble Gen instructions.\n"
           "\n"
           "Options:\n"
           "  -h, --help              Show this help\n"
           "  -p, --platform=PLATFORM 3-letter platform name (required)\n"
           "  -i, --input=PATH        Read assembly from PATH, or '-' for stdin\n"
           "  -o, --output=PATH       Write output to PATH, or '-' for stdout\n"
           "\n"
           "If INPUT is provided positionally, it is used as the input path.\n"
           "\n"
           "Example:\n"
           "  gentool asm -p tgl shader.asm\n");
}

static void
print_disasm_help(FILE *fp)
{
   fprintf(fp,
           "Usage: gentool disasm [OPTIONS] [INPUT]\n"
           "\n"
           "Disassemble Gen binaries.\n"
           "\n"
           "Options:\n"
           "  -h, --help              Show this help\n"
           "  -p, --platform=PLATFORM 3-letter platform name (required)\n"
           "  -i, --input=PATH        Read binary input from PATH, or '-' for stdin\n"
           "  -o, --output=PATH       Write assembly to PATH, or '-' for stdout\n"
           "  -v, --verbose            Print explicit regions, types, and other details\n"
           "      --translated-sends  Print LSC sends as their translated mnemonic instead of\n"
           "                          a raw SEND instruction\n"
           "      --hex               Prefix each instruction with a hex dump of its encoded bytes\n"
           "\n"
           "If INPUT is provided positionally, it is used as the input path.\n"
           "\n"
           "Example:\n"
           "  gentool disasm -p tgl shader.bin\n");
}

static void
print_region_help(FILE *fp)
{
   fprintf(fp,
           "Usage: gentool region [OPTIONS]\n"
           "\n"
           "Parse a single Gen instruction and show the register regions it touches.\n"
           "\n"
           "Options:\n"
           "  -h, --help              Show this help\n"
           "  -p, --platform=PLATFORM 3-letter platform name (required)\n"
           "\n"
           "Reads from stdin, writes to stdout.\n"
           "\n"
           "The input must parse to exactly one instruction.\n"
           "\n"
           "Each register row is drawn as one byte per character. For regular\n"
           "regions, the bytes of logical element 0..v are marked with 0-9a-v.\n"
           "A '*' marks overlapping logical elements. SEND payloads are shown one\n"
           "message register at a time. Immediate operands are reported separately.\n"
           "\n"
           "Currently this command requires Align1 instructions for explicit\n"
           "source/destination region diagrams; SEND payloads are supported too.\n"
           "\n"
           "Example:\n"
           "  printf 'add (8) r5<1>:ud r6<8;8,1>:ud r7<8;8,1>:ud\n' | gentool region -p tgl\n");
}

static void
print_platforms_help(FILE *fp)
{
   fprintf(fp, "Supported platforms (Gfx9+):\n\n");

   for (unsigned i = 0; ; i++) {
      const char *platform = intel_platform_name_by_index(i);
      if (!platform)
         break;

      intel_device_info devinfo = {};
      const int pci_id = intel_device_name_to_pci_device_id(platform);
      if (pci_id < 0 || !intel_get_device_info_for_build(pci_id, &devinfo))
         continue;

      if (devinfo.ver < 9)
         continue;

      if (devinfo.verx10 % 10 == 0) {
         fprintf(fp, "  %-5s gfx%d\n", platform, devinfo.verx10 / 10);
      } else {
         fprintf(fp, "  %-5s gfx%d.%d\n", platform,
                 devinfo.verx10 / 10, devinfo.verx10 % 10);
      }
   }
}

static void
print_syntax_help(FILE *fp)
{
   fprintf(fp,
           "Gen syntax quick reference\n"
           "\n"
           "  add (8) r5 r6 0x0000002a\n"
           "      opcode, exec size, dst, src0, src1\n"
           "      defaults: sequential region/stride, :ud, subnr 0, chan offset 0\n"
           "\n"
           "  mov (8|M0) r10.0<1>:f r11.0<8;8,1>:f\n"
           "      explicit dst stride, src region, and type suffixes\n"
           "      '<v;w,h>' is a source region; '<v>' is shorthand for '<v;1,0>'\n"
           "      the destination uses a stride like '<1>'\n"
           "\n"
           "  cmp (16) (ge)f3.1 r5 r6 r7\n"
           "      conditional modifier writes a flag register\n"
           "\n"
           "  (W&~f0.0) add (8|M0) r1.0<1>:f r2.0<8;8,1>:f 0x3f800000:f\n"
           "      'W' enables write masking; '~f0.0' is the inverted predicate\n"
           "\n"
           "  load.slm.d32x4.a32 (16) r10 r8\n"
           "      translated LSC load syntax\n"
           "      see 'gentool help lsc' for more on LSC mnemonics\n"
           "\n"
           "  send.ugm (1|M0) r6 r5:1 null:0 a0.0 0x2229e500\n"
           "      raw SEND syntax; ':1' and ':0' are source lengths\n"
           "\n"
           "  if   (16) jip:L10 uip:L20\n"
           "  add  (16) r20 r20 1:d\n"
           "  else (16) jip:L20 uip:L20\n"
           "  L10:\n"
           "  add  (16) r21 r21 -1:d\n"
           "  L20:\n"
           "      labels may be used as branch targets in structured control flow\n"
           "\n");
}

static void
print_lsc_help(FILE *fp)
{
   fprintf(fp,
           "LSC syntax quick reference\n"
           "\n"
           "  load.slm.d32x4.a32 (16) r10 r8\n"
           "      load.<sfid>.<data_shape>.<addr_size>\n"
           "      dst is the response GRF, src is the address payload\n"
           "\n"
           "  load.ugm.d32x32t.a32.ca.cc.bss[a0.0] (1) r6 r5\n"
           "      ugm/tgm/slm select the LSC message target\n"
           "      ca.cc are the L1/L3 cache controls; bss[a0.0] is the surface syntax\n"
           "\n"
           "  load_cmask.tgm.d32.xy.a32.ca.ca.bti[3] (8) r20 r12\n"
           "      typed loads with load_cmask; 'xy' is the channel mask\n"
           "\n"
           "  atomic_add.ugm.d32.a32.wt.wb.bss[a0.0] (8) r40 r35 r8\n"
           "      atomics take an address payload plus source data payload(s)\n"
           "\n"
           "  send.ugm (1|M0) r6 r5:1 null:0 a0.0 0x2229e500\n"
           "      raw SEND form of the translated UGM load above\n"
           "\n"
           "Surface syntaxes:\n"
           "  flat               flat addressing (can be omitted)\n"
           "  slm                shared local memory\n"
           "  bss[a0.0]          bindless surface from an address register\n"
           "  ss[a0.0]           surface state from an address register\n"
           "  bti[3]             surface state by BTI index\n"
           "\n"
           "Cache control suffixes are written as <L1>.<L3>:\n"
           "  uc = uncached\n"
           "  ca = cached\n"
           "  cc = cached as constant (Xe2+ loads)\n"
           "  st = streaming\n"
           "  ri = invalidate-after-read\n"
           "  wt = write-through (stores)\n"
           "  wb = write-back (stores)\n");
}

static char
element_char(unsigned idx)
{
   static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
   return idx < ARRAY_SIZE(digits) - 1 ? digits[idx] : '#';
}

static const char *
send_desc_label(bool ex_desc)
{
   return ex_desc ? "ex_desc" : "desc";
}

static std::string
register_name(gen_file file, unsigned nr)
{
   if (file == GEN_GRF)
      return "r" + std::to_string(nr);

   if (file != GEN_ARF)
      return "reg" + std::to_string(nr);

   const unsigned arf = nr & 0xf0;
   const unsigned idx = nr & 0x0f;

   switch (arf) {
   case GEN_ARF_NULL:
      return "null";
   case GEN_ARF_ADDRESS:
      return "a" + std::to_string(idx);
   case GEN_ARF_ACCUMULATOR:
      return "acc" + std::to_string(idx);
   case GEN_ARF_FLAG:
      return "f" + std::to_string(idx);
   case GEN_ARF_MASK:
      return "mask" + std::to_string(idx);
   case GEN_ARF_SCALAR:
      return "s" + std::to_string(idx);
   case GEN_ARF_STATE:
      return "sr" + std::to_string(idx);
   case GEN_ARF_CONTROL:
      return "cr" + std::to_string(idx);
   case GEN_ARF_NOTIFICATION_COUNT:
      return "n" + std::to_string(idx);
   case GEN_ARF_IP:
      return "ip";
   case GEN_ARF_TDR:
      return "tdr" + std::to_string(idx);
   case GEN_ARF_TIMESTAMP:
      return "tm" + std::to_string(idx);
   default:
      return "arf" + std::to_string(nr);
   }
}

struct region_diagram {
   gen_file file = GEN_BAD_FILE;
   unsigned row_size = 0;
   int first_row = 0;
   std::vector<std::string> rows;
};

static void
region_diagram_reserve_row(region_diagram *diag, int row)
{
   if (diag->rows.empty()) {
      diag->first_row = row;
      diag->rows.emplace_back(diag->row_size, '.');
      return;
   }

   if (row < diag->first_row) {
      diag->rows.insert(diag->rows.begin(), diag->first_row - row,
                        std::string(diag->row_size, '.'));
      diag->first_row = row;
   } else if ((size_t)(row - diag->first_row) >= diag->rows.size()) {
      diag->rows.resize(row - diag->first_row + 1,
                        std::string(diag->row_size, '.'));
   }
}

static void
region_diagram_mark_byte(region_diagram *diag, int row, unsigned col, char value)
{
   assert(col < diag->row_size);
   region_diagram_reserve_row(diag, row);

   char &slot = diag->rows[row - diag->first_row][col];
   if (slot == '.' || slot == value) {
      slot = value;
   } else {
      slot = '*';
   }
}

static void
region_diagram_mark_span(region_diagram *diag,
                         const gen_operand &op,
                         unsigned start,
                         unsigned size,
                         char value)
{
   unsigned offset = op.subnr + start;
   for (unsigned i = 0; i < size; i++, offset++) {
      const int row = op.nr + offset / diag->row_size;
      const unsigned col = offset % diag->row_size;
      region_diagram_mark_byte(diag, row, col, value);
   }
}

static void
print_region_row(FILE *fp, const std::string &row)
{
   for (size_t i = 0; i < row.size(); i++) {
      const size_t rev_i = row.size() - 1 - i;

      if (i > 0) {
         if (i % 16 == 0) {
            fputs("  ", fp);
         } else if (i % 4 == 0) {
            fputc(' ', fp);
         }
      }

      fputc(row[rev_i], fp);
   }
}

static void
print_region_diagram(FILE *fp, const region_diagram &diag)
{
   if (diag.rows.empty()) {
      fputs("| (no register bytes) |\n", fp);
      return;
   }

   for (size_t i = 0; i < diag.rows.size(); i++) {
      const int row = diag.first_row + i;
      const std::string reg_name = register_name(diag.file, row);
      fputs("| ", fp);
      print_region_row(fp, diag.rows[i]);
      fprintf(fp, " | %s\n", reg_name.c_str());
   }
}


static bool
build_align1_dst_diagram(const intel_device_info *devinfo,
                         const gen_inst *inst,
                         region_diagram *diag)
{
   if (inst->align16)
      return false;

   const gen_operand &dst = inst->dst;
   if (dst.indirect || dst.file == GEN_IMM || is_null(dst))
      return false;

   diag->file = dst.file;
   diag->row_size = devinfo->grf_size;

   const unsigned type_size = MAX2(gen_type_size_bytes(dst.type), 1u);
   const unsigned hstride = dst.region.hstride;

   for (unsigned i = 0; i < inst->exec_size; i++) {
      const unsigned start = i * hstride * type_size;
      region_diagram_mark_span(diag, dst, start, type_size, element_char(i));
   }

   return true;
}

static bool
build_align1_src_diagram(const intel_device_info *devinfo,
                         const gen_inst *inst,
                         const gen_operand &src,
                         region_diagram *diag)
{
   if (inst->align16)
      return false;

   if (src.indirect || src.file == GEN_IMM)
      return false;

   diag->file = src.file;
   diag->row_size = devinfo->grf_size;

   const unsigned type_size = MAX2(gen_type_size_bytes(src.type), 1u);

   if (src.region.vstride == GEN_VSTRIDE_ONE_DIMENSIONAL) {
      for (unsigned i = 0; i < inst->exec_size; i++) {
         const unsigned start = i * src.region.hstride * type_size;
         region_diagram_mark_span(diag, src, start, type_size, element_char(i));
      }
      return true;
   }

   const unsigned width = MAX2(src.region.width, 1u);
   const unsigned rows = DIV_ROUND_UP(inst->exec_size, width);
   unsigned elem = 0;
   unsigned rowbase = 0;

   for (unsigned y = 0; y < rows && elem < inst->exec_size; y++) {
      unsigned offset = rowbase;
      for (unsigned x = 0; x < width && elem < inst->exec_size; x++, elem++) {
         region_diagram_mark_span(diag, src, offset, type_size, element_char(elem));
         offset += src.region.hstride * type_size;
      }
      rowbase += src.region.vstride * type_size;
   }

   return true;
}

static bool
build_send_payload_diagram(const intel_device_info *devinfo,
                           const gen_operand &op,
                           unsigned reg_count,
                           region_diagram *diag)
{
   if (op.indirect || op.file == GEN_IMM || is_null(op) || reg_count == 0)
      return false;

   diag->file = op.file;
   diag->row_size = devinfo->grf_size;

   for (unsigned r = 0; r < reg_count; r++) {
      const unsigned start = r * diag->row_size;
      region_diagram_mark_span(diag, op, start, diag->row_size, element_char(r));
   }

   return true;
}

static bool
build_send_desc_diagram(const intel_device_info *devinfo,
                        const gen_operand &op,
                        region_diagram *diag)
{
   if (op.indirect || op.file == GEN_IMM || is_null(op))
      return false;

   diag->file = op.file;
   diag->row_size = devinfo->grf_size;
   region_diagram_mark_span(diag, op, 0, 4, 'd');
   return true;
}


static int
send_dst_len(const gen_inst *inst)
{
   if (inst->send.desc_is_reg)
      return -1;
   return (inst->send.desc_imm >> 20) & 0x1F;
}

static void
print_regular_region_output(const intel_device_info *devinfo,
                            FILE *fp,
                            const gen_inst *inst)
{
   const gen_format format = gen_inst_format(inst->opcode);
   const bool has_dst = gen_inst_has_dst(format, inst->opcode);
   const unsigned num_sources = gen_inst_num_sources(devinfo, inst);

   if (inst->align16) {
      fputs("regions\n| (Align16 region diagrams are not supported yet) |\n\n", fp);
      return;
   }

   if (has_dst) {
      fputs("dst\n", fp);
      if (is_null(inst->dst)) {
         fputs("| (null destination) |\n", fp);
      } else if (inst->dst.indirect) {
         fputs("| (indirect destination is not supported) |\n", fp);
      } else {
         region_diagram diag = {};
         if (build_align1_dst_diagram(devinfo, inst, &diag))
            print_region_diagram(fp, diag);
         else
            fputs("| (destination has no direct register bytes) |\n", fp);
      }
      fputc('\n', fp);
   }

   for (unsigned i = 0; i < num_sources; i++) {
      char label[16];
      snprintf(label, sizeof(label), "src%u", i);

      const gen_operand &src = inst->src[i];
      fprintf(fp, "%s\n", label);
      if (src.file == GEN_IMM) {
         fputs("| (immediate operand) |\n", fp);
      } else if (src.indirect) {
         fputs("| (indirect source is not supported) |\n", fp);
      } else {
         region_diagram diag = {};
         if (build_align1_src_diagram(devinfo, inst, src, &diag))
            print_region_diagram(fp, diag);
         else
            fputs("| (source has no direct register bytes) |\n", fp);
      }
      fputc('\n', fp);
   }
}

static void
print_send_region_output(const intel_device_info *devinfo,
                         FILE *fp,
                         const gen_inst *inst)
{
   const int dst_len = send_dst_len(inst);
   const int src0_len = gen_inst_send_src0_len(inst);
   const int src1_len = gen_inst_is_split_send(devinfo, inst) ?
      gen_inst_send_src1_len(devinfo, inst) : 0;

   fputs("dst\n", fp);
   if (is_null(inst->dst)) {
      fputs("| (null destination) |\n", fp);
   } else if (dst_len < 0) {
      fputs("| (response length comes from a descriptor register) |\n", fp);
   } else {
      region_diagram diag = {};
      if (build_send_payload_diagram(devinfo, inst->dst, dst_len, &diag))
         print_region_diagram(fp, diag);
      else
         fputs("| (destination payload has no direct register bytes) |\n", fp);
   }
   fputc('\n', fp);

   fputs("src0\n", fp);
   if (src0_len < 0) {
      fputs("| (payload length comes from a descriptor register) |\n", fp);
   } else {
      region_diagram diag = {};
      if (build_send_payload_diagram(devinfo, inst->src[0], src0_len, &diag))
         print_region_diagram(fp, diag);
      else
         fputs("| (source payload has no direct register bytes) |\n", fp);
   }
   fputc('\n', fp);

   if (gen_inst_is_split_send(devinfo, inst)) {
      fputs("src1\n", fp);
      if (is_null(inst->src[1])) {
         fputs("| (null payload) |\n", fp);
      } else if (src1_len < 0) {
         fputs("| (extended payload length comes from an ex_desc register) |\n", fp);
      } else {
         region_diagram diag = {};
         if (build_send_payload_diagram(devinfo, inst->src[1], src1_len, &diag))
            print_region_diagram(fp, diag);
         else
            fputs("| (extended payload has no direct register bytes) |\n", fp);
      }
      fputc('\n', fp);
   }

   for (unsigned i = 0; i < 2; i++) {
      const bool ex_desc = i != 0;
      const bool is_reg = ex_desc ? inst->send.ex_desc_is_reg : inst->send.desc_is_reg;
      if (!is_reg)
         continue;

      gen_operand op = {};
      op.file = GEN_ARF;
      op.nr = GEN_ARF_ADDRESS;
      op.type = GEN_TYPE_UD;
      op.subnr = ex_desc ? inst->send.ex_desc_subnr : 0;

      fprintf(fp, "%s\n", send_desc_label(ex_desc));
      region_diagram diag = {};
      if (build_send_desc_diagram(devinfo, op, &diag))
         print_region_diagram(fp, diag);
      else
         fputs("| (descriptor register is not a direct register operand) |\n", fp);
      fputc('\n', fp);
   }
}

static void
print_region_output(const intel_device_info *devinfo,
                    FILE *fp,
                    const gen_inst *inst)
{
   switch (gen_inst_format(inst->opcode)) {
   case GEN_FORMAT_SEND:
      print_send_region_output(devinfo, fp, inst);
      break;

   case GEN_FORMAT_BASIC_ONE_SRC:
   case GEN_FORMAT_BASIC_TWO_SRC:
   case GEN_FORMAT_BASIC_THREE_SRC:
   case GEN_FORMAT_DPAS_THREE_SRC:
      print_regular_region_output(devinfo, fp, inst);
      break;

   case GEN_FORMAT_BRANCH_ONE_SRC:
   case GEN_FORMAT_BRANCH_TWO_SRC:
   case GEN_FORMAT_NOP:
   case GEN_FORMAT_ILLEGAL:
      fputs("regions\n| (instruction has no explicit register regions) |\n\n", fp);
      break;
   }
}


static void
init_devinfo(const char *platform, intel_device_info *devinfo)
{
   const int pci_id = intel_device_name_to_pci_device_id(platform);
   if (pci_id < 0)
      failf("can't parse platform '%s', expected 3 letter platform name", platform);

   if (!intel_get_device_info_from_pci_id(pci_id, devinfo))
      failf("can't find device information for '%s'", platform);

   if (devinfo->ver < 9)
      failf("device has gfx version %d, but gentool currently requires gfx9+",
            devinfo->ver);
}


static FILE *
open_output(const std::string &path, bool binary)
{
   if (path == "-" || path.empty())
      return stdout;

   FILE *fp = fopen(path.c_str(), binary ? "wb" : "w");
   if (!fp)
      failf("failed to open output '%s': %s", path.c_str(), strerror(errno));

   return fp;
}

static void
read_input(const std::string &path, std::string *out)
{
   std::ifstream file;
   if (path != "-" && !path.empty()) {
      file.open(path, std::ios::binary);
      if (!file)
         failf("failed to open input '%s': %s", path.c_str(), strerror(errno));
   }

   std::istream &stream = file.is_open() ? file : std::cin;

   std::ostringstream ss;
   ss << stream.rdbuf();
   *out = ss.str();

   if (stream.bad())
      failf("failed to read input '%s' (stream error)",
            path == "-" ? "<stdin>" : path.c_str());
}

static void
write_output_bytes(const std::string &path, const void *data, size_t size)
{
   FILE *output = open_output(path, true);
   if (size > 0 && fwrite(data, 1, size, output) != size)
      failf("failed to write binary output: %s", strerror(errno));

   if (output == stdout) {
      if (fflush(output) != 0)
         failf("failed to flush output: %s", strerror(errno));
   } else if (fclose(output) != 0) {
      failf("failed to close output '%s': %s", path.c_str(), strerror(errno));
   }
}

static void
print_parse_errors(const std::string &input_name,
                   const gen_error *errors,
                   int num_errors)
{
   for (int i = 0; i < num_errors; i++) {
      fprintf(stderr, "%s:%u: %s\n", input_name.c_str(),
              errors[i].index, errors[i].msg);
   }
}


static int
cmd_help(int argc, char **argv)
{
   if (argc <= 1) {
      print_top_help(stdout);
      return 0;
   }

   if (argc > 2) {
      fprintf(stderr, "gentool help: unexpected extra argument '%s'\n", argv[2]);
      return 1;
   }

   const std::string sub = argv[1];
   if (sub == "asm") {
      print_asm_help(stdout);
      return 0;
   }

   if (sub == "disasm") {
      print_disasm_help(stdout);
      return 0;
   }

   if (sub == "region") {
      print_region_help(stdout);
      return 0;
   }

   if (sub == "platforms") {
      print_platforms_help(stdout);
      return 0;
   }

   if (sub == "syntax") {
      print_syntax_help(stdout);
      return 0;
   }

   if (sub == "lsc") {
      print_lsc_help(stdout);
      return 0;
   }

   fprintf(stderr, "gentool: unknown help topic '%s'\n", argv[1]);
   fprintf(stderr, "Available help topics: asm, disasm, region, platforms, syntax, lsc\n");
   return 1;
}

static int
cmd_asm(int argc, char **argv)
{
   std::string input_path = "-";
   std::string output_path = "-";
   intel_device_info devinfo = {};
   bool have_devinfo = false;

   static const struct option long_options[] = {
      {"help",     no_argument,       NULL, 'h'},
      {"platform", required_argument, NULL, 'p'},
      {"gen",      required_argument, NULL, 'p'},
      {"input",    required_argument, NULL, 'i'},
      {"output",   required_argument, NULL, 'o'},
      {},
   };

   optind = 1;
   opterr = 0;

   while (true) {
      const int c = getopt_long(argc, argv, "hp:i:o:", long_options, NULL);
      if (c == -1)
         break;

      switch (c) {
      case 'h':
         print_asm_help(stdout);
         return 0;
      case 'p': {
         init_devinfo(optarg, &devinfo);
         have_devinfo = true;
         break;
      }
      case 'i':
         input_path = optarg;
         break;
      case 'o':
         output_path = optarg;
         break;
      case '?':
      default:
         failf("unknown option '%s'", argv[optind - 1]);
      }
   }

   if (optind < argc) {
      if (input_path != "-")
         failf("input specified both by --input and positionally");
      input_path = argv[optind++];
   }

   if (optind != argc)
      failf("unexpected extra argument '%s'", argv[optind]);

   if (!have_devinfo)
      failf("missing required --platform option");

   std::string text;
   const std::string input_name = input_path == "-" ? "<stdin>" : input_path;

   read_input(input_path, &text);

   if (text.size() > INT_MAX)
      failf("input is too large");

   void *mem_ctx = ralloc_context(NULL);

   gen_parse_params parse_params = {};
   parse_params.devinfo = &devinfo;
   parse_params.text = text.c_str();
   parse_params.text_size = (int)text.size();
   parse_params.mem_ctx = mem_ctx;

   if (!gen_parse(&parse_params)) {
      fprintf(stderr, "gentool asm: parse failed\n");
      print_parse_errors(input_name, parse_params.errors, parse_params.num_errors);
      ralloc_free(mem_ctx);
      return 1;
   }

   if (parse_params.num_insts == 0) {
      write_output_bytes(output_path, NULL, 0);
      ralloc_free(mem_ctx);
      return 0;
   }

   const int raw_bytes_size = parse_params.num_insts * (int)sizeof(gen_raw_inst);

   gen_encode_params encode_params = {};
   encode_params.devinfo = &devinfo;
   encode_params.insts = parse_params.insts;
   encode_params.num_insts = parse_params.num_insts;
   encode_params.mem_ctx = mem_ctx;
   encode_params.raw_bytes = rzalloc_size(mem_ctx, raw_bytes_size);
   encode_params.raw_bytes_size = raw_bytes_size;

   if (!gen_encode(&encode_params)) {
      fprintf(stderr, "gentool asm: validation failed\n");

      gen_print_params print_params = {};
      print_params.devinfo = &devinfo;
      print_params.fp = stderr;
      print_params.flags = GEN_PRINT_VERBOSE;
      print_params.insts = parse_params.insts;
      print_params.num_insts = parse_params.num_insts;
      print_params.errors = encode_params.errors;
      print_params.num_errors = encode_params.num_errors;
      gen_print(&print_params);

      ralloc_free(mem_ctx);
      return 1;
   }

   write_output_bytes(output_path, encode_params.raw_bytes,
                      encode_params.raw_bytes_size);

   ralloc_free(mem_ctx);
   return 0;
}

static int
cmd_disasm(int argc, char **argv)
{
   std::string input_path = "-";
   std::string output_path = "-";
   intel_device_info devinfo = {};
   bool have_devinfo = false;
   gen_print_flags print_flags = GEN_PRINT_NONE;

   enum { OPT_TRANSLATED_SENDS = 1000, OPT_HEX };

   static const struct option long_options[] = {
      {"help",      no_argument,       NULL, 'h'},
      {"platform",  required_argument, NULL, 'p'},
      {"gen",       required_argument, NULL, 'p'},
      {"input",     required_argument, NULL, 'i'},
      {"output",    required_argument, NULL, 'o'},
      {"verbose",          no_argument, NULL, 'v'},
      {"translated-sends", no_argument, NULL, OPT_TRANSLATED_SENDS},
      {"hex",              no_argument, NULL, OPT_HEX},
      {},
   };

   optind = 1;
   opterr = 0;

   while (true) {
      const int c = getopt_long(argc, argv, "hp:i:o:v", long_options, NULL);
      if (c == -1)
         break;

      switch (c) {
      case 'h':
         print_disasm_help(stdout);
         return 0;
      case 'p': {
         init_devinfo(optarg, &devinfo);
         have_devinfo = true;
         break;
      }
      case 'i':
         input_path = optarg;
         break;
      case 'o':
         output_path = optarg;
         break;
      case 'v':
         print_flags = (gen_print_flags)(print_flags | GEN_PRINT_VERBOSE);
         break;
      case OPT_TRANSLATED_SENDS:
         print_flags = (gen_print_flags)(print_flags | GEN_PRINT_TRANSLATED_SENDS);
         break;
      case OPT_HEX:
         print_flags = (gen_print_flags)(print_flags | GEN_PRINT_HEX);
         break;
      case '?':
      default:
         failf("unknown option '%s'", argv[optind - 1]);
      }
   }

   if (optind < argc) {
      if (input_path != "-")
         failf("input specified both by --input and positionally");
      input_path = argv[optind++];
   }

   if (optind != argc)
      failf("unexpected extra argument '%s'", argv[optind]);

   if (!have_devinfo)
      failf("missing required --platform option");

   std::string raw_bytes;
   read_input(input_path, &raw_bytes);

   if (raw_bytes.size() > INT_MAX)
      failf("input is too large");

   if (raw_bytes.size() % 8 != 0)
      failf("input size must be a multiple of 8 bytes");

   if (raw_bytes.empty()) {
      FILE *output = open_output(output_path, false);

      if (fflush(output) != 0)
         failf("failed to flush output: %s", strerror(errno));

      if (output != stdout && fclose(output) != 0)
         failf("failed to close output '%s': %s", output_path.c_str(), strerror(errno));

      return 0;
   }

   void *mem_ctx = ralloc_context(NULL);

   gen_decode_params decode_params = {};
   decode_params.devinfo = &devinfo;
   decode_params.raw_bytes = raw_bytes.data();
   decode_params.raw_bytes_size = (int)raw_bytes.size();
   decode_params.mem_ctx = mem_ctx;
   if (!gen_decode(&decode_params)) {
      ralloc_free(mem_ctx);
      failf("decode failed");
   }

   gen_validate_params validate_params = {};
   validate_params.devinfo = &devinfo;
   validate_params.insts = decode_params.insts;
   validate_params.num_insts = decode_params.num_insts;
   validate_params.mem_ctx = mem_ctx;
   const bool valid = gen_validate(&validate_params);

   FILE *output = open_output(output_path, false);

   gen_print_params print_params = {};
   print_params.devinfo = &devinfo;
   print_params.fp = output;
   print_params.flags = print_flags;
   print_params.insts = decode_params.insts;
   print_params.num_insts = decode_params.num_insts;
   print_params.errors = validate_params.errors;
   print_params.num_errors = validate_params.num_errors;
   print_params.raw_bytes = raw_bytes.data();
   print_params.raw_bytes_size = (int)raw_bytes.size();
   if (!gen_print(&print_params)) {
      ralloc_free(mem_ctx);
      failf("print failed");
   }

   if (fflush(output) != 0) {
      ralloc_free(mem_ctx);
      failf("failed to flush output: %s", strerror(errno));
   }

   if (output != stdout && fclose(output) != 0) {
      ralloc_free(mem_ctx);
      failf("failed to close output '%s': %s", output_path.c_str(), strerror(errno));
   }

   ralloc_free(mem_ctx);
   return valid ? 0 : 1;
}

static int
cmd_region(int argc, char **argv)
{
   intel_device_info devinfo = {};
   bool have_devinfo = false;

   static const struct option long_options[] = {
      {"help",     no_argument,       NULL, 'h'},
      {"platform", required_argument, NULL, 'p'},
      {"gen",      required_argument, NULL, 'p'},
      {},
   };

   optind = 1;
   opterr = 0;

   while (true) {
      const int c = getopt_long(argc, argv, "hp:", long_options, NULL);
      if (c == -1)
         break;

      switch (c) {
      case 'h':
         print_region_help(stdout);
         return 0;
      case 'p': {
         init_devinfo(optarg, &devinfo);
         have_devinfo = true;
         break;
      }
      case '?':
      default:
         failf("unknown option '%s'", argv[optind - 1]);
      }
   }

   if (optind != argc)
      failf("unexpected extra argument '%s'", argv[optind]);

   if (!have_devinfo)
      failf("missing required --platform option");

   std::string text;
   read_input("-", &text);

   if (text.size() > INT_MAX)
      failf("input is too large");

   void *mem_ctx = ralloc_context(NULL);

   gen_parse_params parse_params = {};
   parse_params.devinfo = &devinfo;
   parse_params.text = text.c_str();
   parse_params.text_size = (int)text.size();
   parse_params.mem_ctx = mem_ctx;

   if (!gen_parse(&parse_params)) {
      fprintf(stderr, "gentool region: parse failed\n");
      print_parse_errors("<stdin>", parse_params.errors, parse_params.num_errors);
      ralloc_free(mem_ctx);
      return 1;
   }

   if (parse_params.num_insts != 1) {
      fprintf(stderr,
              "gentool region: expected exactly one instruction, got %d\n",
              parse_params.num_insts);
      ralloc_free(mem_ctx);
      return 1;
   }

   print_region_output(&devinfo, stdout, &parse_params.insts[0]);

   ralloc_free(mem_ctx);
   return 0;
}

} /* namespace */

int
main(int argc, char **argv)
{
   struct command {
      const char *name;
      int (*func)(int argc, char **argv);
   };

   static const command cmds[] = {
      { "asm",    cmd_asm },
      { "disasm", cmd_disasm },
      { "region", cmd_region },
      { "help",   cmd_help },
   };

   if (argc <= 1) {
      print_top_help(stderr);
      return 1;
   }

   if (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) {
      print_top_help(stdout);
      return 0;
   }

   const command *cmd = NULL;
   for (const command *c = cmds; c < cmds + ARRAY_SIZE(cmds); c++) {
      if (!strcmp(c->name, argv[1])) {
         cmd = c;
         break;
      }
   }

   if (cmd)
      return cmd->func(argc - 1, argv + 1);

   fprintf(stderr, "gentool: unknown command '%s'\n", argv[1]);
   print_top_help(stderr);
   return 1;
}
