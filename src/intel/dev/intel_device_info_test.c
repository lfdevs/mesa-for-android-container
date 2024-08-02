#undef NDEBUG

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include "intel_device_info.h"
#include "intel_device_info_test.h"

int
main(int argc, char *argv[])
{
   struct {
      uint32_t pci_id;
      const char *name;
   } chipsets[] = {
#undef CHIPSET
#define CHIPSET(id, family, family_str, str_name, ...) { .pci_id = id, .name = str_name, },
#include "pci_ids/iris_pci_ids.h"
#include "pci_ids/crocus_pci_ids.h"
   };

   for (uint32_t i = 0; i < ARRAY_SIZE(chipsets); i++) {
      struct intel_device_info devinfo = { 0, };
      char force_probe[10];
      int len = snprintf(force_probe, sizeof force_probe, "%x",
                         chipsets[i].pci_id);
      assert(len < sizeof force_probe);

      setenv("INTEL_FORCE_PROBE", force_probe, 1);
      assert(intel_get_device_info_from_pci_id(chipsets[i].pci_id, &devinfo));

      verify_device_info(&devinfo);
   }

   return 0;
}
