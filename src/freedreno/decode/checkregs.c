/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its susidiaries.
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdio.h>

#include "rnn.h"
#include "rnndec.h"
#include "rnnutil.h"

/**
 * A simple utility to check for overlapping/conflicting reg definitions
 * for any given generation.
 */

struct range {
   uint32_t min, max;
};

static struct range
elem_range(const struct rnndelem *e)
{
   uint32_t len;

   if (e->type == RNN_ETYPE_ARRAY) {
      len = e->length * e->stride;
   } else {
      assert(e->width >= 32);
      len = e->width / 32;
   }

   return (struct range){
      .min = e->offset,
      .max = e->offset + len - 1,
   };
}

int
main(int argc, char **argv)
{
   struct rnn *rnn;
   int ret = 0;

   if (argc != 2) {
      fprintf(stderr, "usage: dumpregs GEN\n");
      return -1;
   }

   rnn = rnn_new(true);

   rnn_load(rnn, argv[1]);

   for (unsigned i = 0; i < rnn->dom[0]->subelemsnum; i++) {
      struct rnndelem *ei = rnn->dom[0]->subelems[i];

      if (!rnndec_varmatch(rnn->vc, &ei->varinfo))
         continue;

      struct range ri = elem_range(ei);

      for (unsigned j = i + 1; j < rnn->dom[0]->subelemsnum; j++) {
         struct rnndelem *ej = rnn->dom[0]->subelems[j];

         if (!rnndec_varmatch(rnn->vc, &ej->varinfo))
            continue;

         struct range rj = elem_range(ej);

         if ((ri.max >= rj.min) && (rj.max >= ri.min)) {
            fprintf(stderr, "Conflict: %s (0x%04x->0x%04x) vs %s (0x%04x->0x%04x)\n",
                    ei->name, ri.min, ri.max, ej->name, rj.min, rj.max);
            ret = -1;
         }
      }
   }

   return ret;
}
