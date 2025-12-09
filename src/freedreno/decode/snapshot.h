/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its susidiaries.
 * SPDX-License-Identifier: MIT
 */

#ifndef __SNAPSHOT_H__
#define __SNAPSHOT_H__

/*
 * Based on kgsl_snapshot.h
 */

#include <stdint.h>
#include <string.h>

#include "util/macros.h"

/* High word is static, low word is snapshot version ID */
#define SNAPSHOT_MAGIC 0x504D0002

#define DEBUG_SECTION_SZ(_dwords) (((_dwords) * sizeof(unsigned int)) \
				   + sizeof(struct snapshot_debug))

/* GPU ID scheme:
 * [16:31] - core identifer (0x0002 for 2D or 0x0003 for 3D)
 * [00:16] - GPU specific identifier
 */

struct snapshot_header {
	uint32_t magic; /* Magic identifier */
	uint32_t gpuid; /* GPU ID - see above */
	/* Added in snapshot version 2 */
	uint32_t chipid; /* Chip ID from the GPU */
} PACKED;

/* Section header */
#define SNAPSHOT_SECTION_MAGIC 0xABCD

struct snapshot_section_header {
	uint16_t magic; /* Magic identifier */
	uint16_t id;    /* Type of section */
	uint32_t size;  /* Size of the section including this header */
} PACKED;

/* Section identifiers */
#define SNAPSHOT_SECTION_OS           0x0101
#define SNAPSHOT_SECTION_REGS         0x0201
#define SNAPSHOT_SECTION_REGS_V2      0x0202
#define SNAPSHOT_SECTION_RB_V2        0x0302
#define SNAPSHOT_SECTION_IB_V2        0x0402
#define SNAPSHOT_SECTION_INDEXED_REGS 0x0501
#define SNAPSHOT_SECTION_INDEXED_REGS_V2 0x0502
#define SNAPSHOT_SECTION_DEBUG        0x0901
#define SNAPSHOT_SECTION_DEBUGBUS     0x0A01
#define SNAPSHOT_SECTION_GPU_OBJECT_V2 0x0B02
#define SNAPSHOT_SECTION_MEMLIST_V2   0x0E02
#define SNAPSHOT_SECTION_SHADER       0x1201
#define SNAPSHOT_SECTION_SHADER_V2    0x1202
#define SNAPSHOT_SECTION_SHADER_V3    0x1203
#define SNAPSHOT_SECTION_MVC          0x1501
#define SNAPSHOT_SECTION_MVC_V2       0x1502
#define SNAPSHOT_SECTION_MVC_V3       0x1503
#define SNAPSHOT_SECTION_GMU_MEMORY   0x1701
#define SNAPSHOT_SECTION_SIDE_DEBUGBUS 0x1801
#define SNAPSHOT_SECTION_TRACE_BUFFER 0x1901
#define SNAPSHOT_SECTION_EVENTLOG     0x1A01

#define SNAPSHOT_SECTION_END          0xFFFF

/* OS sub-section header */
#define SNAPSHOT_OS_LINUX_V4          0x00000203

/* Linux OS specific information */
struct snapshot_linux_v4 {
	int osid;		/* subsection OS identifier */
	uint32_t seconds;		/* Unix timestamp for the snapshot */
	uint32_t power_flags;	/* Current power flags */
	uint32_t power_level;	/* Current power level */
	uint32_t power_interval_timeout;	/* Power interval timeout */
	uint32_t grpclk;		/* Current GP clock value */
	uint32_t busclk;		/* Current busclk value */
	uint64_t ptbase;		/* Current ptbase */
	uint64_t ptbase_lpac;	/* Current LPAC ptbase */
	uint32_t pid;		/* PID of the process that owns the PT */
	uint32_t pid_lpac;		/* PID of the LPAC process that owns the PT */
	uint32_t current_context;	/* ID of the current context */
	uint32_t current_context_lpac;	/* ID of the current LPAC context */
	uint32_t ctxtcount;	/* Number of contexts appended to section */
	unsigned char release[32];	/* kernel release */
	unsigned char version[32];	/* kernel version */
	unsigned char comm[16];		/* Name of the process that owns the PT */
	unsigned char comm_lpac[16];	/* Name of the LPAC process that owns the PT */
} PACKED;

/*
 * This structure contains a record of an active context.
 * These are appended one after another in the OS section below
 * the header above
 */
