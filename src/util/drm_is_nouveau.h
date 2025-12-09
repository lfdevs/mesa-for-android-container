#ifndef DRM_IS_NOUVEAU
#define DRM_IS_NOUVEAU

#include "util/libdrm.h"

static inline bool
drm_fd_is_nouveau(int fd)
{
   drmVersionPtr ver = drmGetVersion(fd);
   if (!ver) {
      return false;
   }
   const bool is_nouveau = !strncmp("nouveau", ver->name, ver->name_len);
   drmFreeVersion(ver);
   return is_nouveau;
}

#endif
