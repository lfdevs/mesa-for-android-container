

#ifndef HW_RUNNER_PRIV_H
#define HW_RUNNER_PRIV_H

#include "hw_runner.h"
#include "kmod/pan_kmod.h"
#include <stdint.h>
#include <stdatomic.h>

#define HW_RUNNER_ARCH_DECLS(ver)                                              \
   void hw_runner_new_cmd_stream_v##ver(struct pan_kmod_dev *kdev,             \
                                        struct hw_runner_invocation_info *info,\
                                        struct hw_runner_layout_info *out);

HW_RUNNER_ARCH_DECLS(10);
HW_RUNNER_ARCH_DECLS(12);
HW_RUNNER_ARCH_DECLS(13);

#endif /* HW_RUNNER_PRIV_H */
