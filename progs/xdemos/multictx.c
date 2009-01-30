/*
 * Test rendering with two or more rendering contexts into one window.
 * Brian Paul
 * 26 Feb 2007
 *
 * Copyright (C) 2007  Brian Paul   All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


#include <GL/gl.h>
#include <GL/glx.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/keysym.h>

#define MAX_CONTEXTS 3

static const char *DisplayName = NULL;
static Display *Dpy;
static XVisualInfo *VisInfo;
static Window Win;
static GLXContext Contexts[MAX_CONTEXTS];
static GLboolean ContextEnabled[MAX_CONTEXTS];
static float Angle;
static int WinWidth = 600, WinHeight = 300;

static GLuint TexObj = 0;


static void
Error(const char *msg)
{
   fprintf(stderr, "multictx error: %s\n", msg);
   exit(1);
}


static void
CreateWindow(const char *name)
{
   int attrib[] = { GLX_RGBA,
		    GLX_RED_SIZE, 1,
		    GLX_GREEN_SIZE, 1,
		    GLX_BLUE_SIZE, 1,
		    GLX_DOUBLEBUFFER,
		    None };
   int scrnum;
   XSetWindowAttributes attr;
   unsigned long mask;
   Window root;
   int xpos = 0, ypos = 0;
   static int n = 0;

   scrnum = DefaultScreen(Dpy);
   root = RootWindow(Dpy, scrnum);

   VisInfo = glXChooseVisual(Dpy, scrnum, attrib);
   if (!VisInfo) {
      Error("Unable to find RGB, double-buffered visual");
   }

   /* window attributes */
   xpos = (n % 10) * 100;
   ypos = (n / 10) * 100;
   n++;

   attr.background_pixel = 0;
   attr.border_pixel = 0;
   attr.colormap = XCreateColormap(Dpy, root, VisInfo->visual, AllocNone);
   attr.event_mask = StructureNotifyMask | ExposureMask | KeyPressMask;
   mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;

   Win = XCreateWindow(Dpy, root, xpos, ypos, WinWidth, WinHeight,
		        0, VisInfo->depth, InputOutput,
		        VisInfo->visual, mask, &attr);
   if (!Win) {
      Error("Couldn't create window");
   }

   {
      XSizeHints sizehints;
      sizehints.x = xpos;
      sizehints.y = ypos;
      sizehints.width  = WinWidth;
      sizehints.height = WinHeight;
      sizehints.flags = USSize | USPosition;
      XSetNormalHints(Dpy, Win, &sizehints);
      XSetStandardProperties(Dpy, Win, name, name,
                              None, (char **)NULL, 0, &sizehints);
   }

   XMapWindow(Dpy, Win);
}


static void
InitContext(void)
{
   static const GLubyte checker[2][2][4] = {
      { {255, 255, 255, 255}, {  0,   0,   0, 255} },
      { {  0,   0,   0,   0}, {255, 255, 255, 255} }
   };
   glGenTextures(1, &TexObj);
   assert(TexObj);
   glBindTexture(GL_TEXTURE_2D, TexObj);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGB,
                GL_UNSIGNED_BYTE, checker);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glEnable(GL_TEXTURE_2D);
}


static void
Setup(void)
{
   int i;

   Dpy = XOpenDisplay(DisplayName);
   if (!Dpy) {
      Error("Unable to open display");
   }

   CreateWindow("multictx");

   for (i = 0; i < MAX_CONTEXTS; i++) {
      Contexts[i] = glXCreateContext(Dpy, VisInfo, NULL, True);
      if (!Contexts[i]) {
         Error("Unable to create GLX context");
      }

      if (!glXMakeCurrent(Dpy, Win, Contexts[i])) {
         Error("glXMakeCurrent failed");
      }

      InitContext();

      ContextEnabled[i] = GL_TRUE;
   }
}


static int DestroyFlag = 0;


