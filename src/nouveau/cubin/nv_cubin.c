/*
 * Copyright © 2025 Autumn Ashton
 * SPDX-License-Identifier: MIT
 */
#include "nv_cubin.h"

#include "nv_cubin_defs.h"

#include "util/log.h"
#include "util/macros.h"

#include <libelf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* these symbols are not defined in the libelf lib used for android in CI */
#ifndef ELF64_ST_VISIBILITY
#define ELF64_ST_VISIBILITY(o)	((o)&0x3)
#endif
#ifndef STB_GLOBAL
#define STB_GLOBAL	1
#endif
#ifndef STT_FUNC
#define STT_FUNC	2
#endif
#ifndef STV_DEFAULT
#define STV_DEFAULT	0
#endif

static bool
nv_cubin_error(const char *what)
{
   mesa_loge("nvk cubin elf error: %s (%s)\n", what,
             elf_errmsg(elf_errno()));
   return false;
}

static Elf_Scn *
nv_cubin_module_get_section(struct nv_cubin_module *module,
                            const char *lookup_name)
{
   Elf_Scn *section = NULL;
   while ((section = elf_nextscn(module->elf, section)) != NULL) {
      Elf64_Shdr *header = elf64_getshdr(section);
      if (!header)
         continue;

      const char *section_name =
         elf_strptr(module->elf, module->shstrndx, header->sh_name);
      if (!section_name || !*section_name)
         continue;

      if (!strcmp(section_name, lookup_name))
         return section;
   }

   return NULL;
}

static uint32_t
nv_cubin_module_get_static_smem_size(struct nv_cubin_module *module,
                                     const char *name)
{
   char section_name[2048];
   snprintf(section_name, sizeof(section_name), ".nv.shared.%s", name);

   Elf_Scn *section = nv_cubin_module_get_section(module, section_name);
   if (!section)
      return 0;

   Elf_Data *data = elf_getdata(section, NULL);
   if (!data)
      return 0;

   /* Assume this is all zero'ed. */
   return (uint32_t)data->d_size;
}

static bool
nv_cubin_function_parse_base_nvinfo(struct nv_cubin_module *module,
                                    struct nv_cubin_function *function)
{
   Elf_Scn *section = nv_cubin_module_get_section(module, ".nv.info");
   if (!section)
      return nv_cubin_error("No .nv.info section");

   Elf_Data *data = elf_getdata(section, NULL);
   if (!data)
      return nv_cubin_error("No .nv.info section data");

   const uint8_t *ptr = data->d_buf;
   const uint8_t *end = ptr + data->d_size;

   while (ptr < end) {
      const struct nvinfo_attribute_header_t *attr_header =
         (const struct nvinfo_attribute_header_t *)ptr;
      ptr += sizeof(struct nvinfo_attribute_header_t);

      const void *data = ptr;

      switch (attr_header->attribute) {
      case NVINFO_EIATTR_MIN_STACK_SIZE: {
         assert(attr_header->format == NVINFO_EIFMT_SVAL);
         const struct nvinfo_attr_min_stack_size_t *attr = data;
         if (attr->symbol_index == function->symbol_index)
            function->slm_size = attr->size;
         break;
      }
      case NVINFO_EIATTR_REGCOUNT: {
         assert(attr_header->format == NVINFO_EIFMT_SVAL);
         const struct nvinfo_attr_regcount_t *attr = data;
         if (attr->symbol_index == function->symbol_index)
            function->gpr_count = attr->count;
         break;
      }
      default:
         break;
      }

      if (attr_header->format == NVINFO_EIFMT_SVAL)
         ptr += attr_header->sval.size;
   }

   return true;
}

static bool
nv_cubin_function_parse_extended_nvinfo(struct nv_cubin_module *module,
                                        struct nv_cubin_function *function)
{
   char nvinfo_name[2048];
   sprintf(nvinfo_name, ".nv.info.%s", function->name);

   Elf_Scn *section = nv_cubin_module_get_section(module, nvinfo_name);
   if (!section)
      return nv_cubin_error("No .nv.info section");

   Elf_Data *data = elf_getdata(section, NULL);
   if (!data)
      return nv_cubin_error("No .nv.info section data");

   const uint8_t *ptr = data->d_buf;
   const uint8_t *end = ptr + data->d_size;

