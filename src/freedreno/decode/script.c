/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright © 2014 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#define LUA_COMPAT_APIINTCASTS

#include <assert.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util/u_math.h"

#include "cffdec.h"
#include "cffdump-pkt-handler.h"
#include "cffdump-desc-handler.h"
#include "disasm.h"
#include "rnnutil.h"
#include "script.h"

static lua_State *L;
static lua_State *iL;

#if 0
#define DBG(fmt, ...)                                                          \
   do {                                                                        \
      printf(" ** %s:%d ** " fmt "\n", __func__, __LINE__, ##__VA_ARGS__);     \
   } while (0)
#else
#define DBG(fmt, ...)                                                          \
   do {                                                                        \
   } while (0)
#endif

static int l_rnn_shaderstat(lua_State *L, struct rnn *rnn);

/* An rnn based decoder, which can either be decoding current register
 * values, or domain based decoding of a pm4 packet.
 */
struct rnndec {
   struct rnn base;

   /* for pm4 packet decoding: */
   uint32_t sizedwords;
   const uint32_t *dwords;
};

static inline struct rnndec *
to_rnndec(struct rnn *rnn)
{
   return (struct rnndec *)rnn;
}

static uint32_t
rnn_val(struct rnn *rnn, uint32_t regbase)
{
   struct rnndec *rnndec = to_rnndec(rnn);

   if (!rnndec->sizedwords) {
      return reg_val(regbase);
   } else if (regbase < rnndec->sizedwords) {
      return rnndec->dwords[regbase];
   } else {
      // XXX throw an error
      return -1;
   }
}

/**
 * Error printing macro.
 *
 * Prints an error message and exits (this does not return). There should be
 * a string on the Lua stack for this macro to work.
 */
#define error(_l, fmt, ...)                                                    \
   do {                                                                        \
      fprintf(stderr, fmt ": %s\n", ##__VA_ARGS__, lua_tostring(_l, -1));      \
      exit(1);                                                                 \
   } while (false)

/*
 * An enum type that can be used as string or number:
 */

struct rnndenum {
   struct rnnenum *e;
   const char *str;
   int val;
};

static int
l_meta_rnn_enum_tostring(lua_State *L)
{
   struct rnndenum *e = lua_touserdata(L, 1);
   if (e->str) {
      lua_pushstring(L, e->str);
   } else {
      char buf[32];
      sprintf(buf, "%u", e->val);
      lua_pushstring(L, buf);
   }
   return 1;
}

static int
l_meta_rnn_enum_eq(lua_State *L)
{
   struct rnndenum *e1 = lua_touserdata(L, 1);
   struct rnndenum *e2 = lua_touserdata(L, 2);

   /* Do the enum type+value match: */
   lua_pushboolean(L, (e1->e == e2->e) && (e1->val == e2->val));

   return 1;
}

static const struct luaL_Reg l_meta_rnn_enum[] = {
   {"__tostring", l_meta_rnn_enum_tostring},
   {"__eq",       l_meta_rnn_enum_eq},
   {NULL, NULL} /* sentinel */
};

static void
pushenum(struct lua_State *L, struct rnn *rnn, int val, struct rnnenum *info)
{
   struct rnndenum *e = lua_newuserdata(L, sizeof(*e));

   e->e = info;
   e->val = val;
   e->str = rnn_enumname(rnn, info->name, val);

   luaL_newmetatable(L, "rnnmetaenum");
   luaL_setfuncs(L, l_meta_rnn_enum, 0);
   lua_pop(L, 1);

   luaL_setmetatable(L, "rnnmetaenum");
}

/* Expose rnn decode to script environment as "rnn" library:
 */

struct rnndoff {
   struct rnn *rnn;
   struct rnndelem *elem;
   uint64_t offset;
};

static void
push_rnndoff(lua_State *L, struct rnn *rnn, struct rnndelem *elem,
             uint64_t offset)
{
   struct rnndoff *rnndoff = lua_newuserdata(L, sizeof(*rnndoff));
   rnndoff->rnn = rnn;
   rnndoff->elem = elem;
   rnndoff->offset = offset;
}

static int l_rnn_etype_array(lua_State *L, struct rnn *rnn,
                             struct rnndelem *elem, uint64_t offset);
static int l_rnn_etype_reg(lua_State *L, struct rnn *rnn, struct rnndelem *elem,
                           uint64_t offset);

static int
pushdecval(struct lua_State *L, struct rnn *rnn, uint64_t regval,
           struct rnntypeinfo *info)
{
   union rnndecval val;
   switch (rnn_decodelem(rnn, info, regval, &val)) {
   case RNN_TTYPE_ENUM:
   case RNN_TTYPE_INLINE_ENUM:
      pushenum(L, rnn, val.i, info->eenum);
      return 1;
   case RNN_TTYPE_INT:
      lua_pushinteger(L, val.i);
      return 1;
   case RNN_TTYPE_UINT:
   case RNN_TTYPE_HEX:
      lua_pushinteger(L, val.u);
      return 1;
   case RNN_TTYPE_FLOAT:
      lua_pushnumber(L, uif(val.u));
      return 1;
   case RNN_TTYPE_BOOLEAN:
      lua_pushboolean(L, val.u);
      return 1;
   case RNN_TTYPE_INVALID:
   default:
      return 0;
   }
}

static int
pushfield(lua_State *L, struct rnntypeinfo *info, struct rnn *rnn,
          uint64_t offset, const char *name)
{
   struct rnnbitfield **bitfields;
   int bitfieldsnum;
   int i;

   switch (info->type) {
   case RNN_TTYPE_BITSET:
      bitfields = info->ebitset->bitfields;
      bitfieldsnum = info->ebitset->bitfieldsnum;
      break;
   case RNN_TTYPE_INLINE_BITSET:
      bitfields = info->bitfields;
      bitfieldsnum = info->bitfieldsnum;
      break;
   default:
      printf("invalid register type: %d\n", info->type);
      return 0;
   }

   for (i = 0; i < bitfieldsnum; i++) {
      struct rnnbitfield *bf = bitfields[i];
      if (!strcmp(name, bf->name)) {
         uint32_t regval = rnn_val(rnn, offset);

         regval &= typeinfo_mask(&bf->typeinfo);
         regval >>= bf->typeinfo.low;
         regval <<= bf->typeinfo.shr;

         DBG("name=%s, info=%p, type=%d, regval=%x", name, info,
             bf->typeinfo.type, regval);

         return pushdecval(L, rnn, regval, &bf->typeinfo);
      }
   }

   return 0;
}

static int
l_rnn_etype(lua_State *L, struct rnn *rnn, struct rnndelem *elem,
            uint64_t offset)
{
   int ret;
   uint64_t regval;
   DBG("elem=%p (%d), offset=%lu", elem, elem->type, offset);
   switch (elem->type) {
   case RNN_ETYPE_REG:
      /* if a register has no bitfields, just return
       * the raw value:
       */
      regval = rnn_val(rnn, offset);
      if (elem->width == 64)
         regval |= (uint64_t)rnn_val(rnn, offset + 1) << 32;
      regval <<= elem->typeinfo.shr;
      ret = pushdecval(L, rnn, regval, &elem->typeinfo);
      if (ret)
         return ret;
      return l_rnn_etype_reg(L, rnn, elem, offset);
   case RNN_ETYPE_ARRAY:
      return l_rnn_etype_array(L, rnn, elem, offset);
   case RNN_ETYPE_STRIPE:
      assert(elem->subelemsnum == 1);
      return l_rnn_etype(L, rnn, elem->subelems[0], offset);
   default:
      /* hmm.. */
      printf("unhandled type: %d\n", elem->type);
      return 0;
   }
}

/*
 * Struct Object:
 * To implement stuff like 'RB_MRT[n].CONTROL' we need a struct-object
 * to represent the current array index (ie. 'RB_MRT[n]')
 */

static int
l_rnn_struct_meta_index(lua_State *L)
{
   struct rnndoff *rnndoff = lua_touserdata(L, 1);
   const char *name = lua_tostring(L, 2);
   struct rnndelem *elem = rnndoff->elem;
   int i;

   for (i = 0; i < elem->subelemsnum; i++) {
      struct rnndelem *subelem = elem->subelems[i];
      if (!strcmp(name, subelem->name)) {
         return l_rnn_etype(L, rnndoff->rnn, subelem,
                            rnndoff->offset + subelem->offset);
      }
   }

   return 0;
}

static const struct luaL_Reg l_meta_rnn_struct[] = {
   {"__index", l_rnn_struct_meta_index}, {NULL, NULL} /* sentinel */
};

static int
l_rnn_etype_struct(lua_State *L, struct rnn *rnn, struct rnndelem *elem,
                   uint64_t offset)
{
   push_rnndoff(L, rnn, elem, offset);

   luaL_newmetatable(L, "rnnmetastruct");
   luaL_setfuncs(L, l_meta_rnn_struct, 0);
   lua_pop(L, 1);

   luaL_setmetatable(L, "rnnmetastruct");

   return 1;
}

/*
 * Array Object:
 */

static int
l_rnn_array_meta_index(lua_State *L)
{
   struct rnndoff *rnndoff = lua_touserdata(L, 1);
   int idx = lua_tointeger(L, 2);
   struct rnndelem *elem = rnndoff->elem;
   uint64_t offset = rnndoff->offset + (elem->stride * idx);

   DBG("rnndoff=%p, idx=%d, numsubelems=%d", rnndoff, idx,
       rnndoff->elem->subelemsnum);

   /* if just a single sub-element, it is directly a register,
    * otherwise we need to accumulate the array index while
    * we wait for the register name within the array..
    */
   if (elem->subelemsnum == 1) {
      return l_rnn_etype(L, rnndoff->rnn, elem->subelems[0], offset);
   } else {
      return l_rnn_etype_struct(L, rnndoff->rnn, elem, offset);
   }

   return 0;
}

static const struct luaL_Reg l_meta_rnn_array[] = {
   {"__index", l_rnn_array_meta_index}, {NULL, NULL} /* sentinel */
};

static int
l_rnn_etype_array(lua_State *L, struct rnn *rnn, struct rnndelem *elem,
                  uint64_t offset)
{
   push_rnndoff(L, rnn, elem, offset);

   luaL_newmetatable(L, "rnnmetaarray");
   luaL_setfuncs(L, l_meta_rnn_array, 0);
   lua_pop(L, 1);

   luaL_setmetatable(L, "rnnmetaarray");

   return 1;
}

/*
 * Register element:
 */

static int
l_rnn_reg_meta_index(lua_State *L)
{
   struct rnndoff *rnndoff = lua_touserdata(L, 1);
   const char *name = lua_tostring(L, 2);
   struct rnndelem *elem = rnndoff->elem;
   struct rnntypeinfo *info = &elem->typeinfo;

   int ret = pushfield(L, info, rnndoff->rnn, rnndoff->offset, name);

   if (!ret)
      printf("invalid member: %s\n", name);

   return ret;
}

static int
l_rnn_reg_meta_tostring(lua_State *L)
{
   struct rnndoff *rnndoff = lua_touserdata(L, 1);
   uint32_t regval = rnn_val(rnndoff->rnn, rnndoff->offset);
   struct rnndecaddrinfo *info = rnn_reginfo(rnndoff->rnn, rnndoff->offset);
   char *decoded;
   if (info && info->typeinfo) {
      decoded = rnndec_decodeval(rnndoff->rnn->vc, info->typeinfo, regval);
   } else {
      asprintf(&decoded, "%08x", regval);
   }
   lua_pushstring(L, decoded);
   free(decoded);
   rnn_reginfo_free(info);
   return 1;
}

static const struct luaL_Reg l_meta_rnn_reg[] = {
   {"__index", l_rnn_reg_meta_index},
   {"__tostring", l_rnn_reg_meta_tostring},
   {NULL, NULL} /* sentinel */
};

static int
l_rnn_etype_reg(lua_State *L, struct rnn *rnn, struct rnndelem *elem,
                uint64_t offset)
{
   push_rnndoff(L, rnn, elem, offset);

   luaL_newmetatable(L, "rnnmetareg");
   luaL_setfuncs(L, l_meta_rnn_reg, 0);
   lua_pop(L, 1);

   luaL_setmetatable(L, "rnnmetareg");

   return 1;
}

/*
 * Enum type element
 */

struct rnndenumtype {
   struct rnnenum *e;
   struct rnn *rnn;
};

static int
l_rnn_enumtype_meta_index(lua_State *L)
{
   struct rnndenumtype *et = lua_touserdata(L, 1);
   const char *name = lua_tostring(L, 2);

   int val = rnn_enumval(et->rnn, et->e->name, name);
   if (val < 0)
      return 0;

   pushenum(L, et->rnn, val, et->e);

   return 1;
}

static const struct luaL_Reg l_meta_rnn_enumtype[] = {
   {"__index", l_rnn_enumtype_meta_index},
   {NULL, NULL} /* sentinel */
};

static int
l_rnn_etype_enumtype(lua_State *L, struct rnn *rnn, struct rnnenum *e)
{
   struct rnndenumtype *et = lua_newuserdata(L, sizeof(*e));

   et->e = e;
   et->rnn = rnn;

   luaL_newmetatable(L, "rnnmetaenumtype");
   luaL_setfuncs(L, l_meta_rnn_enumtype, 0);
   lua_pop(L, 1);

   luaL_setmetatable(L, "rnnmetaenumtype");

   return 1;
}

/*
 * rnn domain based decoding of _something_ (a pm4 packet,
 * a descriptor, etc)
 */

static int
l_rnn_meta_dom_index(lua_State *L)
{
   struct rnndec *rnndec = lua_touserdata(L, 1);
   struct rnn *rnn = &rnndec->base;

   if (lua_isnumber(L, 2)) {
      /* index as an array, is pkt[0].FOO: */
      uint32_t offset = (uint32_t)lua_tonumber(L, 2);
      struct rnndelem *elem = rnn_regoff(rnn, offset);
      if (elem)
         return l_rnn_etype(L, rnn, elem, elem->offset);

      /* Just return the raw value if not defined as an element */
      if (offset < rnndec->sizedwords) {
         lua_pushnumber(L, rnndec->dwords[offset]);
         return 1;
      }
   } else if (lua_isstring(L, 2)) {
      const char *name = lua_tostring(L, 2);

      struct rnndelem *elem = rnn_regelem(rnn, name);
      if (elem)
         return l_rnn_etype(L, rnn, elem, elem->offset);

      /* If not indexed like an array, search thru all
       * the elements in the domain finding the matching
       * subelem.
       *
       * This handles the pkt.FOO case
       */

      for (unsigned i = 0; i < rnndec->sizedwords; i++) {
         elem = rnn_regoff(rnn, i);
         if (!elem)
            continue;

         if (!strcmp(name, elem->name))
            return l_rnn_etype(L, rnn, elem, elem->offset);

         /* Try to find and push named field within the element: */
         int ret = pushfield(L, &elem->typeinfo, rnn, i, name);
         if (ret)
            return ret;
      }
   }

   return 0;
}

/*
 * A wrapper object for rnndomain based decoding of an array of dwords
 * (ie. for pm4 packet decoding).  Mostly re-uses the register-value
 * decoding for the individual dwords and bitfields.
 */

static int
l_rnn_meta_dom_gc(lua_State *L)
{
   // TODO
   // struct rnn *rnn = lua_touserdata(L, 1);
   // rnn_deinit(rnn);
   return 0;
}

static const struct luaL_Reg l_meta_rnn_dom[] = {
   {"__index", l_rnn_meta_dom_index},
   {"__gc", l_rnn_meta_dom_gc},
   {NULL, NULL} /* sentinel */
};

static int
l_rnn_dom(lua_State *L, const uint32_t *dwords, uint32_t sizedwords,
          struct rnn *rnn, struct rnndomain *dom)
{
   struct rnndec *rnndec = lua_newuserdata(L, sizeof(*rnndec));

   rnndec->base = *rnn;
   rnndec->base.dom[0] = dom;
   rnndec->base.dom[1] = NULL;
   rnndec->dwords = dwords;
   rnndec->sizedwords = sizedwords;

   luaL_newmetatable(L, "rnnmetadom");
   luaL_setfuncs(L, l_meta_rnn_dom, 0);
   lua_pop(L, 1);

   luaL_setmetatable(L, "rnnmetadom");

   return 1;
}

/*
 * Shader stats object
 */

static int
l_rnn_meta_shaderstat_index(lua_State *L)
{
   struct rnn *rnn = lua_touserdata(L, 1);
   const char *name = lua_tostring(L, 2);
   enum mesa_shader_stage stage;
   struct rnndomain *dom = rnn_finddomain(rnn->db, "ir3_shader_stats");

   if (!dom)
      error(L, "No domain: ir3_shader_stats");

   if (!strcmp(name, "vs")) {
      stage = MESA_SHADER_VERTEX;
   } else if (!strcmp(name, "hs")) {
      stage = MESA_SHADER_TESS_CTRL;
   } else if (!strcmp(name, "ds")) {
      stage = MESA_SHADER_TESS_EVAL;
   } else if (!strcmp(name, "gs")) {
      stage = MESA_SHADER_GEOMETRY;
   } else if (!strcmp(name, "fs")) {
      stage = MESA_SHADER_FRAGMENT;
   } else if (!strcmp(name, "cs")) {
      stage = MESA_SHADER_COMPUTE;
   } else {
      return 0;
   }

   struct shader_stats *stats = get_shader_stats(stage);

   return l_rnn_dom(L, (uint32_t *)stats, DIV_ROUND_UP(sizeof(*stats), 4), rnn, dom);
}

static const struct luaL_Reg l_meta_rnn_shaderstat[] = {
   {"__index", l_rnn_meta_shaderstat_index},
   {NULL, NULL} /* sentinel */
};

static int
l_rnn_shaderstat(lua_State *L, struct rnn *rnn)
{
   struct rnndec *rnndec = lua_newuserdata(L, sizeof(*rnndec));

   rnndec->base = *rnn;

   luaL_newmetatable(L, "rnnmetadom");
   luaL_setfuncs(L, l_meta_rnn_shaderstat, 0);
   lua_pop(L, 1);

   luaL_setmetatable(L, "rnnmetadom");

   return 1;
}

/*
 *
 */

static int
l_rnn_meta_index(lua_State *L)
{
   struct rnn *rnn = lua_touserdata(L, 1);
   const char *name = lua_tostring(L, 2);

   if (!strcmp(name, "shaderstat"))
      return l_rnn_shaderstat(L, rnn);

   struct rnndelem *elem = rnn_regelem(rnn, name);
   if (elem)
      return l_rnn_etype(L, rnn, elem, elem->offset);

   struct rnnenum *e = rnn_enumelem(rnn, name);
   if (e)
      return l_rnn_etype_enumtype(L, rnn, e);

   return 0;
}

static int
l_rnn_meta_gc(lua_State *L)
{
   // TODO
   // struct rnn *rnn = lua_touserdata(L, 1);
   // rnn_deinit(rnn);
   return 0;
}

static const struct luaL_Reg l_meta_rnn[] = {
   {"__index", l_rnn_meta_index},
   {"__gc", l_rnn_meta_gc},
   {NULL, NULL} /* sentinel */
};

static int
l_rnn_init(lua_State *L)
{
   const char *gpuname = lua_tostring(L, 1);
   struct rnndec *rnndec = lua_newuserdata(L, sizeof(*rnndec));
   _rnn_init(&rnndec->base, 0);
   rnn_load(&rnndec->base, gpuname);
   rnndec->sizedwords = 0;

   luaL_newmetatable(L, "rnnmeta");
   luaL_setfuncs(L, l_meta_rnn, 0);
   lua_pop(L, 1);

   luaL_setmetatable(L, "rnnmeta");

   return 1;
}

static int
l_rnn_enumname(lua_State *L)
{
   struct rnn *rnn = lua_touserdata(L, 1);
   const char *name = lua_tostring(L, 2);
   uint32_t val = (uint32_t)lua_tonumber(L, 3);
   lua_pushstring(L, rnn_enumname(rnn, name, val));
   return 1;
}

static int
l_rnn_regname(lua_State *L)
{
   struct rnn *rnn = lua_touserdata(L, 1);
   uint32_t regbase = (uint32_t)lua_tonumber(L, 2);
   lua_pushstring(L, rnn_regname(rnn, regbase, 1));
   return 1;
}

static int
l_rnn_regval(lua_State *L)
{
   struct rnn *rnn = lua_touserdata(L, 1);
   uint32_t regbase = (uint32_t)lua_tonumber(L, 2);
   uint32_t regval = (uint32_t)lua_tonumber(L, 3);
   struct rnndecaddrinfo *info = rnn_reginfo(rnn, regbase);
   char *decoded;
   if (info && info->typeinfo) {
      decoded = rnndec_decodeval(rnn->vc, info->typeinfo, regval);
   } else {
      asprintf(&decoded, "%08x", regval);
   }
   lua_pushstring(L, decoded);
   free(decoded);
   rnn_reginfo_free(info);
   return 1;
}

static const struct luaL_Reg l_rnn[] = {
   {"init", l_rnn_init},
   {"enumname", l_rnn_enumname},
   {"regname", l_rnn_regname},
   {"regval", l_rnn_regval},
   {NULL, NULL} /* sentinel */
};

/* Expose the register state to script enviroment as a "regs" library:
 */

static int
l_reg_written(lua_State *L)
{
   uint32_t regbase = (uint32_t)lua_tonumber(L, 1);
   lua_pushnumber(L, reg_written(regbase));
   return 1;
}

static int
l_reg_lastval(lua_State *L)
{
   uint32_t regbase = (uint32_t)lua_tonumber(L, 1);
   lua_pushnumber(L, reg_lastval(regbase));
   return 1;
}

static int
l_reg_val(lua_State *L)
{
   uint32_t regbase = (uint32_t)lua_tonumber(L, 1);
   lua_pushnumber(L, reg_val(regbase));
   return 1;
}

static const struct luaL_Reg l_regs[] = {
   {"written", l_reg_written},
   {"lastval", l_reg_lastval},
   {"val", l_reg_val},
   {NULL, NULL} /* sentinel */
};

/* Expose the private functionality to the internal pkt enviroment as a "priv"
 * library:
 */

static int
l_priv_reg_set(lua_State *L)
{
   uint32_t regbase = (uint32_t)lua_tonumber(L, 1);
   uint32_t regval = (uint32_t)lua_tonumber(L, 2);

   reg_set(regbase, regval);

   return 1;
}

static const struct luaL_Reg l_priv[] = {
   {"reg_set", l_priv_reg_set},
   {NULL, NULL} /* sentinel */
};

/* Expose API to lookup snapshot buffers:
 */

uint64_t gpubaseaddr(uint64_t gpuaddr);
unsigned hostlen(uint64_t gpuaddr);
void *hostptr(uint64_t gpuaddr);

/* given address, return base-address of buffer: */
static int
l_bo_base(lua_State *L)
{
   uint64_t addr = (uint64_t)lua_tonumber(L, 1);
   lua_pushnumber(L, gpubaseaddr(addr));
   return 1;
}

/* given address, return the remaining size of the buffer: */
static int
l_bo_size(lua_State *L)
{
   uint64_t addr = (uint64_t)lua_tonumber(L, 1);
   lua_pushnumber(L, hostlen(addr));
   return 1;
}

static int
l_bo_write(lua_State *L)
{
   uint64_t addr = (uint64_t)lua_tonumber(L, 1);
   uint32_t val = (uint32_t)lua_tonumber(L, 2);
   uint32_t *ptr = hostptr(addr);

   if (ptr)
      *ptr = val;

   lua_pushboolean(L, !!ptr);
   return 1;
}

static int
l_bo_index(lua_State *L)
{
   uint64_t addr = (uint64_t)lua_tonumber(L, 1);
   uint32_t *ptr = hostptr(addr);
   if (!ptr)
      return 0;
   lua_pushnumber(L, *ptr);
   return 1;
}

static const struct luaL_Reg l_bos[] = {
   {"base", l_bo_base},
   {"size", l_bo_size},
   {"write", l_bo_write},
   {"__index", l_bo_index},
   {NULL, NULL} /* sentinel */
};

static void
openlib(lua_State *state, const char *lib, const luaL_Reg *reg)
{
   lua_newtable(state);
   luaL_setfuncs(state, reg, 0);
   lua_setglobal(state, lib);
}

static void
common_libs(lua_State *state)
{
   luaL_openlibs(state);
   openlib(state, "bos", l_bos);
   openlib(state, "regs", l_regs);
   openlib(state, "rnn", l_rnn);
}

/* called at start to load the script: */
int
script_load(const char *file)
{
   int ret;

   assert(!L);

   L = luaL_newstate();
   common_libs(L);

   ret = luaL_loadfile(L, file);
   if (ret)
      error(L, "Loading file");

   ret = lua_pcall(L, 0, LUA_MULTRET, 0);
   if (ret)
      error(L, "pcall on file load");

   return 0;
}

void
internal_lua_pkt_handler_load(void)
{
   int ret;

   if (iL)
      return;

   iL = luaL_newstate();

   common_libs(iL);
   openlib(iL, "priv", l_priv);

   ret = luaL_loadstring(iL, cffdump_pkt_handler_lua_src);
   if (ret)
      error(iL, "loadstring(cffdump_pkt_handler_lua_src)");

   ret = lua_pcall(iL, 0, LUA_MULTRET, 0);
   if (ret)
      error(iL, "pcall on loadstring(cffdump_pkt_handler_lua_src)");

   ret = luaL_loadstring(iL, cffdump_desc_handler_lua_src);
   if (ret)
      error(iL, "loadstring(cffdump_desc_handler_lua_src)");

   ret = lua_pcall(iL, 0, LUA_MULTRET, 0);
   if (ret)
      error(iL, "pcall on loadstring(cffdump_desc_handler_lua_src)");
}

void
internal_lua_pkt_handler_init_rnn(struct rnn *rnn)
{
   struct rnndec *rnndec = lua_newuserdata(iL, sizeof(*rnndec));
   rnndec->base = *rnn;
   rnndec->sizedwords = 0;

   luaL_newmetatable(iL, "rnnmeta");
   luaL_setfuncs(iL, l_meta_rnn, 0);
   lua_pop(iL, 1);

   luaL_setmetatable(iL, "rnnmeta");

   lua_setglobal(iL, "r");
}

/* called at start of each cmdstream file: */
void
script_start_cmdstream(const char *name)
{
   const char *func_name = "start_cmdstream";

   if (!L)
      return;

   lua_getglobal(L, func_name);

   /* if no handler just ignore it: */
   if (!lua_isfunction(L, -1)) {
      lua_pop(L, 1);
      return;
   }

   lua_pushstring(L, name);

   /* do the call (1 arguments, 0 result) */
   if (lua_pcall(L, 1, 0, 0) != 0)
      error(L, "error running function `%s'", func_name);
}

/* called at each DRAW_INDX, calls script drawidx fxn to process
 * the current state
 */
void
script_draw(const char *primtype, uint32_t nindx)
{
   const char *func_name = "draw";

   if (!L)
      return;

   lua_getglobal(L, func_name);

   /* if no handler just ignore it: */
   if (!lua_isfunction(L, -1)) {
      lua_pop(L, 1);
      return;
   }

   lua_pushstring(L, primtype);
   lua_pushnumber(L, nindx);

   /* do the call (2 arguments, 0 result) */
   if (lua_pcall(L, 2, 0, 0) != 0)
      error(L, "error running function `%s'", func_name);
}

/*
 * Packet/etc handlers:
 */

static bool
setup_call(lua_State *state, const uint32_t *dwords, uint32_t sizedwords,
           const char *name, struct rnn *rnn, struct rnndomain *dom)
{
   if (!state)
      return false;

   assert(state == L || state == iL);

   lua_getglobal(state, name);

   /* if no handler for the packet, just ignore it: */
   if (!lua_isfunction(state, -1)) {
      lua_pop(state, 1);
      return false;
   }

   return l_rnn_dom(state, dwords, sizedwords, rnn, dom);
}

/* called to general pm4 packet decoding, such as texture/sampler state
 */
static bool
handle_packet_setup(lua_State *state, const uint32_t *dwords,
                    uint32_t sizedwords, struct rnn *rnn, struct rnndomain *dom)
{
   if (!setup_call(state, dwords, sizedwords, dom->name, rnn, dom))
      return false;

   lua_pushnumber(state, sizedwords);

   return true;
}

void
script_packet(const uint32_t *dwords, uint32_t sizedwords, struct rnn *rnn,
              struct rnndomain *dom)
{
   bool ret;

   ret = handle_packet_setup(L, dwords, sizedwords, rnn, dom);
   if (!ret)
      return;

   if (lua_pcall(L, 2, 0, 0) != 0)
      error(L, "error running packet function (%s)", __func__);
}

const char *
internal_packet(const uint32_t *dwords, uint32_t sizedwords, struct rnn *rnn,
                struct rnndomain *dom)
{
   bool ret;

   ret = handle_packet_setup(iL, dwords, sizedwords, rnn, dom);
   if (!ret)
      return NULL;

   /* 2 args, 1 result */
   if (lua_pcall(iL, 2, 1, 0) != 0)
      error(iL, "error running packet function (%s)", __func__);

   char *str;
   asprintf(&str, "%s", lua_tostring(iL, -1));
   lua_pop(iL, 1);

   return str;
}

bool
script_show_descriptor(const uint32_t *dwords,
                       uint32_t sizedwords,
                       int base, int idx,
                       const char *type,
                       const char *pm4_pkt,
                       struct rnn *rnn,
                       struct rnndomain *dom)
{
   const char *func_name = "show_descriptor";

   /* If we cannot call handler, fall back to showing all descriptor variants: */
   if (!setup_call(iL, dwords, sizedwords, func_name, rnn, dom))
      return true;

   struct rnnenum *e = rnn_enumelem(rnn, "desctype");
   assert(e);
   pushenum(iL, rnn, rnn_enumval(rnn, "desctype", type), e);

   e = rnn_enumelem(rnn, "adreno_pm4_type3_packets");
   assert(e);
   pushenum(iL, rnn, rnn_enumval(rnn, "adreno_pm4_type3_packets", pm4_pkt), e);

   lua_pushinteger(iL, base);
   lua_pushinteger(iL, idx);

   /* 5 args, 1 result */
   if (lua_pcall(iL, 5, 1, 0) != 0)
      error(iL, "error running function `%s'", func_name);

   bool ret = lua_toboolean(iL, -1);
   lua_pop(iL, 1);

   return ret;
}

/* helper to call fxn that takes and returns void: */
static void
simple_call(const char *name)
{
   if (!L)
      return;

   lua_getglobal(L, name);

   /* if no handler just ignore it: */
   if (!lua_isfunction(L, -1)) {
      lua_pop(L, 1);
      return;
   }

   /* do the call (0 arguments, 0 result) */
   if (lua_pcall(L, 0, 0, 0) != 0)
      error(L, "error running function `%s'", name);
}

/* called at end of each cmdstream file: */
void
script_end_cmdstream(void)
{
   simple_call("end_cmdstream");
}

/* called at start of submit/issueibcmds: */
void
script_start_submit(void)
{
   simple_call("start_submit");
}

/* called at end of submit/issueibcmds: */
void
script_end_submit(void)
{
   simple_call("end_submit");
}

/* called after last cmdstream file: */
void
script_finish(void)
{
   if (!L)
      return;

   simple_call("finish");

   lua_close(L);
   L = NULL;
}

void
internal_lua_pkt_handler_finish(void)
{
   if (!iL)
      return;

   lua_close(iL);
   iL = NULL;
}
