#ifndef __WRAPPER_UTIL_H
#define __WRAPPER_UTIL_H

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CREATE_FOLDER(folder, mode) \
({ \
   struct stat sb; \
   if (stat(folder, &sb) != 0 || !S_ISDIR(sb.st_mode)) { \
      mode_t old_mask = umask(0); \
      mkdir(folder, (mode)); \
      umask(old_mask); \
      chmod(folder, (mode)); \
   } \
})

char *
get_executable_name(void);

#endif
