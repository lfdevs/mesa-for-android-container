#include <GL/gl.h>
#include <GL/glx.h>
#include <stdio.h>

typedef struct _TGLWindow
{
    Display *dpy;
    GLXContext cx;
    XVisualInfo *vi;
    Window win;
    GLXWindow  glxWin;
    GLXFBConfig *fbConfigs;
    int width;
    int height;
} TGLWindow;

static int tSingleBuffer_1_2[] = {GLX_RGBA,
                                    GLX_RED_SIZE,       1,
                                    GLX_GREEN_SIZE,     1,
                                    GLX_BLUE_SIZE,      1,
                                    None};
static int tDoubleBuffer_1_2[] = {GLX_RGBA,
                                    GLX_DOUBLEBUFFER,
                                    GLX_RED_SIZE,       1,
                                    GLX_GREEN_SIZE,     1,
                                    GLX_BLUE_SIZE,      1,
                                    None};


static Bool WaitForNotify(Display *d, XEvent *e, char *arg) {
    if (e->type == CreateNotify)
    {
        printf("Window was successfully created.\n");
    }
    return (e->type == MapNotify) && (e->xmap.window == (Window)arg);
}
TGLWindow tWnd;

int main() {
   int errBase = 0;
   int evtBase = 0;
   GLboolean bStatus;
   tWnd.dpy = NULL;
   tWnd.vi = NULL;
   tWnd.fbConfigs = NULL;
   tWnd.cx = 0;
   tWnd.height = 600;
   tWnd.width = 800;

   /* get a connection */
   tWnd.dpy = XOpenDisplay(0);

   if (!tWnd.dpy)
   {
        printf ("XOpenDisplay () returned NULL.\n");
        return -1;
   }
    if (glXQueryExtension(tWnd.dpy, &errBase, &evtBase) == GL_FALSE)
    {
        printf("The GLX extension is not supported by the server.\n");
        printf("error base is %d, event base is %d.\n", errBase, evtBase);
        return -1;
    }

    Colormap cmap;
    XSetWindowAttributes swa;
    XEvent event;
    int *attrList = tSingleBuffer_1_2;
    tWnd.vi = glXChooseVisual(tWnd.dpy, DefaultScreen(tWnd.dpy), attrList);
    if (tWnd.vi == NULL)
    {
        printf("glXChooseVisual() returned NULL\n");
        return -1;
    }

    /* create a GLX context */
    tWnd.cx = glXCreateContext(tWnd.dpy, tWnd.vi, 0, GL_TRUE);

    /* create a color map */
    cmap = XCreateColormap(tWnd.dpy, RootWindow(tWnd.dpy, tWnd.vi->screen),
                tWnd.vi->visual, AllocNone);

    /* create a window */
    swa.colormap = cmap;
    swa.border_pixel = 0;
    swa.event_mask = StructureNotifyMask;
    tWnd.win = XCreateWindow(tWnd.dpy, RootWindow(tWnd.dpy, tWnd.vi->screen),
0,
                          0, tWnd.width, tWnd.height,
                          0, tWnd.vi->depth, InputOutput,tWnd.vi->visual,
                          CWBorderPixel|CWColormap|CWEventMask,
                          &swa);
    XMapWindow(tWnd.dpy, tWnd.win);
    XIfEvent(tWnd.dpy, &event, WaitForNotify, (char*)tWnd.win);

    /* connect the context to the window */
    glXMakeCurrent(tWnd.dpy, tWnd.win, tWnd.cx);


    GLint params[] = {10, 10300, 14230};

    GLint retParams[4];

    glColor3i (params[0], params[1], params[2]);

    glGetIntegerv (GL_CURRENT_COLOR, retParams);

    printf ("After invocation of glColor3i(%d, %d, %d) interface, the current "
        "color was set to (%d, %d, %d, %d)\n", params[0], params[1], params[2], 
        retParams[0], retParams[1], retParams[2], retParams[3]);

    GLfloat c[4];
    glGetFloatv (GL_CURRENT_COLOR, c);
    printf("color %f %f %f %f\n", c[0], c[1], c[2], c[3]);

    return 1;
}
