/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "common/intel_hang_dump.h"

enum xe_topic {
   XE_TOPIC_DEVICE = 0,
   XE_TOPIC_GUC_CT,
   XE_TOPIC_JOB,
   XE_TOPIC_HW_ENGINES,
   XE_TOPIC_VM,
   XE_TOPIC_CONTEXT,
   XE_TOPIC_UNKNOWN,
};

enum xe_vm_topic_type {
   XE_VM_TOPIC_TYPE_UNKNOWN = 0,
   XE_VM_TOPIC_TYPE_GLOBAL_VM_FLAGS,
   XE_VM_TOPIC_TYPE_LENGTH,
   XE_VM_TOPIC_TYPE_DATA,
   XE_VM_TOPIC_TYPE_PROPERTY,
   XE_VM_TOPIC_TYPE_REPLAY_OFFSET,
   XE_VM_TOPIC_TYPE_REPLAY_LENGTH,
   XE_VM_TOPIC_TYPE_ERROR,
};

enum xe_vma_property_type {
   XE_VMA_TOPIC_PROPERTY_PERMISSION = 0,
   XE_VMA_TOPIC_PROPERTY_TYPE,
   XE_VMA_TOPIC_PROPERTY_MEM_REGION,
   XE_VMA_TOPIC_PROPERTY_PAT_INDEX,
   XE_VMA_TOPIC_PROPERTY_CPU_CACHING,
   XE_VMA_TOPIC_PROPERTY_UNKNOWN,
   XE_VMA_TOPIC_PROPERTY_ERROR,
};

struct xe_vma_properties {
   uint32_t mem_permission;
   uint32_t mem_type;
   uint32_t mem_region;
   uint32_t pat_index;
   uint32_t cpu_caching;
};

struct xe_vm_entry {
   uint64_t address;
   uint32_t length;
   const uint32_t *data;
   struct xe_vma_properties props;
};

struct xe_vm {
   /* TODO: entries could be appended sorted or a hash could be used to
    * optimize performance
    */
   struct xe_vm_entry *entries;
   uint32_t entries_len;

   struct xe_vm_entry hw_context;
};

bool error_decode_xe_read_u64_hexacimal_parameter(const char *line, const char *parameter, uint64_t *value);
bool error_decode_xe_read_hexacimal_parameter(const char *line, const char *parameter, uint32_t *value);
bool error_decode_xe_read_engine_name(const char *line, char *ring_name);

bool error_decode_xe_decode_topic(const char *line, enum xe_topic *new_topic);

enum xe_vm_topic_type error_decode_xe_read_vm_line(const char *line, uint64_t *address, const char **value_ptr);
bool error_decode_xe_read_vm_property_line(struct xe_vma_properties *props, const char *line);
bool error_decode_xe_binary_line(const char *line, char *name, int name_len, enum xe_vm_topic_type *type, const char **value_ptr);

void error_decode_xe_vm_init(struct xe_vm *xe_vm);
void error_decode_xe_vm_fini(struct xe_vm *xe_vm);
void error_decode_xe_vm_hw_ctx_set(struct xe_vm *xe_vm, const uint32_t length, const uint32_t *data);
void error_decode_xe_vm_hw_ctx_set_offset(struct xe_vm *xe_vm, uint64_t offset);
bool error_decode_xe_vm_append(struct xe_vm *xe_vm, const uint64_t address, const uint32_t length,
                               const struct xe_vma_properties *props, const uint32_t *data);
const struct xe_vm_entry *error_decode_xe_vm_entry_get(struct xe_vm *xe_vm, const uint64_t address);
uint32_t *error_decode_xe_vm_entry_address_get_data(const struct xe_vm_entry *entry, const uint64_t address);
uint32_t error_decode_xe_vm_entry_address_get_len(const struct xe_vm_entry *entry, const uint64_t address);

bool error_decode_xe_ascii85_decode_allocated(const char *in, uint32_t *out, uint32_t vm_entry_bytes_len);
