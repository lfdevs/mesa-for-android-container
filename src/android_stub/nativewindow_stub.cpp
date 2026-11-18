#include <vndk/window.h>
#include <vndk/hardware_buffer.h>

extern "C" {

AHardwareBuffer *
ANativeWindowBuffer_getHardwareBuffer(ANativeWindowBuffer *anwb)
{
   return nullptr;
}

void
AHardwareBuffer_acquire(AHardwareBuffer *buffer)
{
}

void
AHardwareBuffer_release(AHardwareBuffer *buffer)
{
}

void
AHardwareBuffer_describe(const AHardwareBuffer *buffer,
                         AHardwareBuffer_Desc *outDesc)
{
}

int
AHardwareBuffer_allocate(const AHardwareBuffer_Desc *desc,
                         AHardwareBuffer **outBuffer)
{
   return 0;
}

int
AHardwareBuffer_isSupported(const AHardwareBuffer_Desc* desc)
{
   return 0;
}

const native_handle_t *
AHardwareBuffer_getNativeHandle(const AHardwareBuffer *buffer)
{
   return NULL;
}

void
ANativeWindow_acquire(ANativeWindow *window)
{
}

void
ANativeWindow_release(ANativeWindow *window)
{
}

int32_t
ANativeWindow_getFormat(ANativeWindow *window)
{
   return 0;
}

int
ANativeWindow_setSwapInterval(ANativeWindow *window, int interval)
{
   return 0;
}

int
ANativeWindow_query(const ANativeWindow *window,
                    ANativeWindowQuery query,
                    int *value)
{
   return 0;
}

int
ANativeWindow_dequeueBuffer(ANativeWindow *window,
                            ANativeWindowBuffer **buffer,
                            int *fenceFd)
{
   return 0;
}

int
ANativeWindow_queueBuffer(ANativeWindow *window,
                          ANativeWindowBuffer *buffer,
                          int fenceFd)
{
   return 0;
}

int ANativeWindow_cancelBuffer(ANativeWindow* window,
                               ANativeWindowBuffer* buffer,
                               int fenceFd) {
   return 0;
}

int
ANativeWindow_setUsage(ANativeWindow *window, uint64_t usage)
{
   return 0;
}

int
ANativeWindow_setSharedBufferMode(ANativeWindow *window,
                                  bool sharedBufferMode)
{
   return 0;
}

int32_t ANativeWindow_getWidth(ANativeWindow *window) { return 0; }
int32_t ANativeWindow_getHeight(ANativeWindow *window) { return 0; }
int32_t ANativeWindow_setBuffersGeometry(ANativeWindow *window, int32_t width, int32_t height, int32_t format) { return 0; }
int32_t ANativeWindow_lock(ANativeWindow *window, ANativeWindow_Buffer *outBuffer, ARect *inOutDirtyBounds) { return 0; }
int32_t ANativeWindow_unlockAndPost(ANativeWindow *window) { return 0; }
int32_t AHardwareBuffer_lock(AHardwareBuffer *buffer, uint64_t usage, int32_t fence, const ARect *rect, void **outVirtualAddress) { return 0; }
int32_t AHardwareBuffer_unlock(AHardwareBuffer *buffer, int32_t *fence) { return 0; }
}
