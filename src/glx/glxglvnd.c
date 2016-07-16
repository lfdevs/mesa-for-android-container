#include <string.h>
#include <X11/Xlib.h>

#include "glvnd/libglxabi.h"

#include "glxglvnd.h"


static Bool __glXGLVNDIsScreenSupported(Display *dpy, int screen)
{
    /* TODO: Think of a better heuristic... */
    return True;
}

static void *__glXGLVNDGetProcAddress(const GLubyte *procName)
{
    return glXGetProcAddressARB(procName);
}

static unsigned FindGLXFunction(const GLubyte *name)
{
    unsigned first = 0;
    unsigned last = DI_FUNCTION_COUNT - 1;

    while (first <= last) {
        unsigned middle = (first + last) / 2;
        int comp = strcmp((const char *) name,
                          __glXDispatchTableStrings[middle]);

        if (comp < 0)
            first = middle + 1;
        else if (comp > 0)
            last = middle - 1;
        else
            return middle;
    }

    /* Just point to the dummy entry at the end of the respective table */
    return DI_FUNCTION_COUNT;
}

static void *__glXGLVNDGetDispatchAddress(const GLubyte *procName)
{
    unsigned internalIndex = FindGLXFunction(procName);

    return __glXDispatchFunctions[internalIndex];
}

static void __glXGLVNDSetDispatchIndex(const GLubyte *procName, int index)
{
    unsigned internalIndex = FindGLXFunction(procName);

    __glXDispatchTableIndices[internalIndex] = index;
}

_X_EXPORT Bool __glx_Main(uint32_t version, const __GLXapiExports *exports,
                          __GLXvendorInfo *vendor, __GLXapiImports *imports)
{
    static Bool initDone = False;

    if (GLX_VENDOR_ABI_GET_MAJOR_VERSION(version) !=
        GLX_VENDOR_ABI_MAJOR_VERSION ||
        GLX_VENDOR_ABI_GET_MINOR_VERSION(version) <
        GLX_VENDOR_ABI_MINOR_VERSION)
        return False;

    if (!initDone) {
        initDone = True;
        __glXGLVNDAPIExports = exports;

        imports->isScreenSupported = __glXGLVNDIsScreenSupported;
        imports->getProcAddress = __glXGLVNDGetProcAddress;
        imports->getDispatchAddress = __glXGLVNDGetDispatchAddress;
        imports->setDispatchIndex = __glXGLVNDSetDispatchIndex;
        imports->notifyError = NULL;
        imports->isPatchSupported = NULL;
        imports->initiatePatch = NULL;
    }

    return True;
}
