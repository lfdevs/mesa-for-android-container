/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "error_decode_xe_lib.h"

#include <stdlib.h>
#include <string.h>

#include "error_decode_lib.h"
#include "intel/common/intel_gem.h"
#include "util/macros.h"
#include "util/compiler.h"

static const char *
read_parameter_helper(const char *line, const char *parameter)
{
   if (!strstr(line, parameter))
      return NULL;

   while (*line != ':')
      line++;
   /* skip ':' and ' ' */
   line += 2;

   return line;
}

/* parse lines like 'batch_addr[0]: 0x0000effeffff5000 */
bool
error_decode_xe_read_u64_hexacimal_parameter(const char *line, const char *parameter, uint64_t *value)
{
   line = read_parameter_helper(line, parameter);
   if (!line)
      return false;

   *value = (uint64_t)strtoull(line, NULL, 0);
   return true;
}

/* parse lines like 'PCI ID: 0x9a49' */
bool
error_decode_xe_read_hexacimal_parameter(const char *line, const char *parameter, uint32_t *value)
{
   line = read_parameter_helper(line, parameter);
   if (!line)
      return false;

   *value = (int)strtoul(line, NULL, 0);
   return true;
}

/* parse lines like 'rcs0 (physical), logical instance=0' */
bool
error_decode_xe_read_engine_name(const char *line, char *ring_name)
{
   int i;

   if (!strstr(line, " (physical), logical instance="))
      return false;

   i = 0;
   for (i = 0; *line != ' '; i++, line++)
      ring_name[i] = *line;

   ring_name[i] = 0;
   return true;
}

/*
 * when a topic string is parsed it sets new_topic and returns true, otherwise
 * does nothing.
 */
bool
error_decode_xe_decode_topic(const char *line, enum xe_topic *new_topic)
{
   static const char *xe_topic_strings[] = {
      "**** Xe Device Coredump ****",
      "**** GuC CT ****",
      "**** Job ****",
      "**** HW Engines ****",
      "**** VM state ****",
      "**** Contexts ****",
   };
   const bool topic_changed = strncmp("**** ", line, strlen("**** ")) == 0;

   if (topic_changed)
      *new_topic = XE_TOPIC_UNKNOWN;

   for (int i = 0; i < ARRAY_SIZE(xe_topic_strings); i++) {
      if (strncmp(xe_topic_strings[i], line, strlen(xe_topic_strings[i])) == 0) {
         *new_topic = i;
         break;
      }
   }

   return topic_changed;
}

/* return type of VM state topic lines like 'VM.uapi_flags: 0x1' and points
 * value_ptr to first char of data of topic type
 */
static enum xe_vm_topic_type
error_decode_xe_read_vm_flags_line(const char *line, const char **vm_value_ptr)
{
   enum xe_vm_topic_type type = XE_VM_TOPIC_TYPE_GLOBAL_VM_FLAGS;

   for (; *line != ':'; line++);

   *vm_value_ptr = line + 2;

   return type;
}

/* return type of VM topic lines like '[200000].data: x...' and points
 * value_ptr to first char of data of topic type
 */
enum xe_vm_topic_type
error_decode_xe_read_vm_line(const char *line, uint64_t *address, const char **value_ptr)
{
   enum xe_vm_topic_type type;
   char text_addr[64];
   const char *vm_flags_value_ptr;
   int i;

   if (*line == 'V') {
      type = error_decode_xe_read_vm_flags_line(line, &vm_flags_value_ptr);
      *value_ptr = vm_flags_value_ptr;
      return type;
   }

   if (*line != '[')
      return XE_VM_TOPIC_TYPE_UNKNOWN;

   for (i = 0, line++; *line != ']'; i++, line++)
      text_addr[i] = *line;

   text_addr[i] = 0;
   *address = (uint64_t)strtoull(text_addr, NULL, 16);

   /* at this point line points to last address digit so +3 to point to type */
   line += 2;
   switch (*line) {
   case 'd':
      type = XE_VM_TOPIC_TYPE_DATA;
      break;
   case 'l':
      type = XE_VM_TOPIC_TYPE_LENGTH;
      break;
   case 'p':
      type = XE_VM_TOPIC_TYPE_PROPERTY;
      break;
   case 'e':
      type = XE_VM_TOPIC_TYPE_ERROR;
      break;
   default:
      printf("type char: %c, VM topic is unknown\n", *line);
      return XE_VM_TOPIC_TYPE_UNKNOWN;
   }

   for (; *line != ':'; line++);

   *value_ptr = line + 2;
   return type;
}