   while (ptr < end) {
      const struct nvinfo_attribute_header_t *attr_header =
         (const struct nvinfo_attribute_header_t *)ptr;
      ptr += sizeof(struct nvinfo_attribute_header_t);

      const void *data = ptr;

      switch (attr_header->attribute) {
      case NVINFO_EIATTR_PARAM_CBANK: {
         if (attr_header->format != NVINFO_EIFMT_SVAL)
            return nv_cubin_error("Unexpected header format");
         const struct nvinfo_param_cbank_t *attr = data;
         function->params_offset = attr->params_offset;
         function->params_size = attr->params_size;
         break;
      }
      case NVINFO_EIATTR_KPARAM_INFO: {
         if (attr_header->format != NVINFO_EIFMT_SVAL)
            return nv_cubin_error("Unexpected header format");
         const struct nvinfo_kparam_info_t *attr = data;

         uint32_t param_idx = function->param_info_count++;
         function->param_infos = realloc(
            function->param_infos, sizeof(struct nv_cubin_function_param_info) *
                                      function->param_info_count);
         struct nv_cubin_function_param_info *func_param =
            &function->param_infos[param_idx];

         func_param->index = attr->index;
         func_param->ordinal = attr->ordinal;
         func_param->offset = attr->offset;
         func_param->size = (attr->packed >> 18) & 0x3fff;
         break;
      }
      case NVINFO_EIATTR_CRS_STACK_SIZE: {
         if (attr_header->format != NVINFO_EIFMT_SVAL)
            return nv_cubin_error("Unexpected header format");
         const struct nvinfo_crs_stack_size_t *attr = data;
         function->crs_size = attr->size;
         break;
      }
      case NVINFO_EIATTR_NUM_BARRIERS: {
         /* new version has NUM_BARRIERS as an attribute. */
         if (attr_header->format != NVINFO_EIFMT_BVAL)
            return nv_cubin_error("Unexpected header format");
         function->num_control_barriers = attr_header->bval.value;
         break;
      }
      default:
         break;
      }

      if (attr_header->format == NVINFO_EIFMT_SVAL)
         ptr += attr_header->sval.size;
   }

   return true;
}

static bool
nv_cubin_function_init(struct nv_cubin_module *module,
                       struct nv_cubin_function *function,
                       uint32_t symbol_index, const char *symbol_name,
                       Elf64_Sym *symbol)
{
   memset(function, 0, sizeof(*function));

   if (symbol->st_size == 0)
      return nv_cubin_error("Function with no size!");

   size_t code_section_index = symbol->st_shndx;
   if (code_section_index == SHN_UNDEF)
      return nv_cubin_error("Function with no code section!");

   Elf_Scn *code_section = elf_getscn(module->elf, code_section_index);
   if (!code_section)
      return nv_cubin_error("Function with invalid code section!");

   Elf64_Shdr *code_section_header = elf64_getshdr(code_section);

   if (!code_section_header)
      return nv_cubin_error("Couldn't get code section header");

   Elf_Data *code_section_data = elf_getdata(code_section, NULL);
   if (!code_section_data)
      return nv_cubin_error("Couldn't get code section data");

   size_t offset_in_section = symbol->st_value - code_section_header->sh_addr;
   if (offset_in_section + symbol->st_size > code_section_data->d_size)
      return nv_cubin_error("Function extends beyond section bounds");

   function->symbol_index = symbol_index;
   function->name = symbol_name;

   function->code_ptr = code_section_data->d_buf;
   function->code_size = code_section_data->d_size;
   function->func_offset = offset_in_section;

   function->static_smem_size =
      nv_cubin_module_get_static_smem_size(module, symbol_name);

   if (module->abi < NV_ELF_OSABI_41) {
      /* old version has SHF_BARRIERS in .sectionheader */
      const uint8_t shf_barriers = (code_section_header->sh_flags >> 20) & 0xf;
      function->num_control_barriers = shf_barriers;
   }

   if (!nv_cubin_function_parse_base_nvinfo(module, function))
      return nv_cubin_error("Couldn't parse base .nv.info");

   if (!nv_cubin_function_parse_extended_nvinfo(module, function))
      return nv_cubin_error("Couldn't parse extended .nv.info");

   return true;
}

static void
nv_cubin_function_fini(struct nv_cubin_function *function)
{
   free(function->param_infos);
}

static Elf64_Sym *
nv_cubin_get_symbol(Elf_Data *data, uint32_t index)
{
   Elf64_Sym *symbol = NULL;

   if (index * sizeof(Elf64_Sym) < data->d_size) {
      Elf64_Sym *symbols = (Elf64_Sym *)data->d_buf;
      symbol = &symbols[index];
   }

    return symbol;
}