static void
Redraw(void)
{
   int i, cleared = 0;

   for (i = 0; i < MAX_CONTEXTS; i++) {
      if (ContextEnabled[i]) {
         float xpos = i / (float) (MAX_CONTEXTS - 1);
         float ar;

         xpos = (xpos * 2.0) - 1.0;

         if (Win && !glXMakeCurrent(Dpy, Win, Contexts[i])) {
            Error("glXMakeCurrent failed");
         }

         glViewport(0, 0, WinWidth, WinHeight);
         ar = (float) WinWidth / (float) WinHeight;
         glMatrixMode(GL_PROJECTION);
         glLoadIdentity();
         glOrtho(-ar, ar, -1.0, 1.0, -1.0, 1.0);


         if (DestroyFlag) {
            printf("Destroy window\n");
            glXDestroyWindow(Dpy, Win);
            /*
            XDestroyWindow(Dpy, Win);
            */
            Win = 0;
         }

         glShadeModel(GL_FLAT);
         glClearColor(0.5, 0.5, 0.5, 1.0);
         if (!cleared) {
            glClear(GL_COLOR_BUFFER_BIT);
            cleared = 1;
         }

         if (DestroyFlag) {
            printf("clear/flush\n");
            glFlush();
         }

         glPushMatrix();
         glTranslatef(xpos, 0, 0);

         /* draw green triangle */
         glColor3f(0.0, 1.0, 0.0);
         glPushMatrix();
         glRotatef(Angle, 0, 0, 1);
         glScalef(0.5, 0.5, 1.0);
         glBegin(GL_TRIANGLES);
         glTexCoord2f(0.5, 1.0);   glVertex2f(0, 0.8);
         glTexCoord2f(0.0, 0.0);   glVertex2f(-0.8, -0.7);
         glTexCoord2f(1.0, 0.0);   glVertex2f(0.8, -0.7);
         glEnd();
         glPopMatrix();

         glPopMatrix();
         glFlush();
      }
   }

   if (Win)
      glXSwapBuffers(Dpy, Win);
}


static void
EventLoop(void)
{
   while (1) {
      /*while (XPending(Dpy) > 0)*/ {
         XEvent event;
         XNextEvent(Dpy, &event);
         printf("Event\n");

         switch (event.type) {
         case Expose:
            Redraw();
            break;
         case ConfigureNotify:
            WinWidth = event.xconfigure.width;
            WinHeight = event.xconfigure.height;
            break;
         case KeyPress:
            {
               char buf[100];
               KeySym keySym;
               XComposeStatus stat;
               XLookupString(&event.xkey, buf, sizeof(buf), &keySym, &stat);
               switch (keySym) {
               case XK_Escape:
                  exit(0);
                  break;
               case XK_a:
                  Angle += 2.0;
                  break;
               case XK_d:
                  DestroyFlag = 1;
#if 0
                  XDestroyWindow(Dpy, Win);
                  if (0) {
                     Redraw();
                  }
                  else {
                     glClear(GL_COLOR_BUFFER_BIT);
                     glFlush();
                  }
#endif
                  break;
               case XK_1:
                  ContextEnabled[0] = !ContextEnabled[0];
                  break;
               case XK_2:
                  ContextEnabled[1] = !ContextEnabled[1];
                  break;
#if 0
               case XK_d:
               case XK_D:
                  printf("Delete Texture in window %d\n", i);
                  glXMakeCurrent(Dpy, Win, Context);
                  glDeleteTextures(1, &TexObj);
                  break;
               case XK_u:
               case XK_U:
                  printf("Unbind Texture in window %d\n", i);
                  glXMakeCurrent(Dpy, Win, Context);
                  glBindTexture(GL_TEXTURE_2D, 0);
                  break;
#endif
               }
            }
            Redraw();
            break;
         default:
            /*no-op*/ ;
         }
      }

      Redraw();
      usleep(1);
   }
}




int
main(int argc, char *argv[])
{
   int i;

   for (i = 1; i < argc; i++) {
      if (strcmp(argv[i], "-display") == 0 && i < argc) {
         DisplayName = argv[i+1];
         i++;
      }
   }

   if (0) {
      printf("multictx: open N simultaneous glx windows\n");
      return 0;
   }

   Setup();

   EventLoop();

   return 0;
}
