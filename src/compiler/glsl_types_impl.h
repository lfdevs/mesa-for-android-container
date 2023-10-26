/*
 * Copyright © 2009 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef GLSL_TYPES_IMPL_H
#define GLSL_TYPES_IMPL_H

#ifdef __cplusplus

#define BUILTIN_TYPES_CPP_DEFINITIONS
#include "compiler/builtin_types_cpp.h"
#undef BUILTIN_TYPES_CPP_DEFINITIONS

inline bool glsl_type::is_boolean() const { return glsl_type_is_boolean(this); }
inline bool glsl_type::is_sampler() const { return glsl_type_is_sampler(this); }
inline bool glsl_type::is_texture() const { return glsl_type_is_texture(this); }
inline bool glsl_type::is_image() const { return glsl_type_is_image(this); }
inline bool glsl_type::is_array() const { return glsl_type_is_array(this); }
inline bool glsl_type::is_struct() const { return glsl_type_is_struct(this); }
inline bool glsl_type::is_interface() const { return glsl_type_is_interface(this); }
inline bool glsl_type::is_cmat() const { return glsl_type_is_cmat(this); }
inline bool glsl_type::is_void() const { return glsl_type_is_void(this); }
inline bool glsl_type::is_error() const { return glsl_type_is_error(this); }
inline bool glsl_type::is_subroutine() const { return glsl_type_is_subroutine(this); }
inline bool glsl_type::is_atomic_uint() const { return glsl_type_is_atomic_uint(this); }

inline bool glsl_type::is_scalar() const { return glsl_type_is_scalar(this); }
inline bool glsl_type::is_vector() const { return glsl_type_is_vector(this); }
inline bool glsl_type::is_matrix() const { return glsl_type_is_matrix(this); }
inline bool glsl_type::is_numeric() const { return glsl_type_is_numeric(this); }
inline bool glsl_type::is_integer() const { return glsl_type_is_integer(this); }
inline bool glsl_type::is_double() const { return glsl_type_is_double(this); }

inline bool glsl_type::is_array_of_arrays() const { return glsl_type_is_array_of_arrays(this); }
inline bool glsl_type::is_dual_slot() const { return glsl_type_is_dual_slot(this); }
inline bool glsl_type::is_16bit() const { return glsl_type_is_16bit(this); }
inline bool glsl_type::is_32bit() const { return glsl_type_is_32bit(this); }
inline bool glsl_type::is_64bit() const { return glsl_type_is_64bit(this); }

inline bool glsl_type::contains_64bit() const { return glsl_type_contains_64bit(this); }
inline bool glsl_type::contains_image() const { return glsl_type_contains_image(this); }
inline bool glsl_type::contains_atomic() const { return glsl_contains_atomic(this); }
inline bool glsl_type::contains_opaque() const { return glsl_contains_opaque(this); }
inline bool glsl_type::contains_double() const { return glsl_contains_double(this); }
inline bool glsl_type::contains_integer() const { return glsl_contains_integer(this); }
inline bool glsl_type::contains_sampler() const { return glsl_contains_sampler(this); }
inline bool glsl_type::contains_array() const { return glsl_contains_array(this); }
inline bool glsl_type::contains_subroutine() const { return glsl_contains_subroutine(this); }

inline int glsl_type::array_size() const { return glsl_array_size(this); }
inline const glsl_type *glsl_type::without_array() const { return glsl_without_array(this); }

inline unsigned glsl_type::struct_location_offset(unsigned len) const { return glsl_get_struct_location_offset(this, len); }
inline int glsl_type::field_index(const char *n) const { return glsl_get_field_index(this, n); }

inline enum glsl_interface_packing glsl_type::get_interface_packing() const { return glsl_get_ifc_packing(this); }
inline enum glsl_interface_packing glsl_type::get_internal_ifc_packing(bool std430_supported) const { return glsl_get_internal_ifc_packing(this, std430_supported); }

inline bool glsl_type::compare_no_precision(const glsl_type *b) const { return glsl_type_compare_no_precision(this, b); }
inline bool glsl_type::record_compare(const glsl_type *b, bool match_name, bool match_locations, bool match_precision) const { return glsl_record_compare(this, b, match_name, match_locations, match_precision); }

inline unsigned glsl_type::components() const { return glsl_get_components(this); }
inline unsigned glsl_type::component_slots() const { return glsl_get_component_slots(this); }
inline unsigned glsl_type::component_slots_aligned(unsigned int offset) const { return glsl_get_component_slots_aligned(this, offset); }
inline unsigned glsl_type::count_vec4_slots(bool is_gl_vertex_input, bool bindless) const { return glsl_count_vec4_slots(this, is_gl_vertex_input, bindless); }
inline unsigned glsl_type::count_dword_slots(bool bindless) const { return glsl_count_dword_slots(this, bindless); };
inline unsigned glsl_type::count_attribute_slots(bool is_gl_vertex_input) const { return glsl_count_attribute_slots(this, is_gl_vertex_input); }
inline unsigned glsl_type::varying_count() const { return glsl_varying_count(this); }
inline unsigned glsl_type::atomic_size() const { return glsl_atomic_size(this); }
inline int glsl_type::coordinate_components() const { return glsl_get_sampler_coordinate_components(this); }
inline unsigned glsl_type::uniform_locations() const { return glsl_type_uniform_locations(this); }

inline unsigned glsl_type::cl_size() const { return glsl_get_cl_size(this); }
inline unsigned glsl_type::cl_alignment() const { return glsl_get_cl_alignment(this); }

inline unsigned glsl_type::std140_base_alignment(bool row_major) const { return glsl_get_std140_base_alignment(this, row_major); }
inline unsigned glsl_type::std140_size(bool row_major) const { return glsl_get_std140_size(this, row_major); }
inline unsigned glsl_type::std430_base_alignment(bool row_major) const { return glsl_get_std430_base_alignment(this, row_major); }
inline unsigned glsl_type::std430_array_stride(bool row_major) const { return glsl_get_std430_array_stride(this, row_major); }
inline unsigned glsl_type::std430_size(bool row_major) const { return glsl_get_std430_size(this, row_major); }
inline unsigned glsl_type::explicit_size(bool align_to_stride) const { return glsl_get_explicit_size(this, align_to_stride); }

inline const glsl_type *glsl_type::get_explicit_std140_type(bool row_major) const { return glsl_get_explicit_std140_type(this, row_major); }
inline const glsl_type *glsl_type::get_explicit_std430_type(bool row_major) const { return glsl_get_explicit_std430_type(this, row_major); }
inline const glsl_type *glsl_type::get_explicit_interface_type(bool supports_std430) const { return glsl_get_explicit_interface_type(this, supports_std430); }

inline const glsl_type *glsl_type::row_type() const { return glsl_get_row_type(this); }
inline const glsl_type *glsl_type::column_type() const { return glsl_get_column_type(this); }
inline const glsl_type *glsl_type::get_bare_type() const { return glsl_get_bare_type(this); }
inline const glsl_type *glsl_type::get_base_type() const { return glsl_get_base_glsl_type(this); }
inline const glsl_type *glsl_type::get_scalar_type() const { return glsl_get_scalar_type(this); }

inline const glsl_type *glsl_type::get_float16_type() const { return glsl_float16_type(this); }
inline const glsl_type *glsl_type::get_int16_type() const { return glsl_int16_type(this); }
inline const glsl_type *glsl_type::get_uint16_type() const { return glsl_uint16_type(this); }

inline const glsl_type *glsl_type::vec(unsigned components) { return glsl_vec_type(components); }
inline const glsl_type *glsl_type::f16vec(unsigned components) { return glsl_f16vec_type(components); }
inline const glsl_type *glsl_type::dvec(unsigned components) { return glsl_dvec_type(components); }
inline const glsl_type *glsl_type::ivec(unsigned components) { return glsl_ivec_type(components); }
inline const glsl_type *glsl_type::uvec(unsigned components) { return glsl_uvec_type(components); }
inline const glsl_type *glsl_type::bvec(unsigned components) { return glsl_bvec_type(components); }
inline const glsl_type *glsl_type::i64vec(unsigned components) { return glsl_i64vec_type(components); }
inline const glsl_type *glsl_type::u64vec(unsigned components) { return glsl_u64vec_type(components); }
inline const glsl_type *glsl_type::i16vec(unsigned components) { return glsl_i16vec_type(components); }
inline const glsl_type *glsl_type::u16vec(unsigned components) { return glsl_u16vec_type(components); }
inline const glsl_type *glsl_type::i8vec(unsigned components) { return glsl_i8vec_type(components); }
inline const glsl_type *glsl_type::u8vec(unsigned components) { return glsl_u8vec_type(components); }

inline const glsl_type *
glsl_type::get_instance(unsigned base_type, unsigned rows, unsigned columns,
                        unsigned explicit_stride, bool row_major,
                        unsigned explicit_alignment)
{
   return glsl_simple_explicit_type(base_type, rows, columns, explicit_stride,
                                    row_major, explicit_alignment);
}

inline const glsl_type *
glsl_type::get_sampler_instance(enum glsl_sampler_dim dim, bool shadow,
                                bool array, glsl_base_type type)
{
   return glsl_sampler_type(dim, shadow, array, type);
}

inline const glsl_type *
glsl_type::get_texture_instance(enum glsl_sampler_dim dim, bool array, glsl_base_type type)
{
   return glsl_texture_type(dim, array, type);
}

inline const glsl_type *
glsl_type::get_image_instance(enum glsl_sampler_dim dim, bool array, glsl_base_type type)
{
   return glsl_image_type(dim, array, type);
}

inline const glsl_type *
glsl_type::get_array_instance(const glsl_type *element, unsigned array_size,
                              unsigned explicit_stride)
{
   return glsl_array_type(element, array_size, explicit_stride);
}

inline const glsl_type *
glsl_type::get_cmat_instance(const struct glsl_cmat_description desc)
{
   return glsl_cmat_type(&desc);
}

inline const glsl_type *
glsl_type::get_struct_instance(const glsl_struct_field *fields, unsigned num_fields,
                               const char *name, bool packed, unsigned explicit_alignment)
{
   return glsl_struct_type_with_explicit_alignment(fields, num_fields, name, packed, explicit_alignment);
}

inline const glsl_type *
glsl_type::get_interface_instance(const glsl_struct_field *fields, unsigned num_fields,
                                  enum glsl_interface_packing packing,
                                  bool row_major, const char *block_name)
{
   return glsl_interface_type(fields, num_fields, packing, row_major, block_name);
}

inline const glsl_type *
glsl_type::get_subroutine_instance(const char *subroutine_name)
{
   return glsl_subroutine_type(subroutine_name);
}

inline const glsl_type *
glsl_type::get_explicit_type_for_size_align(glsl_type_size_align_func type_info,
                                            unsigned *size, unsigned *align) const
{
   return glsl_get_explicit_type_for_size_align(this, type_info, size, align);
}

inline const glsl_type *glsl_type::replace_vec3_with_vec4() const { return glsl_type_replace_vec3_with_vec4(this); }
inline const glsl_type *glsl_type::get_mul_type(const glsl_type *type_a, const glsl_type *type_b) { return glsl_get_mul_type(type_a, type_b); }

inline const glsl_type *
glsl_type::field_type(const char *n) const
{
   const int idx = glsl_get_field_index(this, n);
   if (idx == -1)
      return &glsl_type_builtin_error;
   return glsl_get_struct_field(this, (unsigned)idx);
}

inline bool
glsl_type::is_integer_16() const
{
   return base_type == GLSL_TYPE_UINT16 || base_type == GLSL_TYPE_INT16;
}

inline bool
glsl_type::is_integer_32() const
{
   return (base_type == GLSL_TYPE_UINT) || (base_type == GLSL_TYPE_INT);
}

inline bool
glsl_type::is_integer_64() const
{
   return base_type == GLSL_TYPE_UINT64 || base_type == GLSL_TYPE_INT64;
}

inline bool
glsl_type::is_integer_32_64() const
{
   return is_integer_32() || is_integer_64();
}

inline bool
glsl_type::is_integer_16_32() const
{
   return is_integer_16() || is_integer_32();
}

inline bool
glsl_type::is_integer_16_32_64() const
{
   return is_integer_16() || is_integer_32() || is_integer_64();
}

inline bool
glsl_type::is_float() const
{
   return base_type == GLSL_TYPE_FLOAT;
}

inline bool
glsl_type::is_float_16_32() const
{
   return base_type == GLSL_TYPE_FLOAT16 || is_float();
}

inline bool
glsl_type::is_float_16_32_64() const
{
   return base_type == GLSL_TYPE_FLOAT16 || is_float() || is_double();
}

inline bool
glsl_type::is_float_32_64() const
{
   return is_float() || is_double();
}

inline bool
glsl_type::is_int_16_32_64() const
{
   return base_type == GLSL_TYPE_INT16 ||
          base_type == GLSL_TYPE_INT ||
          base_type == GLSL_TYPE_INT64;
}

inline bool
glsl_type::is_uint_16_32_64() const
{
   return base_type == GLSL_TYPE_UINT16 ||
          base_type == GLSL_TYPE_UINT ||
          base_type == GLSL_TYPE_UINT64;
}

inline bool
glsl_type::is_int_16_32() const
{
   return base_type == GLSL_TYPE_INT ||
          base_type == GLSL_TYPE_INT16;
}

inline bool
glsl_type::is_uint_16_32() const
{
   return base_type == GLSL_TYPE_UINT ||
          base_type == GLSL_TYPE_UINT16;
}

inline bool
glsl_type::is_anonymous() const
{
   return !strncmp(glsl_get_type_name(this), "#anon", 5);
}

inline unsigned glsl_type::arrays_of_arrays_size() const { return glsl_get_aoa_size(this); }

inline unsigned
glsl_type::bit_size() const
{
   return glsl_base_type_bit_size(this->base_type);
}

inline bool glsl_type::is_unsized_array() const { return glsl_type_is_unsized_array(this); }

inline bool
glsl_type::get_interface_row_major() const
{
   return (bool) interface_row_major;
}

#endif /* __cplusplus */

#endif /* GLSL_TYPES_H */
