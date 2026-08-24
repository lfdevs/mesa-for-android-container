/*
 * Copyright © 2026 Jan Meisel
 * SPDX-License-Identifier: MIT
 */

#undef NDEBUG

#include "util/hash_table.h"

static void *
key(unsigned i)
{
   return (void *)(uintptr_t)i;
}

static void
fill_table(struct hash_table *ht, unsigned count)
{
   for (unsigned i = 0; i < count; i++)
      _mesa_hash_table_insert(ht, key(i), key(i + count));
}

static void
check_table(struct hash_table *ht, unsigned count, int missing)
{
   assert(ht->entries == count - (missing >= 0));

   for (unsigned i = 0; i < count; i++) {
      struct hash_entry *entry = _mesa_hash_table_search(ht, key(i));
      if (i == missing)
         assert(!entry);
      else
         assert(entry && entry->data == key(i + count));
   }
}

int
main(void)
{
   struct hash_table src, dst;
   _mesa_pointer_hash_table_init(&src, NULL);
   _mesa_pointer_hash_table_init(&dst, NULL);

   /* Equal capacities can copy the backing table directly, including
    * tombstones.
    */
   fill_table(&src, 100);
   assert(_mesa_hash_table_reserve(&dst, src.entries));
   assert(dst.size == src.size);

   _mesa_hash_table_remove_key(&src, key(50));
   assert(src.deleted_entries > 0);

   struct hash_entry *dst_storage = dst.table;
   assert(_mesa_hash_table_clone_into(&dst, &src));
   assert(dst.table == dst_storage);
   assert(dst.deleted_entries == src.deleted_entries);
   check_table(&dst, 100, 50);

   /* A larger destination keeps its allocation and rehashes the entries. */
   assert(_mesa_hash_table_reserve(&dst, 1000));
   dst_storage = dst.table;
   uint32_t dst_size = dst.size;
   assert(dst.size > src.size);
   assert(_mesa_hash_table_clone_into(&dst, &src));
   assert(dst.table == dst_storage);
   assert(dst.size == dst_size);
   assert(dst.deleted_entries == 0);
   check_table(&dst, 100, 50);

   /* Cloning an empty table clears the destination without shrinking it. */
   _mesa_hash_table_clear(&src, NULL);
   assert(_mesa_hash_table_clone_into(&dst, &src));
   assert(dst.table == dst_storage);
   assert(dst.size == dst_size);
   assert(dst.entries == 0);
   assert(dst.deleted_entries == 0);

   _mesa_hash_table_fini(&src, NULL);
   _mesa_hash_table_fini(&dst, NULL);

   /* A smaller destination grows enough to hold all source entries. */
   _mesa_pointer_hash_table_init(&src, NULL);
   _mesa_pointer_hash_table_init(&dst, NULL);
   fill_table(&src, 1000);
   dst_storage = dst.table;
   assert(dst.size < src.size);
   assert(_mesa_hash_table_clone_into(&dst, &src));
   assert(dst.table != dst_storage);
   check_table(&dst, 1000, -1);

   _mesa_hash_table_fini(&src, NULL);
   _mesa_hash_table_fini(&dst, NULL);

   return 0;
}
