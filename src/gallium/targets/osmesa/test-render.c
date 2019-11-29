#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "GL/osmesa.h"

static void
render(void)
{
   glClearColor(0, 1, 0, 0);
   glClear(GL_COLOR_BUFFER_BIT);
}

int
main(int argc, char **argv)
{
   OSMesaContext ctx;
   uint32_t pixel;
   uint32_t green = 0xff << 8;
   int w = 1, h = 1;

   ctx = OSMesaCreateContext(GL_RGBA, NULL);
   if (!ctx) {
      fprintf(stderr, "Context create failed\n");
      return 1;
   }

   if (!OSMesaMakeCurrent(ctx, &pixel, GL_UNSIGNED_BYTE, w, h )) {
      fprintf(stderr, "MakeCurrent failed\n");
      return 1;
   }

   render();
   glFinish();

   if (pixel != green) {
      fprintf(stderr, "Expected: 0x%08x\n", green);
      fprintf(stderr, "Probed: 0x%08x\n", pixel);
      return 1;
   }

   OSMesaDestroyContext(ctx);

   return 0;
}
