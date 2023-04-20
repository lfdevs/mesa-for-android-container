/*
 * Copyright 2017-2019 Lyude Paul
 * Copyright 2017-2019 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef __AGX_DECODE_H__
#define __AGX_DECODE_H__

#include "agx_bo.h"

void agxdecode_next_frame(void);

void agxdecode_close(void);

void agxdecode_cmdstream(unsigned cmdbuf_index, unsigned map_index,
                         bool verbose);

void agxdecode_dump_file_open(void);

void agxdecode_track_alloc(struct agx_bo *alloc);

void agxdecode_dump_mappings(unsigned map_index);

void agxdecode_track_free(struct agx_bo *bo);

#endif /* __AGX_DECODE_H__ */