struct snapshot_linux_context_v2 {
	uint32_t id;			/* The context ID */
	uint32_t timestamp_queued;		/* The last queued timestamp */
	uint32_t timestamp_consumed;	/* The last timestamp consumed by HW */
	uint32_t timestamp_retired;	/* The last timestamp retired by HW */
};

/* Ringbuffer sub-section header */
struct snapshot_rb_v2 {
	int start;  /* dword at the start of the dump */
	int end;    /* dword at the end of the dump */
	int rbsize; /* Size (in dwords) of the ringbuffer */
	int wptr;   /* Current index of the CPU write pointer */
	int rptr;   /* Current index of the GPU read pointer */
	int count;  /* Number of dwords in the dump */
	uint32_t timestamp_queued; /* The last queued timestamp */
	uint32_t timestamp_retired; /* The last timestamp retired by HW */
	uint64_t gpuaddr; /* The GPU address of the ringbuffer */
	uint32_t id; /* Ringbuffer identifier */
} PACKED;

/* Replay or Memory list section, both sections have same header */
struct snapshot_mem_list_v2 {
	/*
	 * Number of IBs to replay for replay section or
	 * number of memory list entries for mem list section
	 */
	int num_entries;
	/* Pagetable base to which the replay IBs or memory entries belong */
	uint64_t ptbase;
} PACKED;

/* Indirect buffer sub-section header (v2) */
struct snapshot_ib_v2 {
	uint64_t gpuaddr; /* GPU address of the IB */
	uint64_t ptbase;  /* Base for the pagetable the GPU address is valid in */
	uint64_t size;    /* Size of the IB */
} PACKED;

/* GMU memory ID's */
#define SNAPSHOT_GMU_MEM_UNKNOWN	0x00
#define SNAPSHOT_GMU_MEM_HFI		0x01
#define SNAPSHOT_GMU_MEM_LOG		0x02
#define SNAPSHOT_GMU_MEM_BWTABLE	0x03
#define SNAPSHOT_GMU_MEM_DEBUG		0x04
#define SNAPSHOT_GMU_MEM_BIN_BLOCK	0x05
#define SNAPSHOT_GMU_MEM_CONTEXT_QUEUE	0x06
#define SNAPSHOT_GMU_MEM_HW_FENCE	0x07
#define SNAPSHOT_GMU_MEM_WARMBOOT	0x08
#define SNAPSHOT_GMU_MEM_VRB		0x09
#define SNAPSHOT_GMU_MEM_TRACE		0x0a

/* GMU memory section data */
struct snapshot_gmu_mem {
	int type;
	uint64_t hostaddr;
	uint64_t gmuaddr;
	uint64_t gpuaddr;
} PACKED;

/* Register sub-section header */
struct snapshot_regs {
	uint32_t count; /* Number of register pairs in the section */
} PACKED;

/* Indexed register sub-section header */
struct snapshot_indexed_regs {
	uint32_t index_reg; /* Offset of the index register for this section */
	uint32_t data_reg;  /* Offset of the data register for this section */
	int start;     /* Starting index */
	int count;     /* Number of dwords in the data */
} PACKED;

struct snapshot_indexed_regs_v2 {
	uint32_t index_reg;	/* Offset of the index register for this section */
	uint32_t data_reg;	/* Offset of the data register for this section */
	uint32_t start;	/* Starting index */
	uint32_t count;	/* Number of dwords in the data */
	uint32_t pipe_id;	/* Id of pipe, BV, Br etc */
	uint32_t slice_id;	/* Slice ID to be dumped */
} PACKED;

/* MVC register sub-section header */
struct snapshot_mvc_regs {
	int ctxt_id;
	int cluster_id;
} PACKED;

struct snapshot_mvc_regs_v2 {
	int ctxt_id;
	int cluster_id;
	int pipe_id;
	int location_id;
} PACKED;

struct snapshot_mvc_regs_v3 {
	uint32_t ctxt_id;
	uint32_t cluster_id;
	uint32_t pipe_id;
	uint32_t location_id;
	uint32_t slice_id;
	uint32_t sp_id;
	uint32_t usptp_id;
} PACKED;

/* Debug data sub-section header */

/* A5XX debug sections */
#define SNAPSHOT_DEBUG_CP_MEQ     7
#define SNAPSHOT_DEBUG_CP_PM4_RAM 8
#define SNAPSHOT_DEBUG_CP_PFP_RAM 9
#define SNAPSHOT_DEBUG_CP_ROQ     10
#define SNAPSHOT_DEBUG_SHADER_MEMORY 11
#define SNAPSHOT_DEBUG_CP_MERCIU 12
#define SNAPSHOT_DEBUG_SQE_VERSION 14

