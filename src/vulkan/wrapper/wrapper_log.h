#ifndef WRAPPER_LOG_H
#define WRAPPER_LOG_H

#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>

#define WRAPPER_LOG_INFO (1ull << 0)
#define WRAPPER_LOG_ERROR (1ull << 1)
#define WRAPPER_LOG_SHADER (1ull << 2)
#define WRAPPER_LOG_VALIDATION (1ull << 3)
#define WRAPPER_LOG_BCN (1ull << 4)

struct wrapper_log {
	char *name;
	unsigned long long value;
};

#ifdef __cplusplus
extern "C" {
#endif

void
init_wrapper_logging(void);

void
dump_shader_code(const uint32_t *code, size_t size);

int
get_wrapper_log_level(const char *option);

void
write_to_logfile(const char *fmt, const char *level, ...);

#ifdef __cplusplus
}
#endif

VKAPI_ATTR VkBool32 VKAPI_CALL
wrapper_debug_utils_messenger(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                              VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                              const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
                              void *userData);

#define WRAPPER_LOG_LEVEL(s) (get_wrapper_log_level(#s))

#define WRAPPER_LOG(level, fmt, ...) \
do {\
   if (WRAPPER_LOG_LEVEL(level)) {\
      write_to_logfile(fmt, #level, ##__VA_ARGS__); \
   }\
} while (0)

#endif
