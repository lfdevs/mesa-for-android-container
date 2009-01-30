/**
 * Display dds files using gltc library.
 */


#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <math.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/glut.h>
#include "gltc.h"


static int Win, WinHeight=400, WinWidth=400;
static GLfloat Xrot = 0, Yrot = 0, Zrot = 0;

static GLubyte *Image;
static GLuint Width, Height;
static GLenum Format;



static void
LoadFile(const char *name)
{
   gltc_dword r;
   struct gltc_image img, img2;
   gltc_dword format = 0;

   r = gltc_file_load(name, &img, format);
   printf("file_load retval=%lu\n", r);

   r = gltc_image_convert(&img, &img2, GLTC_A8_R8_G8_B8);
   printf("image_convert retval=%lu\n", r);

   Image = gltc_image_data(&img2, 0);
   Width = gltc_image_width(&img2, 0);
   Height = gltc_image_height(&img2, 0);
   switch (img2.format) {
   case GLTC_A8_R8_G8_B8:
      Format = GL_BGRA;
      break;
   default:
      printf("format is %d\n", img2.format);
   }
}


static void
Draw(void)
{
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   glWindowPos2iARB(10, WinHeight - 10);
   glPixelZoom(1, -1);
   glDrawPixels(Width, Height, Format, GL_UNSIGNED_BYTE, Image);

   glutSwapBuffers();
}


static void
Reshape(int width, int height)
{
   WinWidth = width;
   WinHeight = height;
   glViewport(0, 0, width, height);
   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glFrustum(-1.0, 1.0, -1.0, 1.0, 5.0, 25.0);
   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();
   glTranslatef(0.0, 0.0, -15.0);
}


static void
Key(unsigned char key, int x, int y)
{
   const GLfloat step = 3.0;
   (void) x;
   (void) y;
   switch (key) {
      case 'z':
         Zrot -= step;
         break;
      case 'Z':
         Zrot += step;
         break;
      case 27:
         glutDestroyWindow(Win);
         exit(0);
         break;
   }
   glutPostRedisplay();
}


static void
SpecialKey(int key, int x, int y)
{
   const GLfloat step = 3.0;
   (void) x;
   (void) y;
   switch (key) {
      case GLUT_KEY_UP:
         Xrot -= step;
         break;
      case GLUT_KEY_DOWN:
         Xrot += step;
         break;
      case GLUT_KEY_LEFT:
         Yrot -= step;
         break;
      case GLUT_KEY_RIGHT:
         Yrot += step;
         break;
   }
   glutPostRedisplay();
}


static void
Init(void)
{
}


static void
Usage(void)
{
}


static void
Args(int argc, char *argv[])
{
   if (argc > 1)
      LoadFile(argv[1]);
   else {
      Usage();
      exit(0);
   }
}


int
main(int argc, char *argv[])
{
   glutInit(&argc, argv);
   glutInitWindowPosition(0, 0);
   glutInitWindowSize(WinWidth, WinHeight);
   glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);
   Win = glutCreateWindow(argv[0]);
   glutReshapeFunc(Reshape);
   glutKeyboardFunc(Key);
   glutSpecialFunc(SpecialKey);
   glutDisplayFunc(Draw);
   Args(argc, argv);
   Init();
   glutMainLoop();
   return 0;
}
