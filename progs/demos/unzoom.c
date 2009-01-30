#include <stdio.h>
#include <GL/gl.h>


/**
 * Can use this for unzooming X or Y values.
 */
static GLint
unzoom_x(GLfloat zoomX, GLint imageX, GLint zx)
{
   /*
   zx = imageX + (x - imageX) * zoomX;
   zx - imageX = (x - imageX) * zoomX;
   (zx - imageX) / zoomX = x - imageX;
   */
   GLint x;
   if (zoomX < 0.0)
      zx++;
   x = imageX + (GLint) ((zx - imageX) / zoomX);
   return x;
}


static void test(float Xzoom)
{
   int x0 = 26;
   int imgX = 220;
   int i = 0;
   int span_x = 220;
   int j = unzoom_x(Xzoom, imgX, x0 + i) - span_x;
   printf ("zoom %f  j = %d\n", Xzoom, j);
}


int main(int argc, char *argv[])
{
   test(1.0);
   test(-1.0);
   test( -1.000001 );
   test( -0.999999 );

   return 0;
}
