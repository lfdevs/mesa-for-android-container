#include <hardware/hardware.h>
#include <errno.h>

extern "C" {

int hw_get_module(const char *id, const struct hw_module_t **module)
{
   if (module)
      *module = nullptr;
   return -ENOENT;
}

}