/* parses a line: '[40000].properties: read_write|bo|mem_region=0x1|pat_index=0|cpu_caching=1'
 * and populates a struct from the properties being extracted, returns true on success.
 */
bool
error_decode_xe_read_vm_property_line(struct xe_vma_properties *props, const char *line)
{
   enum xe_vma_property_type property_type = XE_VMA_TOPIC_PROPERTY_PERMISSION;

   while (*line != '\0') {
      char property[64], property_value[64];
      int property_len = 0;
      int value_len = 0;

      while (*line != '|' && *line != '=' && *line != '\0') {
         property[property_len++] = *line;
         line++;
      }
      property[property_len] = 0;

      if (*line == '=') {
         line++;
         while (*line != '|' && *line != '\0') {
            property_value[value_len++] = *line;
            line++;
         }
         property_value[value_len] = 0;
      }

      switch (property_type) {
         case XE_VMA_TOPIC_PROPERTY_PERMISSION:
            if (strcmp("read", property) == 0) {
               props->mem_permission = INTEL_HANG_DUMP_BLOCK_MEM_TYPE_READ_ONLY;
            } else if (strcmp("read_write", property) == 0) {
               props->mem_permission = INTEL_HANG_DUMP_BLOCK_MEM_TYPE_READ_WRITE;
            } else {
               printf("Error unknown permission property: %s\n", property);
               return false;
            }
            break;
         case XE_VMA_TOPIC_PROPERTY_TYPE:
            if (strcmp("bo", property) == 0) {
               props->mem_type = INTEL_HANG_DUMP_BLOCK_MEM_TYPE_BO;
            } else if (strcmp("userptr", property) == 0) {
               props->mem_type = INTEL_HANG_DUMP_BLOCK_MEM_TYPE_USERPTR;
            } else if (strcmp("null_sparse", property) == 0) {
               props->mem_type = INTEL_HANG_DUMP_BLOCK_MEM_TYPE_NULL_SPARSE;
            } else {
               printf("Error unknown vma type: %s\n", property);
               return false;
            }
            break;
         case XE_VMA_TOPIC_PROPERTY_MEM_REGION:
            if (strcmp("mem_region", property) != 0) {
               printf("Error: mismatch in VMA property string name %s - expected 'mem_region'\n", property);
               return false;
            }
            props->mem_region = strtoul(property_value, NULL, 0);
            break;
         case XE_VMA_TOPIC_PROPERTY_PAT_INDEX:
            if (strcmp("pat_index", property) != 0) {
               printf("Error: mismatch in VMA property string name: %s - expected 'pat_index'\n", property);
               return false;
            }
            props->pat_index = strtoul(property_value, NULL, 0);
            break;
         case XE_VMA_TOPIC_PROPERTY_CPU_CACHING:
            if (strcmp("cpu_caching", property) != 0) {
               printf("Error: mismatch in VMA property string name: %s - expected 'cpu_caching'\n", property);
               return false;
            }
            props->cpu_caching = strtoul(property_value, NULL, 0);
            if (props->cpu_caching != INTEL_HANG_DUMP_BLOCK_CPU_CACHING_MODE_WB &&
                props->cpu_caching != INTEL_HANG_DUMP_BLOCK_CPU_CACHING_MODE_WC) {
               printf("Error unknown cpu caching: %s\n", property_value);
               return false;
            }
            break;
         default:
            printf("Error unknown VMA property type: %s\n", property);
            return false;
      }

      property_type++;
      if (*line == '|') {
         line++;
      }
   }

   return true;
}

/* return true if line is a binary line.
 * name is set with binary name, type is set with line binary type and
 * value_ptr with line binary value(length, error or data).
 */
bool
error_decode_xe_binary_line(const char *line, char *name, int name_len, enum xe_vm_topic_type *type, const char **value_ptr)
{
   const char *c = line;

   while (*c == '\t' || *c == 0)
      c++;

   if (*c != '[')
      return false;
   c++;

   for (; *c != ']' && (name_len - 1) && *c != 0; c++, name++, name_len--)
      *name = *c;
   *name = 0;

   if (*c != ']' || c[1] != '.')
      return false;
   c += 2;

   switch (*c) {
   case 'd':
      *type = XE_VM_TOPIC_TYPE_DATA;
      break;
   case 'e':
      *type = XE_VM_TOPIC_TYPE_ERROR;
      break;
   case 'l':
      *type = XE_VM_TOPIC_TYPE_LENGTH;
      break;
   case 'r':
      if (strncmp(c, "replay_offset", strlen("replay_offset")) == 0) {
         *type = XE_VM_TOPIC_TYPE_REPLAY_OFFSET;
         break;
      } else if (strncmp(c, "replay_length", strlen("replay_length")) == 0) {
         *type = XE_VM_TOPIC_TYPE_REPLAY_LENGTH;
         break;
      }
      FALLTHROUGH;
   default:
      printf("type char: %c\n", *line);
      return false;
   }

   while (*c != ':' && *c != 0)
      c++;

   if (*c != ':' || c[1] != ' ')
      return false;
   c += 2;

   *value_ptr = c;
   return true;
}

