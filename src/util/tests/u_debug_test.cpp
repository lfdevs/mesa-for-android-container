/*
 * Copyright © 2022 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * \author Illia Abernikhin <illia.abernikhin@globallogic.com>
 *
 */

#include <gtest/gtest.h>
#include "util/u_debug.h"

/* When testing, the environment variable name should not be the same */

TEST(u_debug, debug_get_bool_option)
{
   {
      static char env_str[] = "MESA_UNIT_TEST_BOOL_VARIABLE_0=10";
      putenv(env_str);
      EXPECT_TRUE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_0", true));
      EXPECT_FALSE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_0", false));
   }

   {
      static char env_str[] = "MESA_UNIT_TEST_BOOL_VARIABLE_1";
      putenv(env_str);
      EXPECT_TRUE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_1", true));
      EXPECT_FALSE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_1", false));
   }

   {
      static char env_str[] = "MESA_UNIT_TEST_BOOL_VARIABLE_2=INVALID";
      putenv(env_str);
      EXPECT_TRUE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_2", true));
      EXPECT_FALSE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_2", false));
   }

   {
      static char env_str[] = "MESA_UNIT_TEST_BOOL_VARIABLE_3=0";
      putenv(env_str);
      EXPECT_FALSE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_3", true));
      EXPECT_FALSE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_3", false));
   }

   {
      static char env_str[] = "MESA_UNIT_TEST_BOOL_VARIABLE_4=n";
      putenv(env_str);
      EXPECT_FALSE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_4", true));
      EXPECT_FALSE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_4", false));
   }

   {
      static char env_str[] = "MESA_UNIT_TEST_BOOL_VARIABLE_5=No";
      putenv(env_str);
      EXPECT_FALSE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_5", true));
      EXPECT_FALSE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_5", false));
   }

   {
      static char env_str[] = "MESA_UNIT_TEST_BOOL_VARIABLE_6=F";
      putenv(env_str);
      EXPECT_FALSE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_6", true));
      EXPECT_FALSE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_6", false));
   }

   {
      static char env_str[] = "MESA_UNIT_TEST_BOOL_VARIABLE_7=fAlse";
      putenv(env_str);
      EXPECT_FALSE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_7", true));
      EXPECT_FALSE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_7", false));
   }

   {
      static char env_str[] = "MESA_UNIT_TEST_BOOL_VARIABLE_8=1";
      putenv(env_str);
      EXPECT_TRUE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_8", true));
      EXPECT_TRUE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_8", false));
   }

   {
      static char env_str[] = "MESA_UNIT_TEST_BOOL_VARIABLE_9=Y";
      putenv(env_str);
      EXPECT_TRUE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_9", true));
      EXPECT_TRUE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_9", false));
   }

   {
      static char env_str[] = "MESA_UNIT_TEST_BOOL_VARIABLE_10=Yes";
      putenv(env_str);
      EXPECT_TRUE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_10", true));
      EXPECT_TRUE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_10", false));
   }

   {
      static char env_str[] = "MESA_UNIT_TEST_BOOL_VARIABLE_11=t";
      putenv(env_str);
      EXPECT_TRUE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_11", true));
      EXPECT_TRUE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_11", false));
   }

   {
      static char env_str[] = "MESA_UNIT_TEST_BOOL_VARIABLE_12=True";
      putenv(env_str);
      EXPECT_TRUE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_12", true));
      EXPECT_TRUE(debug_get_bool_option("MESA_UNIT_TEST_BOOL_VARIABLE_12", false));
   }
}

TEST(u_debug, debug_get_num_option)
{
   {
      static char env_str[] = "MESA_UNIT_TEST_NUM_VARIABLE_0=101";
      putenv(env_str);
      EXPECT_EQ(debug_get_num_option("MESA_UNIT_TEST_NUM_VARIABLE_0", 10), 101);
      EXPECT_EQ(debug_get_num_option("MESA_UNIT_TEST_NUM_VARIABLE_0", 0), 101);
   }

   {
      static char env_str[] = "MESA_UNIT_TEST_NUM_VARIABLE_1";
      putenv(env_str);
      EXPECT_EQ(debug_get_num_option("MESA_UNIT_TEST_NUM_VARIABLE_1", 10), 10);
      EXPECT_EQ(debug_get_num_option("MESA_UNIT_TEST_NUM_VARIABLE_1", 100), 100);
   }

   {
      static char env_str[] = "MESA_UNIT_TEST_NUM_VARIABLE_2=something";
      putenv(env_str);
      EXPECT_EQ(debug_get_num_option("MESA_UNIT_TEST_NUM_VARIABLE_2", 10), 10);
      EXPECT_EQ(debug_get_num_option("MESA_UNIT_TEST_NUM_VARIABLE_2", 100), 100);
   }
}
