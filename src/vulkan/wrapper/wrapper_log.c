#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "wrapper_log.h"
#include "wrapper_util.h"

#define PATH_MAX_SIZE 1024
#define WRAPPER_DIR "/sdcard/wrapper"
#define WRAPPER_SHADER_LOG_PATH WRAPPER_DIR "/shaders"
#define WRAPPER_LOG_PATH WRAPPER_DIR "/logs"
#define WRAPPER_VALIDATION_LOG_PATH WRAPPER_DIR "/validation"

static struct wrapper_log wrapper_log_options[] = {
	{"info", WRAPPER_LOG_INFO},
	{"error", WRAPPER_LOG_ERROR},
	{"shader", WRAPPER_LOG_SHADER},
	{"validation", WRAPPER_LOG_VALIDATION},
	{"bcn", WRAPPER_LOG_BCN},
	{NULL, 0}
};

uint64_t wrapper_log_mask;

static void get_formatted_date_time(char *buf, size_t length)
{
   time_t rawtime = time(NULL);
   struct tm *ptm = localtime(&rawtime);

   strftime(buf, 256, "%F_%H-%M-%S", ptm);
}

char *get_executable_name() {
   char *path = malloc(PATH_MAX);

   int fd = open("/proc/self/cmdline", O_RDONLY);

   if (fd != -1) {
      read(fd, path, PATH_MAX_SIZE);
      char *ptr = strrchr(path, '/');
      if (ptr)
         path = ptr + 1;
      ptr = strrchr(path, '\\');
      if (ptr)
         path = ptr + 1;
      close(fd);
   }

   return path;
}

static unsigned long long get_debug_flag(const char *option) {
   int index = 0;

   while (wrapper_log_options[index].name != NULL) {
      if (!strcmp(wrapper_log_options[index].name, option))
         return wrapper_log_options[index].value;

      index++;
   }

   return 0;
}

static void parse_wrapper_debug_str(char *wrapper_log_level_env) {
   if (!wrapper_log_level_env) {
      wrapper_log_mask = 0;
      return;
   }

   char *option = strtok(wrapper_log_level_env, ",");

   while (option != NULL) {
      wrapper_log_mask |= get_debug_flag(option);
      option = strtok(NULL, ",");
   }
}

int get_wrapper_log_level(const char *option) {
    uint64_t flag = get_debug_flag(option);

    if (wrapper_log_mask & flag)
       return 1;

    return 0;
}

void write_to_logfile(const char *fmt, const char *level, ...)  {
   static FILE *wrapper_log_file = NULL;
   va_list va_args;

   va_start(va_args, level);

   if (!wrapper_log_file) {
      char *wrapper_log_filename = getenv("WRAPPER_LOG_FILE");
      char date[256];

      get_formatted_date_time(date, 256);

      if (!wrapper_log_filename)
         asprintf(&wrapper_log_filename, "%s/%s_%s", WRAPPER_LOG_PATH, get_executable_name(), date);

      if (!strcmp("stdout", wrapper_log_filename)) {
         wrapper_log_file = stdout;
       }
       else {
         wrapper_log_file = fopen(wrapper_log_filename, "w");
       }
   }

   if (wrapper_log_file) {
      fprintf(wrapper_log_file, "[%s]: ", level);
      vfprintf(wrapper_log_file, fmt, va_args);
      fprintf(wrapper_log_file, "\n");
      fflush(wrapper_log_file);
   }

   va_end(va_args);
}

void init_wrapper_logging()
{
   char *wrapper_log_level_env = getenv("WRAPPER_LOG_LEVEL");
   parse_wrapper_debug_str(wrapper_log_level_env);

   CREATE_FOLDER(WRAPPER_DIR, 0770);
   CREATE_FOLDER(WRAPPER_LOG_PATH, 0770);
   CREATE_FOLDER(WRAPPER_SHADER_LOG_PATH, 0770);
   CREATE_FOLDER(WRAPPER_VALIDATION_LOG_PATH, 0770);
}

void dump_shader_code(const uint32_t *code, size_t size) {
   char *file;
   static int index = 0;

   asprintf(&file, "%s/%s_shader_%d.spv", WRAPPER_SHADER_LOG_PATH, get_executable_name(), index);

   FILE *fp = fopen(file, "wb");
   if (fp) {
      fwrite(code, 1, size, fp);
      fclose(fp);
   }

   index++;
}

VKAPI_ATTR VkBool32 VKAPI_CALL
wrapper_debug_utils_messenger(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                              VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                              const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
                              void *userData)
{
   static FILE *vvl_log_file = NULL;
   const char* messageIdName = callbackData->pMessageIdName;
   int32_t messageIdNumber = callbackData->messageIdNumber;
   const char* message = callbackData->pMessage;

   if (!vvl_log_file) {
       char date[256];
       char *vvl_log_filename;

       get_formatted_date_time(date, 256);

       asprintf(&vvl_log_filename, "%s/%s_%s", WRAPPER_VALIDATION_LOG_PATH, get_executable_name(), date);
       vvl_log_file = fopen(vvl_log_filename, "w");
   }

   if (vvl_log_file) {
      fprintf(vvl_log_file, "[%s] Code %i : %s\n", messageIdName, messageIdNumber, message);
      fflush(vvl_log_file);
   }

   return 0;
}
