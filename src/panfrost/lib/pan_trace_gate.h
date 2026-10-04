#ifndef PAN_TRACE_GATE_H
#define PAN_TRACE_GATE_H
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Debug prints stay silent unless PANVK_TRACE=1 or PANVK_DEBUG contains "trace". */
static inline bool
panvk_trace_on(void)
{
   static int state = -1;
   if (state < 0) {
      const char *t = getenv("PANVK_TRACE");
      const char *d = getenv("PANVK_DEBUG");
      state = ((t && t[0] == '1') || (d && strstr(d, "trace"))) ? 1 : 0;
   }
   return state == 1;
}

#define PANVK_TRACE_PRINTF(...)          \
   do {                                  \
      if (panvk_trace_on())              \
         fprintf(stderr, __VA_ARGS__);   \
   } while (0)

#endif
