#include "nv_push.h"

#include "nv_device_info.h"

#include <inttypes.h>
#include "util/os_misc.h"

#include "drf.h"
#include "cl906f.h"
#include "nv_push_class_dump.h"

#ifndef NDEBUG
void
nv_push_validate(struct nv_push *push)
{
   uint32_t *cur = push->start;

   /* submitting empty push buffers is probably a bug */
   assert(push->end != push->start);

   /* make sure we don't overrun the bo */
   assert(push->end <= push->limit);

   /* parse all the headers to see if we get to buf->map */
   while (cur < push->end) {
      uint32_t hdr = *cur;
      uint32_t mthd = NVVAL_GET(hdr, NV906F, DMA, SEC_OP);

      switch (mthd) {
      case NV906F_DMA_SEC_OP_IMMD_DATA_METHOD:
         break;
      case NV906F_DMA_SEC_OP_INC_METHOD:
      case NV906F_DMA_SEC_OP_NON_INC_METHOD:
      case NV906F_DMA_SEC_OP_ONE_INC: {
         uint32_t count = NVVAL_GET(hdr, NV906F, DMA, METHOD_COUNT);
         assert(count);
         cur += count;
         break;
      }
      default:
         assert(!"unknown method found");
      }

      cur++;
      assert(cur <= push->end);
   }
}
#endif

void
vk_push_print(FILE *fp, const struct nv_push *push,
              const struct nv_device_info *devinfo)
{
   uint32_t *cur = push->start;
   uint16_t curr_subchans[8] = {0};
   curr_subchans[0] = devinfo->cls_eng3d;
   curr_subchans[1] = devinfo->cls_compute;
   curr_subchans[2] = devinfo->cls_m2mf;
   curr_subchans[3] = devinfo->cls_eng2d;
   curr_subchans[4] = devinfo->cls_copy;

   const bool print_offsets = true;

   while (cur < push->end) {
      uint32_t hdr = *cur;
      uint32_t type = NVVAL_GET(hdr, NV906F, DMA, SEC_OP);
      bool is_tert = type == NV906F_DMA_SEC_OP_GRP0_USE_TERT ||
                     type == NV906F_DMA_SEC_OP_GRP2_USE_TERT;
      uint32_t inc = 0;
      uint32_t count = is_tert ? (hdr >> 18) & 0x3ff
                               : NVVAL_GET(hdr, NV906F, DMA, METHOD_COUNT);
      uint32_t tert_op = NVVAL_GET(hdr, NV906F, DMA, TERT_OP);
      uint32_t subchan = NVVAL_GET(hdr, NV906F, DMA, METHOD_SUBCHANNEL);
      uint32_t mthd = NVVAL_GET(hdr, NV906F, DMA, METHOD_ADDRESS) << 2;
      uint32_t value = 0;
      bool is_immd = false;

      if (print_offsets)
         fprintf(fp, "[0x%08" PRIxPTR "] ", cur - push->start);

      if (is_tert && tert_op != 0) {
         fprintf(fp, "HDR %x subch N/A", hdr);
      } else {
         fprintf(fp, "HDR %x subch %i", hdr, subchan);
      }

      cur++;

      const char *mthd_name = "";

      switch (type) {
      case NV906F_DMA_SEC_OP_IMMD_DATA_METHOD:
         fprintf(fp, " IMMD\n");
         inc = 0;
         is_immd = true;
         value = NVVAL_GET(hdr, NV906F, DMA, IMMD_DATA);
         count = 1;
         break;
      case NV906F_DMA_SEC_OP_INC_METHOD:
         fprintf(fp, " NINC\n");
         inc = count;
         break;
      case NV906F_DMA_SEC_OP_GRP2_USE_TERT:
      case NV906F_DMA_SEC_OP_NON_INC_METHOD:
         fprintf(fp, " 0INC\n");
         inc = 0;
         break;
      case NV906F_DMA_SEC_OP_ONE_INC:
         fprintf(fp, " 1INC\n");
         inc = 1;
         break;
      case NV906F_DMA_SEC_OP_GRP0_USE_TERT:
         switch (tert_op) {
         case NV906F_DMA_TERT_OP_GRP0_INC_METHOD:
            fprintf(fp, " NINC\n");
            inc = count;
            break;
         case NV906F_DMA_TERT_OP_GRP0_SET_SUB_DEV_MASK:
            fprintf(fp, " SUB_DEVICE_OP\n");
            mthd_name = "SET_SUBDEVICE_MASK";
            mthd = tert_op;
            value = (hdr >> 4) & 0xfff;
            count = 1;
            is_immd = true;
            break;
         case NV906F_DMA_TERT_OP_GRP0_STORE_SUB_DEV_MASK:
            fprintf(fp, " SUB_DEVICE_OP\n");
            mthd_name = "STORE_SUBDEVICE_MASK";
            mthd = tert_op;
            value = (hdr >> 4) & 0xfff;
            count = 1;
            is_immd = true;
            break;
         case NV906F_DMA_TERT_OP_GRP0_USE_SUB_DEV_MASK:
            fprintf(fp, " SUB_DEVICE_OP\n");
            mthd_name = "USE_SUBDEVICE_MASK";
            mthd = tert_op;
            count = 1;
            break;
         }
         break;
      }

      while (count--) {
         if (!is_immd)
            value = *cur;

         if (mthd == 0) { /* SET_OBJECT */
            curr_subchans[subchan] = value & 0xffff;
         }
         int class_id = curr_subchans[subchan];

         /* If the sub channel is unbound, the expected behavior is to have it
          * routed to the GPFIFO class */
         if (class_id == 0)
            class_id = devinfo->cls_gpfifo;

         if (!is_tert)
            mthd_name = P_PARSE_NV_MTHD(class_id, mthd);

         fprintf(fp, "\tmthd %04x %s\n", mthd, mthd_name);
         P_DUMP_NV_MTHD_DATA(fp, class_id, mthd, value, "\t\t");

         if (!is_immd)
            cur++;

         if (inc) {
            inc--;
            mthd += 4;
         }
      }

      fprintf(fp, "\n");
   }
}