/* GMU Version information */
#define SNAPSHOT_DEBUG_GMU_CORE_VERSION 15
#define SNAPSHOT_DEBUG_GMU_CORE_DEV_VERSION 16
#define SNAPSHOT_DEBUG_GMU_PWR_VERSION 17
#define SNAPSHOT_DEBUG_GMU_PWR_DEV_VERSION 18
#define SNAPSHOT_DEBUG_GMU_HFI_VERSION 19
#define SNAPSHOT_DEBUG_AQE_VERSION 20

struct snapshot_debug {
	int type;    /* Type identifier for the attached tata */
	int size;   /* Size of the section in dwords */
} PACKED;

struct snapshot_debugbus {
	int id;	   /* Debug bus ID */
	int count; /* Number of dwords in the dump */
} PACKED;

struct snapshot_side_debugbus {
	int id;   /* Debug bus ID */
	int size; /* Number of dwords in the dump */
	int valid_data;  /* Mask of valid bits of the side debugbus */
} PACKED;

struct snapshot_shader {
	int type;  /* SP/TP statetype */
	int index; /* SP/TP index */
	int size;  /* Number of dwords in the dump */
} PACKED;

struct snapshot_shader_v2 {
	int type;  /* SP/TP statetype */
	int index; /* SP/TP index */
	int usptp; /* USPTP index */
	int pipe_id; /* Pipe id */
	int location; /* Location value */
	uint32_t size;  /* Number of dwords in the dump */
} PACKED;

struct snapshot_shader_v3 {
	uint32_t type;  /* SP/TP statetype */
	uint32_t slice_id; /* Slice ID */
	uint32_t sp_index; /* SP/TP index */
	uint32_t usptp; /* USPTP index */
	uint32_t pipe_id; /* Pipe id */
	uint32_t location; /* Location value */
	uint32_t ctxt_id; /* Context ID */
	uint32_t size;  /* Number of dwords in the dump */
} PACKED;

#define TRACE_BUF_NUM_SIG 4

/**
 * enum trace_buffer_source - Bits to identify the source block of trace buffer information
 * GX_DBGC : Signals captured from GX block
 * CX_DBGC : Signals captured from CX block
 */
enum trace_buffer_source {
	GX_DBGC = 1,
	CX_DBGC = 2,
};

/**
 * snapshot_trace_buffer: Header Information for the tracebuffer in snapshot.
 */
struct snapshot_trace_buffer {
	/** @dbgc_ctrl: Identify source for trace */
	uint16_t dbgc_ctrl;
	/** @dbgc_ctrl: Identify source for trace */
	uint16_t segment;
	/** @granularity: The total number of segments in each packet */
	uint16_t granularity;
	/** @ping_blk: Signal block */
	uint16_t ping_blk[TRACE_BUF_NUM_SIG];
	/** @ping_idx: Signal Index */
	uint16_t ping_idx[TRACE_BUF_NUM_SIG];
	/** @size: Number of bytes in the dump */
	uint32_t size;
} PACKED;

#define SNAPSHOT_GPU_OBJECT_SHADER  1
#define SNAPSHOT_GPU_OBJECT_IB      2
#define SNAPSHOT_GPU_OBJECT_GENERIC 3
#define SNAPSHOT_GPU_OBJECT_DRAW    4
#define SNAPSHOT_GPU_OBJECT_GLOBAL  5

struct snapshot_gpu_object_v2 {
	int type;      /* Type of GPU object */
	uint64_t gpuaddr; /* GPU address of the object */
	uint64_t ptbase;  /* Base for the pagetable the GPU address is valid in */
	uint64_t size;    /* Size of the object (in dwords) */
} PACKED;

struct snapshot_eventlog {
	/** @type: Type of the event log buffer */
	uint16_t type;
	/** @version: Version of the event log buffer */
	uint16_t version;
	/** @size: Size of the eventlog buffer in bytes */
	uint32_t size;
} PACKED;

struct snapshot_gmu_version {
	/** @type: Type of the GMU version buffer */
	uint32_t type;
	/** @value: GMU FW version value */
	uint32_t value;
} PACKED;

/*
 * Helpers to write snapshots below here:
 */

