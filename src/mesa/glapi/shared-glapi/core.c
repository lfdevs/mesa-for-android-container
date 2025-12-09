/* Copyright (C) 2010 LunarG Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#include "glapi/glapi_priv.h"

struct mapi_stub {
   size_t name_offset;
   int slot;
};

/* Implemented in mesa/main/context.c. */
void
_mesa_noop_entrypoint(const char *name);

#define MAPI_TMP_NOOP_ARRAY
#define MAPI_TMP_PUBLIC_STUBS
#include "shared_glapi_mapi_tmp.h"

#if defined(_GLAPI_ENTRY_ARCH_TLS_H)
#include _GLAPI_ENTRY_ARCH_TLS_H
#define MAPI_TMP_STUB_ASM_GCC
#include "shared_glapi_mapi_tmp.h"

#if DETECT_ARCH_X86

#ifndef GLX_X86_READONLY_TEXT
__asm__(".balign 16\n"
        "x86_entry_end:");
__asm__(".text");
#endif /* GLX_X86_READONLY_TEXT */

extern unsigned long
x86_current_tls();

extern char x86_entry_start[] HIDDEN;
extern char x86_entry_end[] HIDDEN;

static inline _glapi_proc
entry_generate_or_patch(int, char *, size_t);

static void
entry_patch_public(void)
{
#ifndef GLX_X86_READONLY_TEXT
   char *entry;
   int slot = 0;
   for (entry = x86_entry_start; entry < x86_entry_end;
        entry += X86_ENTRY_SIZE, ++slot)
      entry_generate_or_patch(slot, entry, X86_ENTRY_SIZE);
#endif
}

static _glapi_proc
entry_get_public(int slot)
{
   return (_glapi_proc) (x86_entry_start + slot * X86_ENTRY_SIZE);
}

static void
entry_patch(_glapi_proc entry, int slot)
{
   char *code = (char *) entry;
   *((unsigned long *) (code + 8)) = slot * sizeof(_glapi_proc);
}

static inline _glapi_proc
entry_generate_or_patch(int slot, char *code, size_t size)
{
   const char code_templ[16] = {
      0x65, 0xa1, 0x00, 0x00, 0x00, 0x00, /* movl %gs:0x0, %eax */
      0xff, 0xa0, 0x34, 0x12, 0x00, 0x00, /* jmp *0x1234(%eax) */
      0x90, 0x90, 0x90, 0x90              /* nop's */
   };
   _glapi_proc entry;

   if (size < sizeof(code_templ))
      return NULL;

   memcpy(code, code_templ, sizeof(code_templ));

   *((unsigned long *) (code + 2)) = x86_current_tls();
   entry = (_glapi_proc) code;
   entry_patch(entry, slot);

   return entry;
}

#elif DETECT_ARCH_X86_64

static void
entry_patch_public(void)
{
}

extern char
x86_64_entry_start[] HIDDEN;

static _glapi_proc
entry_get_public(int slot)
{
   return (_glapi_proc) (x86_64_entry_start + slot * 32);
}

#elif DETECT_ARCH_PPC_64 && UTIL_ARCH_LITTLE_ENDIAN

static void
entry_patch_public(void)
{
}

extern char
ppc64le_entry_start[] HIDDEN;

static _glapi_proc
entry_get_public(int slot)
{
   return (_glapi_proc) (ppc64le_entry_start + slot * PPC64LE_ENTRY_SIZE);
}

#else

#error "Unsupported architecture for:" _GLAPI_ENTRY_ARCH_TLS_H

#endif

#else /* !defined(_GLAPI_ENTRY_ARCH_TLS_H) */

/* C version of the public entries */
#define MAPI_TMP_DEFINES
#define MAPI_TMP_PUBLIC_ENTRIES
#include "shared_glapi_mapi_tmp.h"

static void
entry_patch_public(void)
{
}

static _glapi_proc
entry_get_public(int slot)
{
   return public_entries[slot];
}

#endif /* defined(_GLAPI_ENTRY_ARCH_TLS_H) */

/* Current dispatch and current context variables */
__THREAD_INITIAL_EXEC struct _glapi_table *_mesa_glapi_tls_Dispatch
   = (struct _glapi_table *)table_noop_array;
__THREAD_INITIAL_EXEC void *_mesa_glapi_tls_Context;

static int
stub_compare(const void *key, const void *elem)
{
   const char *name = (const char *)key;
   const struct mapi_stub *stub = (const struct mapi_stub *)elem;

   return strcmp(name, &public_string_pool[stub->name_offset]);
}

/**
 * Return size of dispatch table struct as number of functions (or
 * slots).
 */
unsigned
_mesa_glapi_get_dispatch_table_size(void)
{
   return _gloffset_COUNT;
}

static const struct mapi_stub *
_glapi_get_stub(const char *name)
{
   if (!name || name[0] != 'g' || name[1] != 'l')
      return NULL;

   return (const struct mapi_stub *)
          bsearch(name + 2, public_stubs, ARRAY_SIZE(public_stubs),
                  sizeof(public_stubs[0]), stub_compare);
}

/**
 * Return offset of entrypoint for named function within dispatch table.
 */
int
_mesa_glapi_get_proc_offset(const char *funcName)
{
   const struct mapi_stub *stub = _glapi_get_stub(funcName);
   return stub ? stub->slot : -1;
}

/**
 * Return pointer to the named function.  If the function name isn't found
 * in the name of static functions, try generating a new API entrypoint on
 * the fly with assembly language.
 */
_glapi_proc
_mesa_glapi_get_proc_address(const char *funcName)
{
   const struct mapi_stub *stub = _glapi_get_stub(funcName);
   return stub ? entry_get_public(stub->slot) : NULL;
}

/**
 * Return the name of the function at the given dispatch offset.
 * This is only intended for debugging.
 */
const char *
_glapi_get_proc_name(unsigned offset)
{
   for (unsigned i = 0; i < ARRAY_SIZE(public_stubs); ++i) {
      if (public_stubs[i].slot == offset)
         return &public_string_pool[public_stubs[i].name_offset];
   }

   return NULL;
}

/** Return pointer to new dispatch table filled with no-op functions */
struct _glapi_table *
_glapi_new_nop_table(void)
{
   struct _glapi_table *table = malloc(_gloffset_COUNT * sizeof(_glapi_proc));

   if (table)
      memcpy(table, table_noop_array, _gloffset_COUNT * sizeof(_glapi_proc));
   return table;
}

/**
 * Set the current context pointer for this thread.
 * The context pointer is an opaque type which should be cast to
 * void from the real context pointer type.
 */
void
_mesa_glapi_set_context(void *ptr)
{
   _mesa_glapi_tls_Context = ptr;
}

/**
 * Get the current context pointer for this thread.
 * The context pointer is an opaque type which should be cast from
 * void to the real context pointer type.
 */
void *
_mesa_glapi_get_context(void)
{
   return _mesa_glapi_tls_Context;
}

/**
 * Set the global or per-thread dispatch table pointer.
 * If the dispatch parameter is NULL we'll plug in the no-op dispatch
 * table (__glapi_noop_table).
 */
void
_mesa_glapi_set_dispatch(struct _glapi_table *tbl)
{
   static once_flag flag = ONCE_FLAG_INIT;
   call_once(&flag, entry_patch_public);

   _mesa_glapi_tls_Dispatch =
      tbl ? tbl : (struct _glapi_table *)table_noop_array;
}

/**
 * Return pointer to current dispatch table for calling thread.
 */
struct _glapi_table *
_mesa_glapi_get_dispatch(void)
{
   return _mesa_glapi_tls_Dispatch;
}
