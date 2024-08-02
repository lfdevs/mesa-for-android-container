/*
 * Copyright © 2020 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef ACO_TEST_COMMON_H
#define ACO_TEST_COMMON_H
#include "aco_builder.h"
#include "aco_ir.h"

#include "util/macros.h"

#include "ac_shader_util.h"
#include "amd_family.h"
#include <map>
#include <stdio.h>
#include <string>

struct TestDef {
   const char* name;
   const char* source_file;
   void (*func)();
};

extern std::map<std::string, TestDef> *tests;
extern FILE* output;

bool set_variant(const char* name);

inline bool
set_variant(amd_gfx_level cls, const char* rest = "")
{
   char buf[8 + strlen(rest)];
   if (cls == GFX10_3) {
      snprintf(buf, sizeof(buf), "gfx10_3%s", rest);
   } else if (cls == GFX11_5) {
      snprintf(buf, sizeof(buf), "gfx11_5%s", rest);
   } else {
      unsigned num = cls - GFX6 + 6;
      num -= (cls > GFX10_3) + (cls > GFX11_5);
      snprintf(buf, sizeof(buf), "gfx%d%s", num, rest);
   }
   return set_variant(buf);
}

void fail_test(const char* fmt, ...);
void skip_test(const char* fmt, ...);

#define _BEGIN_TEST(name, struct_name)                                                             \
   static void struct_name();                                                                      \
   static __attribute__((constructor)) void CONCAT2(add_test_, __COUNTER__)()                      \
   {                                                                                               \
      if (!tests)                                                                                  \
         tests = new std::map<std::string, TestDef>;                                               \
      (*tests)[#name] = (TestDef){#name, ACO_TEST_BUILD_ROOT "/" __FILE__, &struct_name};          \
   }                                                                                               \
   static void struct_name()                                                                       \
   {

#define BEGIN_TEST(name)      _BEGIN_TEST(name, CONCAT2(Test_, __COUNTER__))
#define BEGIN_TEST_TODO(name) _BEGIN_TEST(name, CONCAT2(Test_, __COUNTER__))
#define BEGIN_TEST_FAIL(name) _BEGIN_TEST(name, CONCAT2(Test_, __COUNTER__))
#define END_TEST              }

#endif /* ACO_TEST_COMMON_H */