static FILE *snapshot;
static uint64_t ptbase = 0x43210000;  /* We don't always have a real ttbr0, so fake it */

static inline void
snapshot_write(void *data, size_t sz)
{
   fwrite(data, sz, 1, snapshot);
}

static inline void
snapshot_write_sect_header(uint16_t sect_id, size_t sz)
{
   struct snapshot_section_header sect_hdr = {
      .magic = SNAPSHOT_SECTION_MAGIC,
      .id = sect_id,
      .size = sizeof(sect_hdr) + sz,
   };

   snapshot_write(&sect_hdr, sizeof(sect_hdr));
}

static inline void
snapshot_write_header(uint32_t chip_id)
{
   if (!snapshot)
      return;

   struct snapshot_header hdr = {
      .magic = SNAPSHOT_MAGIC,
      .gpuid = ~0,
      .chipid = chip_id,
   };

   snapshot_write(&hdr, sizeof(hdr));
}

static struct snapshot_linux_v4 snapshot_linux = {
   .osid = SNAPSHOT_OS_LINUX_V4,
};
static struct snapshot_linux_context_v2 snapshot_contexts[16];
static struct snapshot_rb_v2 snapshot_rb[16];

static inline void
snapshot_gmu_mem(int type, uint64_t iova, uint32_t *buf, uint32_t size)
{
   if (!snapshot)
      return;

   struct snapshot_gmu_mem gmu_mem = {
      .type = type,
      .gmuaddr = iova,
      .gpuaddr = 0,
      .hostaddr = 0,
   };

   snapshot_write_sect_header(
      SNAPSHOT_SECTION_GMU_MEMORY,
      sizeof(gmu_mem) + size
   );
   snapshot_write(&gmu_mem, sizeof(gmu_mem));
   snapshot_write(buf, size);
}

/* Not directly part of the snapshot, but used to generate the register
 * snapshot sections:
 */

#define MAX_REGS 2500

struct reg_buf {
   uint32_t count;
   struct {
      uint32_t offset;
      uint32_t value;
   } regs[10000];
};

static struct reg_buf reg_buf;

static inline void
snapshot_registers(void)
{
   uint32_t count = reg_buf.count;

   if (!count)
      return;

   reg_buf.count = 0;

   if (!snapshot)
      return;

   struct snapshot_regs regs = {
      .count = count,
   };

   snapshot_write_sect_header(
      SNAPSHOT_SECTION_REGS,
      sizeof(regs) + (8 * count)
   );
   snapshot_write(&regs, sizeof(regs));
   snapshot_write(reg_buf.regs, 8 * count);
}

static inline void
snapshot_indexed_regs(const char *name, uint32_t *regs, uint32_t sizedwords)
{
   if (!snapshot)
      return;

   char addr_reg[64];
   char data_reg[64];

   strcpy(addr_reg, name);
   strcpy(&addr_reg[strlen(name)], "_ADDR");

   strcpy(data_reg, name);
   strcpy(&data_reg[strlen(name)], "_DATA");

   /* Todo, 8xx should use snapshot_indexed_regs_v2.. which needs more
    * info from kernel:
    */
   struct snapshot_indexed_regs index_regs = {
      .index_reg = regbase(addr_reg),
      .data_reg  = regbase(data_reg),
      .count = sizedwords,
   };

   snapshot_write_sect_header(
      SNAPSHOT_SECTION_INDEXED_REGS,
      sizeof(index_regs) + (4 * sizedwords)
   );
   snapshot_write(&index_regs, sizeof(index_regs));
   snapshot_write(regs, 4 * sizedwords);
}

static inline void
snapshot_cluster_regs(uint32_t pipe_id, uint32_t cluster_id, int context,
                      uint32_t location)
{
   uint32_t count = reg_buf.count;

   if (!count)
      return;

   reg_buf.count = 0;

   if (!snapshot)
      return;

   /* TODO, 8xx should use snapshot_mvc_regs_v3: */
   struct snapshot_mvc_regs_v2 cluster_regs = {
      .ctxt_id = context,
      .cluster_id = cluster_id,
      .pipe_id = pipe_id,
      .location_id = location,
   };

   snapshot_write_sect_header(
      SNAPSHOT_SECTION_MVC_V2,
      sizeof(cluster_regs) + (8 * count)
   );
   snapshot_write(&cluster_regs, sizeof(cluster_regs));
   snapshot_write(reg_buf.regs, 8 * count);
}

