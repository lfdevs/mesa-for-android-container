/*
 * Copyright © 2018 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <stdio.h>
#include <string.h>
#include <getopt.h>

#include "util/ralloc.h"
#include "brw_eu_inst.h"
#include "dev/intel_device_info.h"

#include "brw_asm.h"

enum opt_output_type {
   OPT_OUTPUT_HEX,
   OPT_OUTPUT_C_LITERAL,
   OPT_OUTPUT_BIN,
};

static enum opt_output_type output_type = OPT_OUTPUT_BIN;

static void
print_help(const char *progname, FILE *file)
{
   fprintf(file,
           "Usage: %s [OPTION] inputfile\n"
           "Assemble i965 instructions from input file.\n\n"
           "    -h, --help             display this help and exit\n"
           "    -t, --type=OUTPUT_TYPE OUTPUT_TYPE can be 'bin' (default if omitted),\n"
           "                           'c_literal', or 'hex'\n"
           "    -o, --output           specify output file\n"
           "        --compact          print compacted instructions\n"
           "    -g, --gen=platform     assemble instructions for given \n"
           "                           platform (3 letter platform name)\n"
           "Example:\n"
           "    brw_asm -g kbl input.asm -t hex -o output\n",
           progname);
}

static uint32_t
get_dword(const brw_eu_inst *inst, int idx)
{
   uint32_t dword;
   memcpy(&dword, (char *)inst + 4 * idx, sizeof(dword));
   return dword;
}

static void
print_instruction(FILE *output, bool compact, const brw_eu_inst *instruction)
{
   int byte_limit;

   byte_limit = (compact == true) ? 8 : 16;

   switch (output_type) {
   case OPT_OUTPUT_HEX: {
      fprintf(output, "%02x", ((unsigned char *)instruction)[0]);

      for (unsigned i = 1; i < byte_limit; i++) {
         fprintf(output, " %02x", ((unsigned char *)instruction)[i]);
      }
      break;
   }
   case OPT_OUTPUT_C_LITERAL: {
      fprintf(output, "\t0x%08x,", get_dword(instruction, 0));

      for (unsigned i = 1; i < byte_limit / 4; i++)
         fprintf(output, " 0x%08x,", get_dword(instruction, i));

      break;
   }
   case OPT_OUTPUT_BIN:
      fwrite(instruction, 1, byte_limit, output);
      break;
   }

   if (output_type != OPT_OUTPUT_BIN) {
      fprintf(output, "\n");
   }
}

static struct intel_device_info *
i965_asm_init(uint16_t pci_id)
{
   struct intel_device_info *devinfo;

   devinfo = malloc(sizeof *devinfo);
   if (devinfo == NULL)
      return NULL;

   if (!intel_get_device_info_from_pci_id(pci_id, devinfo)) {
      fprintf(stderr, "can't find device information: pci_id=0x%x\n",
              pci_id);
      free(devinfo);
      return NULL;
   }

   if (devinfo->ver < 9) {
      fprintf(stderr, "device has gfx version %d but must be >= 9, try elk_asm instead",
              devinfo->ver);
      exit(EXIT_FAILURE);
   }

   return devinfo;
}



int main(int argc, char **argv)
{
   void *mem_ctx = ralloc_context(NULL);
   FILE *input_file = NULL;
   char *output_file = NULL;
   int c;
   FILE *output = stdout;
   bool help = false, compact = false;
   uint64_t pci_id = 0;
   struct intel_device_info *devinfo = NULL;
   int result = EXIT_FAILURE;

   const struct option brw_asm_opts[] = {
      { "help",          no_argument,       (int *) &help,      true },
      { "type",          required_argument, NULL,               't' },
      { "gen",           required_argument, NULL,               'g' },
      { "output",        required_argument, NULL,               'o' },
      { "compact",       no_argument,       (int *) &compact,   true },
      { NULL,            0,                 NULL,               0 }
   };

   while ((c = getopt_long(argc, argv, ":t:g:o:h", brw_asm_opts, NULL)) != -1) {
      switch (c) {
      case 'g': {
         const int id = intel_device_name_to_pci_device_id(optarg);
         if (id < 0) {
            fprintf(stderr, "can't parse gen: '%s', expected 3 letter "
                            "platform name\n", optarg);
            goto end;
         } else {
            pci_id = id;
         }
         break;
      }
      case 'h':
         help = true;
         print_help(argv[0], stderr);
         goto end;
      case 't': {
         if (strcmp(optarg, "hex") == 0) {
            output_type = OPT_OUTPUT_HEX;
         } else if (strcmp(optarg, "c_literal") == 0) {
            output_type = OPT_OUTPUT_C_LITERAL;
         } else if (strcmp(optarg, "bin") == 0) {
            output_type = OPT_OUTPUT_BIN;
         } else {
            fprintf(stderr, "invalid value for --type: %s\n", optarg);
            goto end;
         }
         break;
      }
      case 'o':
         output_file = strdup(optarg);
         break;
      case 0:
         break;
      case ':':
         fprintf(stderr, "%s: option `-%c' requires an argument\n",
                 argv[0], optopt);
         goto end;
      case '?':
      default:
         fprintf(stderr, "%s: option `-%c' is invalid: ignored\n",
                 argv[0], optopt);
         goto end;
      }
   }

   if (help || !pci_id) {
      print_help(argv[0], stderr);
      goto end;
   }

   if (optind == argc) {
      fprintf(stderr, "Please specify input file\n");
      goto end;
   }

   const char *filename = argv[optind];
   input_file = fopen(filename, "r");
   if (!input_file) {
      fprintf(stderr, "Unable to read input file : %s\n",
              filename);
      goto end;
   }

   if (output_file) {
      output = fopen(output_file, "w");
      if (!output) {
         fprintf(stderr, "Couldn't open output file\n");
         goto end;
      }
   }

   devinfo = i965_asm_init(pci_id);
   if (!devinfo) {
      fprintf(stderr, "Unable to allocate memory for "
                      "intel_device_info struct instance.\n");
      goto end;
   }

   brw_assemble_result r = brw_assemble(mem_ctx, devinfo, input_file, filename,
                                        compact ? BRW_ASSEMBLE_COMPACT : 0);
   if (!r.bin)
      goto end;

   if (output_type == OPT_OUTPUT_C_LITERAL)
      fprintf(output, "{\n");

   for (int offset = 0; offset < r.bin_size;) {
      const brw_eu_inst *insn = r.bin + offset;
      bool compacted = false;

      if (compact && brw_eu_inst_cmpt_control(devinfo, insn)) {
            offset += 8;
            compacted = true;
      } else {
            offset += 16;
      }

      print_instruction(output, compacted, insn);
   }

   if (output_type == OPT_OUTPUT_C_LITERAL)
      fprintf(output, "}");

   result = EXIT_SUCCESS;
   goto end;

end:
   free(output_file);

   if (input_file)
      fclose(input_file);

   if (output)
      fclose(output);

   ralloc_free(mem_ctx);

   free(devinfo);

   exit(result);
}
