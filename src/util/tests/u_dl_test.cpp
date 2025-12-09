/*
 * Copyright 2022 Yonggang Luo
 * SPDX-License-Identifier: MIT
 *
 */

#include <gtest/gtest.h>

#include "util/u_dl.h"

TEST(u_dl_test, util_dl_get_path_from_proc_test)
{
   void *func_name = util_dl_get_path_from_proc((const void *)util_dl_get_path_from_proc);
   bool dl_get_path_implemented =
   #if defined(HAVE_DLADDR)
      true;
   #elif DETECT_OS_WINDOWS
      true;
   #else
      false;
   #endif
   bool func_name_fetched = func_name != NULL;
   free(func_name);
   EXPECT_EQ(func_name_fetched, dl_get_path_implemented);
}
