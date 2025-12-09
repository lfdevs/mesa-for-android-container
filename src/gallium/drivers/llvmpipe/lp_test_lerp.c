/*
 * Copyright 2025 Autodesk, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "util/u_memory.h"

#include "gallivm/lp_bld.h"
#include "gallivm/lp_bld_init.h"
#include "gallivm/lp_bld_arit.h"

#include "lp_test.h"


void
write_tsv_header(FILE *fp)
{
}

static LLVMValueRef
build_lerp_test_func(struct gallivm_state *gallivm,
                     struct lp_type type,
                     unsigned flags,
                     const char *test_name)
{
   LLVMContextRef context = gallivm->context;
   LLVMModuleRef module = gallivm->module;
   LLVMTypeRef vec_type = lp_build_vec_type(gallivm, type);
   LLVMTypeRef args[] = {
      LLVMPointerType(vec_type, 0),
      LLVMPointerType(vec_type, 0),
      LLVMPointerType(vec_type, 0),
      LLVMPointerType(vec_type, 0),
   };
   LLVMValueRef func = LLVMAddFunction(module, test_name,
      LLVMFunctionType(LLVMVoidTypeInContext(context),
         args, ARRAY_SIZE(args), 0));
   LLVMValueRef out_arg = LLVMGetParam(func, 0);
   LLVMValueRef x_arg = LLVMGetParam(func, 1);
   LLVMValueRef v0_arg = LLVMGetParam(func, 2);
   LLVMValueRef v1_arg = LLVMGetParam(func, 3);

   LLVMBuilderRef builder = gallivm->builder;
   LLVMBasicBlockRef block = LLVMAppendBasicBlockInContext(context, func, "entry");

   struct lp_build_context bld;
   lp_build_context_init(&bld, gallivm, type);
   LLVMSetFunctionCallConv(func, LLVMCCallConv);

   LLVMPositionBuilderAtEnd(builder, block);

   x_arg = LLVMBuildLoad2(builder, vec_type, x_arg, "x");
   v0_arg = LLVMBuildLoad2(builder, vec_type, v0_arg, "v0");
   v1_arg = LLVMBuildLoad2(builder, vec_type, v1_arg, "v1");
   LLVMValueRef ret = lp_build_lerp(&bld, x_arg, v0_arg, v1_arg, flags);
   LLVMBuildStore(builder, ret, out_arg);

   LLVMBuildRetVoid(builder);

   gallivm_verify_function(gallivm, func);

   return func;
}

typedef void (*lerp_func_u8)(uint8_t *out, const uint8_t *x, const uint8_t *v0, const uint8_t *v1);
typedef void (*lerp_func_s8)(int8_t *out, const int8_t *x, const int8_t *v0, const int8_t *v1);
typedef void (*lerp_func_u16)(uint16_t *out, const uint16_t *x, const uint16_t *v0, const uint16_t *v1);
typedef void (*lerp_func_s16)(int16_t *out, const int16_t *x, const int16_t *v0, const int16_t *v1);
typedef void (*lerp_func_u32)(uint32_t *out, const uint32_t *x, const uint32_t *v0, const uint32_t *v1);
typedef void (*lerp_func_s32)(int32_t *out, const int32_t *x, const int32_t *v0, const int32_t *v1);
typedef void (*lerp_func_fp32)(float *out, const float *x, const float *v0, const float *v1);

static bool
test_lerp(struct lp_type type, unsigned flags, const void *x, const void *v0,
   const void *v1, const void *expected, void* out)
{
   const char *type_name;
   if (type.floating) {
      type_name = "fp";
   } if (type.norm) {
      type_name = type.sign ? "snorm" : "unorm";
   } else if (type.fixed) {
      type_name = type.sign ? "sfix" : "ufix";
   } else {
      exit(1);
   }
   char test_name[128];
   snprintf(test_name, ARRAY_SIZE(test_name), "lerp.v%u%s%u", type.length, type_name, type.width);

   lp_context_ref context;
   lp_context_create(&context);
   struct gallivm_state *gallivm = gallivm_create("test_module", &context, NULL);

   LLVMValueRef test_func = build_lerp_test_func(gallivm, type, flags, test_name);

   gallivm_compile_module(gallivm);

   func_pointer test_func_jit = gallivm_jit_function(gallivm, test_func, test_name);

   gallivm_free_ir(gallivm);

   if (type.floating) {
      if (type.width == 32) {
         ((lerp_func_fp32)test_func_jit)(out, x, v0, v1);
      } else {
         exit(1);
      }
   } else {
      switch (type.width) {
      case 8:
         if (type.sign) {
            ((lerp_func_s8)test_func_jit)(out, x, v0, v1);
         } else {
            ((lerp_func_u8)test_func_jit)(out, x, v0, v1);
         }
         break;
      case 16:
         if (type.sign) {
            ((lerp_func_s16)test_func_jit)(out, x, v0, v1);
         } else {
            ((lerp_func_u16)test_func_jit)(out, x, v0, v1);
         }
         break;
      case 32:
         if (type.sign) {
            ((lerp_func_s32)test_func_jit)(out, x, v0, v1);
         } else {
            ((lerp_func_u32)test_func_jit)(out, x, v0, v1);
         }
         break;
      default:
         exit(1);
      }
   }

   gallivm_destroy(gallivm);
   lp_context_destroy(&context);

   return memcmp(out, expected, type.length * (type.width / 8)) == 0;
}

#define test_lerp_type(type_name, type_format, type_expr, flags, x, v0, v1, expected) do { \
   const struct lp_type type = (type_expr); \
   struct lp_type native_type = type; \
   native_type.length = lp_native_vector_width / type.width; \
   const size_t total_size = native_type.length * (native_type.width / 8); \
   void *vector_x = align_malloc(total_size, total_size); \
   void *vector_v0 = align_malloc(total_size, total_size); \
   void *vector_v1 = align_malloc(total_size, total_size); \
   void *vector_expected = align_malloc(total_size, total_size); \
   void *vector_out = align_malloc(total_size, total_size); \
   for (size_t i = 0; i < native_type.length; i++) { \
      const size_t j = i % type.length;\
      ((type_name*)vector_x)[i] = (x); \
      ((type_name*)vector_v0)[i] = (v0)[j]; \
      ((type_name*)vector_v1)[i] = (v1)[j]; \
      ((type_name*)vector_expected)[i] = (expected)[j]; \
      ((type_name*)vector_out)[i] = 0; \
   } \
   const bool pass = test_lerp(native_type, (flags), vector_x, vector_v0, vector_v1, vector_expected, vector_out); \
   success &= pass; \
   if (!pass || verbose) { \
      for (size_t i = 0; i < native_type.length; i++) { \
         printf("lerp(" type_format ", " type_format ", " type_format ") = " type_format " (expected " type_format ")\n", \
            ((type_name*)vector_x)[i], ((type_name*)vector_v0)[i], ((type_name*)vector_v1)[i], \
            ((type_name*)vector_out)[i], ((type_name*)vector_expected)[i]); \
      } \
      printf("\n"); \
   } \
   align_free(vector_x); \
   align_free(vector_v0); \
   align_free(vector_v1); \
   align_free(vector_expected); \
   align_free(vector_out); \
} while (0)


bool
test_all(unsigned verbose, FILE *fp)
{
   /* This test focuses on verifying the edge cases: half-way (or near half-way
    * rounding if 0.5 isn't exactly representable) and extrema values.
    */
   bool success = true;

   /* Half way rounding of scaled normalized values (x / 2^n).
    *   roundeven(+1 * 0.5) = 0
    *   roundeven(-1 * 0.5) = 0
    * So v0 is always returned.
    */
   test_lerp_type(uint8_t, "%hhu", ((struct lp_type){
      .width = 8, .length = 4, .norm = 1,
   }), LP_BLD_LERP_PRESCALED_WEIGHTS, 1 << 7,
      ((uint8_t[]){0, 83, 86, 0xff}),
      ((uint8_t[]){0, 84, 85, 0xff}),
      ((uint8_t[]){0, 83, 86, 0xff}));
   test_lerp_type(uint16_t, "%hu", ((struct lp_type){
      .width = 16, .length = 4, .norm = 1,
   }), LP_BLD_LERP_PRESCALED_WEIGHTS, 1 << 15,
      ((uint16_t[]){0, 83, 86, 0xffff}),
      ((uint16_t[]){0, 84, 85, 0xffff}),
      ((uint16_t[]){0, 83, 86, 0xffff}));
   test_lerp_type(uint32_t, "%u", ((struct lp_type){
      .width = 32, .length = 4, .norm = 1,
   }), LP_BLD_LERP_PRESCALED_WEIGHTS, 1 << 31,
      ((uint32_t[]){0, 83, 86, 0xffffffff}),
      ((uint32_t[]){0, 84, 85, 0xffffffff}),
      ((uint32_t[]){0, 83, 86, 0xffffffff}));

   /* "Just over" half way rounding of unsigned normalized values (x / 2^n - 1).
    *   roundeven(+1 * nextval(0.5)) = 1
    *   roundeven(-1 * nextval(0.5)) = -1
    * So v1 is always returned.
    */
   test_lerp_type(uint8_t, "%hhu", ((struct lp_type){
      .width = 8, .length = 4, .norm = 1,
   }), 0, 1 << 7,
      ((uint8_t[]){0, 83, 86, 0xff}),
      ((uint8_t[]){0, 84, 85, 0xff}),
      ((uint8_t[]){0, 84, 85, 0xff}));
   test_lerp_type(uint16_t, "%hu", ((struct lp_type){
      .width = 16, .length = 4, .norm = 1,
   }), 0, 1 << 15,
      ((uint16_t[]){0, 83, 86, 0xffff}),
      ((uint16_t[]){0, 84, 85, 0xffff}),
      ((uint16_t[]){0, 84, 85, 0xffff}));
   test_lerp_type(uint32_t, "%u", ((struct lp_type){
      .width = 32, .length = 4, .norm = 1,
   }), 0, 1 << 31,
      ((uint32_t[]){0, 83, 86, 0xffffffff}),
      ((uint32_t[]){0, 84, 85, 0xffffffff}),
      ((uint32_t[]){0, 84, 85, 0xffffffff}));

   /* "Just under" half way rounding of unsigned normalized values (x / 2^n - 1).
    *   roundeven(+1 * prevval(0.5)) = 0
    *   roundeven(-1 * prevval(0.5)) = 0
    * So v0 is always returned.
    */
   test_lerp_type(uint8_t,"%hhd",  ((struct lp_type){
      .width = 8, .length = 4, .norm = 1,
   }), 0, (1 << 7) - 1,
      ((uint8_t[]){0, 83, 86, 0xff}),
      ((uint8_t[]){0, 84, 85, 0xff}),
      ((uint8_t[]){0, 83, 86, 0xff}));
   test_lerp_type(uint16_t, "%hd", ((struct lp_type){
      .width = 16, .length = 4, .norm = 1,
   }), 0, (1 << 14) - 1,
      ((uint16_t[]){0, 83, 86, 0xffff}),
      ((uint16_t[]){0, 84, 85, 0xffff}),
      ((uint16_t[]){0, 83, 86, 0xffff}));
   test_lerp_type(uint32_t, "%d", ((struct lp_type){
      .width = 32, .length = 4, .norm = 1,
   }), 0, (1 << 30) - 1,
      ((uint32_t[]){0, 83, 86, 0xffffffff}),
      ((uint32_t[]){0, 84, 85, 0xffffffff}),
      ((uint32_t[]){0, 83, 86, 0xffffffff}));

   /* "Just over" half way rounding of signed normalized values (x / 2^(n-1) - 1)
    *   roundeven(+1 * nextval(0.5)) = 1
    *   roundeven(-1 * nextval(0.5)) = -1
    * So v1 is always returned
    */
   test_lerp_type(int8_t,"%hhd",  ((struct lp_type){
      .width = 8, .length = 4, .norm = 1, .sign = 1,
   }), 0, 1 << 6,
      ((int8_t[]){0, 83, 86, 0xff}),
      ((int8_t[]){0, 84, 85, 0xff}),
      ((int8_t[]){0, 84, 85, 0xff}));
   test_lerp_type(int16_t, "%hd", ((struct lp_type){
      .width = 16, .length = 4, .norm = 1, .sign = 1,
   }), 0, 1 << 14,
      ((int16_t[]){0, 83, 86, 0xffff}),
      ((int16_t[]){0, 84, 85, 0xffff}),
      ((int16_t[]){0, 84, 85, 0xffff}));
   test_lerp_type(int32_t, "%d", ((struct lp_type){
      .width = 32, .length = 4, .norm = 1, .sign = 1,
   }), 0, 1 << 30,
      ((int32_t[]){0, 83, 86, 0xffffffff}),
      ((int32_t[]){0, 84, 85, 0xffffffff}),
      ((int32_t[]){0, 84, 85, 0xffffffff}));

   /* "Just under" half way rounding of signed normalized values (x / 2^(n-1) - 1).
    *   roundeven(+1 * prevval(0.5)) = 0
    *   roundeven(-1 * prevval(0.5)) = 0
    * So v0 is always returned.
    */
   test_lerp_type(int8_t,"%hhd",  ((struct lp_type){
      .width = 8, .length = 4, .norm = 1, .sign = 1,
   }), 0, (1 << 6) - 1,
      ((int8_t[]){0, 83, 86, 0xff}),
      ((int8_t[]){0, 84, 85, 0xff}),
      ((int8_t[]){0, 83, 86, 0xff}));
   test_lerp_type(int16_t, "%hd", ((struct lp_type){
      .width = 16, .length = 4, .norm = 1, .sign = 1,
   }), 0, (1 << 14) - 1,
      ((int16_t[]){0, 83, 86, 0xffff}),
      ((int16_t[]){0, 84, 85, 0xffff}),
      ((int16_t[]){0, 83, 86, 0xffff}));
   test_lerp_type(int32_t, "%d", ((struct lp_type){
      .width = 32, .length = 4, .norm = 1, .sign = 1,
   }), 0, (1 << 30) - 1,
      ((int32_t[]){0, 83, 86, 0xffffffff}),
      ((int32_t[]){0, 84, 85, 0xffffffff}),
      ((int32_t[]){0, 83, 86, 0xffffffff}));

   /* Half way rounding of unsigned fixed point values (x / 2^(n/2)).
    *   roundeven(+1 * 0.5) = 0
    *   roundeven(-1 * 0.5) = 0
    * So v0 is always returned.
    * Fixed point requires twice the bits, so we don't test 32 bit.
    */
   test_lerp_type(uint16_t, "%hu", ((struct lp_type){
      .width = 16, .length = 4, .fixed = 1,
   }), 0, 1 << 3,
      ((uint16_t[]){0, 83, 86, 0xff}),
      ((uint16_t[]){0, 84, 85, 0xff}),
      ((uint16_t[]){0, 83, 86, 0xff}));
   test_lerp_type(uint32_t, "%u", ((struct lp_type){
      .width = 32, .length = 4, .fixed = 1,
   }), 0, 1 << 7,
      ((uint32_t[]){0, 83, 86, 0xffff}),
      ((uint32_t[]){0, 84, 85, 0xffff}),
      ((uint32_t[]){0, 83, 86, 0xffff}));


   /* Half way rounding of signed fixed point values (x / 2^(n/2)).
    *   roundeven(+1 * 0.5) = 0
    *   roundeven(-1 * 0.5) = 0
    * So v0 is always returned.
    * Fixed point requires twice the bits, so we don't test 32 bit.
    */
   test_lerp_type(uint16_t, "%hu", ((struct lp_type){
      .width = 16, .length = 4, .fixed = 1, .sign = 1,
   }), 0, 1 << 3,
      ((uint16_t[]){0, 83, 86, 0xff}),
      ((uint16_t[]){0, 84, 85, 0xff}),
      ((uint16_t[]){0, 83, 86, 0xff}));
   test_lerp_type(uint32_t, "%u", ((struct lp_type){
      .width = 32, .length = 4, .fixed = 1, .sign = 1,
   }), 0, 1 << 7,
      ((uint32_t[]){0, 83, 86, 0xffff}),
      ((uint32_t[]){0, 84, 85, 0xffff}),
      ((uint32_t[]){0, 83, 86, 0xffff}));

   return success;
}


bool
test_some(unsigned verbose, FILE *fp,
          unsigned long n)
{
   /*
    * Not randomly generated test cases, so test all.
    */
   return test_all(verbose, fp);
}


bool
test_single(unsigned verbose, FILE *fp)
{
   return true;
}
