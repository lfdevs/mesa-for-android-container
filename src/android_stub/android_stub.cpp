#include <cutils/properties.h>
#include <sync/sync.h>
#include <hardware/hardware.h>
#include <android/log.h>
#include <backtrace/Backtrace.h>

extern "C" {

int property_get(const char* key, char* value, const char* default_value)
{
   return 0;
}

/* timeout in msecs */
int sync_wait(int fd, int timeout)
{
   return 0;
}

/* From hardware/hardware.h */

int hw_get_module(const char *id, const struct hw_module_t **module)
{
   return 0;
}

/* From android/log.h */

int __android_log_print(int prio, const char* tag, const char* fmt, ...)
{
   return 0;
}

int __android_log_vprint(int prio, const char* tag, const char* fmt, va_list ap)
{
   return 0;
}

}

/* From backtrace/Backtrace.h */

Backtrace*
Backtrace::Create(pid_t pid, pid_t tid, BacktraceMap* map)
{
   return NULL;
}

std::string
backtrace_map_t::Name() const
{
   return "";
}