static bool
nv_cubin_module_parse_functions(struct nv_cubin_module *module)
{
   /* Iterate over the symbol table and extract the data about each function. */
   Elf_Scn *symtab_section = NULL;
   Elf64_Shdr *symtab_section_header;
   while ((symtab_section = elf_nextscn(module->elf, symtab_section)) != NULL) {
      symtab_section_header = elf64_getshdr(symtab_section);
      if (!symtab_section_header)
         return nv_cubin_error("Couldn't get section header");

      /* We only care about symbol tables. */
      if (symtab_section_header->sh_type != SHT_SYMTAB)
         continue;

      Elf_Data *symtab_data = elf_getdata(symtab_section, NULL);
      if (!symtab_data)
         return nv_cubin_error("Couldn't get .symtab section data");

      uint32_t num_symbols =
         symtab_section_header->sh_size / symtab_section_header->sh_entsize;
      for (uint32_t i = 0; i < num_symbols; i++) {
         Elf64_Sym *symbol = nv_cubin_get_symbol(symtab_data, i);
         if (!symbol)
            return nv_cubin_error("Couldn't get symbol");

         char *symbol_name = elf_strptr(
            module->elf, symtab_section_header->sh_link, symbol->st_name);
         /* We don't care about any unnamed functions. */
         if (!symbol_name || symbol_name[0] == '\0')
            continue;

         if (ELF64_ST_TYPE(symbol->st_info) != STT_FUNC ||
             ELF64_ST_BIND(symbol->st_info) != STB_GLOBAL ||
             ELF64_ST_VISIBILITY(symbol->st_other) != STV_DEFAULT)
            continue;

         uint32_t function_idx = module->function_count++;
         module->functions =
            realloc(module->functions,
                    sizeof(struct nv_cubin_function) * module->function_count);

         struct nv_cubin_function *function = &module->functions[function_idx];
         if (!nv_cubin_function_init(module, function, i, symbol_name, symbol))
            return nv_cubin_error("Couldn't init function");
      }
   }

   return true;
}

/* public interface */

bool
nv_cubin_module_init(struct nv_cubin_module *module, const void *data,
                     size_t size)
{
   if (elf_version(EV_CURRENT) == EV_NONE)
      return nv_cubin_error("Couldn't initialize libelf");

   module->elf = elf_memory((char *)data, size);
   if (!module->elf)
      return nv_cubin_error("Couldn't load elf");

   if (elf_kind(module->elf) != ELF_K_ELF)
      return nv_cubin_error("Cubin was not an elf");

   if (elf_getshdrstrndx(module->elf, &module->shstrndx) != 0)
      return nv_cubin_error("Couldn't get section header string table index");

   Elf64_Ehdr *ehdr = elf64_getehdr(module->elf);
   if (!ehdr)
      return nv_cubin_error("Failed to get elf header");

   if (ehdr->e_machine != 190 /* EM_CUDA */)
      return nv_cubin_error("ELF is not targeting Cuda");

   module->abi = ehdr->e_ident[EI_OSABI];

   /* new version encodes e_flags differently... */
   if (module->abi >= NV_ELF_OSABI_41)
      module->sm = (ehdr->e_flags >> 8) & 0xFF;
   else
      module->sm = (ehdr->e_flags >> 16) & 0xFF;

   if (!nv_cubin_module_parse_functions(module))
      return nv_cubin_error("Couldn't parse functions");

   return true;
}

void
nv_cubin_module_fini(struct nv_cubin_module *module)
{
   if (module->elf) {
      elf_end(module->elf);
      module->elf = NULL;
   }

   for (uint32_t i = 0; i < module->function_count; i++)
      nv_cubin_function_fini(&module->functions[i]);
   module->function_count = 0;

   free(module->functions);
   module->functions = NULL;
}

const struct nv_cubin_function *
nv_cubin_module_find_function(const struct nv_cubin_module *module,
                              const char *name)
{
   for (uint32_t i = 0; i < module->function_count; i++) {
      struct nv_cubin_function *function = &module->functions[i];
      if (!strcmp(function->name, name))
         return function;
   }

   return NULL;
}

const struct nv_cubin_function_param_info *
nv_cubin_function_get_param_info(const struct nv_cubin_function *function,
                                 uint32_t index)
{
   for (int i = 0; i < function->param_info_count; i++) {
      if (function->param_infos[i].ordinal == index)
         return &function->param_infos[i];
   }
   return NULL;
}
