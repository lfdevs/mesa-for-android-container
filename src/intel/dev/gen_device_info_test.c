#undef NDEBUG

#include <stdint.h>
#include <assert.h>

#include "gen_device_info.h"

int
main(int argc, char *argv[])
{
   struct {
      uint32_t pci_id;
      const char *name;
   } chipsets[] = {
#undef CHIPSET
#define CHIPSET(id, family, family_str, str_name) { .pci_id = id, .name = str_name, },
#include "pci_ids/i965_pci_ids.h"
#include "pci_ids/iris_pci_ids.h"
   };

   for (uint32_t i = 0; i < ARRAY_SIZE(chipsets); i++) {
      struct gen_device_info devinfo = { 0, };

      assert(gen_get_device_info_from_pci_id(chipsets[i].pci_id, &devinfo));

      assert(devinfo.gen != 0);
      assert(devinfo.num_eu_per_subslice != 0);
      assert(devinfo.num_thread_per_eu != 0);
      assert(devinfo.timestamp_frequency != 0);
   }

   return 0;
}