static inline void
snapshot_debugbus(uint32_t block, uint32_t *buf, uint32_t sizedwords)
{
   if (!snapshot)
      return;

   struct snapshot_debugbus debugbus = {
      .id = block,
      .count = sizedwords,
   };

   snapshot_write_sect_header(
      SNAPSHOT_SECTION_DEBUGBUS,
      sizeof(debugbus) + (4 * sizedwords)
   );
   snapshot_write(&debugbus, sizeof(debugbus));
   snapshot_write(buf, 4 * sizedwords);
}

static inline void
snapshot_shader_block(uint32_t type, uint32_t pipe, int sp, int usptp,
                      int location, uint32_t *buf, uint32_t sizedwords)
{
   if (!snapshot)
      return;

   /* TODO, 8xx should use snapshot_shader_v3: */
   struct snapshot_shader_v2 shader_block = {
      .type = type,
      .index = sp,
      .usptp = usptp,
      .pipe_id = pipe,
      .location = location,
      .size = sizedwords,
   };

   snapshot_write_sect_header(
      SNAPSHOT_SECTION_SHADER_V2,
      sizeof(shader_block) + (4 * sizedwords)
   );
   snapshot_write(&shader_block, sizeof(shader_block));
   snapshot_write(buf, 4 * sizedwords);
}

static inline void
snapshot_gpu_object(uint64_t gpuaddr, uint32_t size, uint32_t *buf)
{
   if (!snapshot)
      return;

   struct snapshot_gpu_object_v2 gpu_object = {
      .type = SNAPSHOT_GPU_OBJECT_GENERIC,
      .gpuaddr = gpuaddr,
      .ptbase = 0,  /* We don't have this.. use magic value? */
      .size = size / 4, /* dwords */
   };

   snapshot_write_sect_header(
      SNAPSHOT_SECTION_GPU_OBJECT_V2,
      sizeof(gpu_object) + size
   );
   snapshot_write(&gpu_object, sizeof(gpu_object));
   snapshot_write(buf, size);
}

static struct {
   struct snapshot_ib_v2 ib;
   uint32_t *dwords;
} ibs[512];
static unsigned nibs;

static inline void
snapshot_ib(uint64_t gpuaddr, uint32_t *dwords, uint32_t sizedwords)
{
   int idx;

   if (!snapshot || !dwords)
      return;

   for (idx = 0; idx < nibs; idx++) {
      if (ibs[idx].ib.gpuaddr == gpuaddr) {
         ibs[idx].ib.size = MAX2(ibs[idx].ib.size, sizedwords);
         return;
      }
   }

   assert(idx < ARRAY_SIZE(ibs));
   ibs[idx].ib.gpuaddr = gpuaddr;
   ibs[idx].ib.size = sizedwords;
   ibs[idx].ib.ptbase = ptbase;
   ibs[idx].dwords = dwords;

   nibs++;
}

static inline void
do_snapshot(void)
{
   if (!snapshot)
      return;

   snapshot_write_sect_header(
      SNAPSHOT_SECTION_OS,
      sizeof(snapshot_linux) + (snapshot_linux.ctxtcount * sizeof(snapshot_contexts[0]))
   );
   snapshot_write(&snapshot_linux, sizeof(snapshot_linux));
   snapshot_write(snapshot_contexts, snapshot_linux.ctxtcount * sizeof(snapshot_contexts[0]));

   for (unsigned i = 0; i < snapshot_linux.ctxtcount; i++) {
      snapshot_write_sect_header(
         SNAPSHOT_SECTION_RB_V2,
         sizeof(snapshot_rb[i]) + (4 * snapshot_rb[i].rbsize)
      );
      snapshot_write(&snapshot_rb[i], sizeof(snapshot_rb[i]));

      int id = snapshot_rb[i].id;
      void *buf = ringbuffers[id].buf;

      snapshot_write(buf, snapshot_rb[i].rbsize * 4);
   }

   for (unsigned i = 0; i < nibs; i++) {
      snapshot_write_sect_header(
         SNAPSHOT_SECTION_IB_V2,
         sizeof(ibs[i].ib) + (4 * ibs[i].ib.size)
      );
      snapshot_write(&ibs[i].ib, sizeof(ibs[i].ib));
      snapshot_write(ibs[i].dwords, ibs[i].ib.size * 4);

   }

   snapshot_write_sect_header(SNAPSHOT_SECTION_END, 0);
}

#endif /* __SNAPSHOT_H__ */