void error_decode_xe_vm_init(struct xe_vm *xe_vm)
{
   xe_vm->entries = NULL;
   xe_vm->entries_len = 0;
   memset(&xe_vm->hw_context, 0, sizeof(xe_vm->hw_context));
}

void error_decode_xe_vm_fini(struct xe_vm *xe_vm)
{
   uint32_t i;

   for (i = 0; i < xe_vm->entries_len; i++)
      free((uint32_t *)xe_vm->entries[i].data);

   free((uint32_t *)xe_vm->hw_context.data);
   free(xe_vm->entries);
}

static void
xe_vm_entry_set(struct xe_vm_entry *entry, const uint64_t address, const uint32_t length,
                const struct xe_vma_properties *props, const uint32_t *data)
{
   /* Newer versions of Xe KMD will give us the canonical VMA address while
    * older will give us 48b address.
    * intel_batch_decoder.c convert addresses to 48b address before calling
    * get_bo() so here converting all VMA addresses to 48b.
    */
   entry->address = intel_48b_address(address);
   entry->length = length;
   entry->data = data;
   memcpy(&entry->props, props, sizeof(struct xe_vma_properties));
}

void
error_decode_xe_vm_hw_ctx_set(struct xe_vm *xe_vm, const uint32_t length,
                              const uint32_t *data)
{
   struct xe_vma_properties props = {0};

   xe_vm_entry_set(&xe_vm->hw_context, 0, length, &props, data);
}

void error_decode_xe_vm_hw_ctx_set_offset(struct xe_vm *xe_vm, uint64_t offset)
{
	xe_vm->hw_context.address = offset;
}

/*
 * error_decode_xe_vm_fini() will take care to free data
 */
bool
error_decode_xe_vm_append(struct xe_vm *xe_vm, const uint64_t address,
                          const uint32_t length,
                          const struct xe_vma_properties *props,
                          const uint32_t *data)
{
   size_t len = sizeof(*xe_vm->entries) * (xe_vm->entries_len + 1);

   xe_vm->entries = realloc(xe_vm->entries, len);

   if (!xe_vm->entries)
      return false;
   xe_vm_entry_set(&xe_vm->entries[xe_vm->entries_len], address, length, props, data);
   xe_vm->entries_len++;
   return true;
}

const struct xe_vm_entry *
error_decode_xe_vm_entry_get(struct xe_vm *xe_vm, const uint64_t address)
{
   uint32_t i;

   for (i = 0; i < xe_vm->entries_len; i++) {
      struct xe_vm_entry *entry = &xe_vm->entries[i];

      if (entry->address == address)
         return entry;

      if (address > entry->address &&
          address < (entry->address + entry->length))
         return entry;
   }

   return NULL;
}

uint32_t *
error_decode_xe_vm_entry_address_get_data(const struct xe_vm_entry *entry,
                                          const uint64_t address)
{
   uint32_t offset = (address - entry->address) / sizeof(uint32_t);
   return (uint32_t *)&entry->data[offset];
}

uint32_t
error_decode_xe_vm_entry_address_get_len(const struct xe_vm_entry *entry,
                                         const uint64_t address)
{
   return entry->length - (address - entry->address);
}

bool
error_decode_xe_ascii85_decode_allocated(const char *in, uint32_t *out, uint32_t vm_entry_bytes_len)
{
   const uint32_t dword_len = vm_entry_bytes_len / sizeof(uint32_t);
   uint32_t i;

   for (i = 0; (*in >= '!') && (*in <= 'z') && (i < dword_len); i++)
      in = ascii85_decode_char(in, &out[i]);

   if (dword_len != i)
      printf("mismatch dword_len=%u i=%u\n", dword_len, i);

   return dword_len == i && (*in < '!' || *in > 'z');
}
