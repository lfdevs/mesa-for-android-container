/*
 * Copyright 2008 Corbin Simpson <MostAwesomeDude@gmail.com>
 * Copyright 2010 Marek Olšák <maraeo@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE. */

#ifndef RADEON_WINSYS_H
#define RADEON_WINSYS_H

/* The public winsys interface header for the radeon driver. */

#include "pipebuffer/pb_buffer.h"

#include "amd/common/amd_family.h"

#define RADEON_FLUSH_ASYNC		(1 << 0)
#define RADEON_FLUSH_END_OF_FRAME       (1 << 1)

/* Tiling flags. */
enum radeon_bo_layout {
    RADEON_LAYOUT_LINEAR = 0,
    RADEON_LAYOUT_TILED,
    RADEON_LAYOUT_SQUARETILED,

    RADEON_LAYOUT_UNKNOWN
};

enum radeon_bo_domain { /* bitfield */
    RADEON_DOMAIN_GTT  = 2,
    RADEON_DOMAIN_VRAM = 4,
    RADEON_DOMAIN_VRAM_GTT = RADEON_DOMAIN_VRAM | RADEON_DOMAIN_GTT
};

enum radeon_bo_flag { /* bitfield */
    RADEON_FLAG_GTT_WC =        (1 << 0),
    RADEON_FLAG_CPU_ACCESS =    (1 << 1),
    RADEON_FLAG_NO_CPU_ACCESS = (1 << 2),
    RADEON_FLAG_HANDLE =        (1 << 3), /* the buffer most not be suballocated */
};

enum radeon_bo_usage { /* bitfield */
    RADEON_USAGE_READ = 2,
    RADEON_USAGE_WRITE = 4,
    RADEON_USAGE_READWRITE = RADEON_USAGE_READ | RADEON_USAGE_WRITE,

    /* The winsys ensures that the CS submission will be scheduled after
     * previously flushed CSs referencing this BO in a conflicting way.
     */
    RADEON_USAGE_SYNCHRONIZED = 8
};

enum ring_type {
    RING_GFX = 0,
    RING_COMPUTE,
    RING_DMA,
    RING_UVD,
    RING_VCE,
    RING_LAST,
};

enum radeon_value_id {
    RADEON_REQUESTED_VRAM_MEMORY,
    RADEON_REQUESTED_GTT_MEMORY,
    RADEON_MAPPED_VRAM,
    RADEON_MAPPED_GTT,
    RADEON_BUFFER_WAIT_TIME_NS,
    RADEON_TIMESTAMP,
    RADEON_NUM_GFX_IBS,
    RADEON_NUM_SDMA_IBS,
    RADEON_NUM_BYTES_MOVED,
    RADEON_NUM_EVICTIONS,
    RADEON_VRAM_USAGE,
    RADEON_GTT_USAGE,
    RADEON_GPU_TEMPERATURE, /* DRM 2.42.0 */
    RADEON_CURRENT_SCLK,
    RADEON_CURRENT_MCLK,
    RADEON_GPU_RESET_COUNTER, /* DRM 2.43.0 */
};

/* Each group of four has the same priority. */
enum radeon_bo_priority {
    RADEON_PRIO_FENCE = 0,
    RADEON_PRIO_TRACE,
    RADEON_PRIO_SO_FILLED_SIZE,
    RADEON_PRIO_QUERY,

    RADEON_PRIO_IB1 = 4, /* main IB submitted to the kernel */
    RADEON_PRIO_IB2, /* IB executed with INDIRECT_BUFFER */
    RADEON_PRIO_DRAW_INDIRECT,
    RADEON_PRIO_INDEX_BUFFER,

    RADEON_PRIO_VCE = 8,
    RADEON_PRIO_UVD,
    RADEON_PRIO_SDMA_BUFFER,
    RADEON_PRIO_SDMA_TEXTURE,

    RADEON_PRIO_CP_DMA = 12,

    RADEON_PRIO_CONST_BUFFER = 16,
    RADEON_PRIO_DESCRIPTORS,
    RADEON_PRIO_BORDER_COLORS,

    RADEON_PRIO_SAMPLER_BUFFER = 20,
    RADEON_PRIO_VERTEX_BUFFER,

    RADEON_PRIO_SHADER_RW_BUFFER = 24,
    RADEON_PRIO_COMPUTE_GLOBAL,

    RADEON_PRIO_SAMPLER_TEXTURE = 28,
    RADEON_PRIO_SHADER_RW_IMAGE,

    RADEON_PRIO_SAMPLER_TEXTURE_MSAA = 32,

    RADEON_PRIO_COLOR_BUFFER = 36,

    RADEON_PRIO_DEPTH_BUFFER = 40,

    RADEON_PRIO_COLOR_BUFFER_MSAA = 44,

    RADEON_PRIO_DEPTH_BUFFER_MSAA = 48,

    RADEON_PRIO_CMASK = 52,
    RADEON_PRIO_DCC,
    RADEON_PRIO_HTILE,
    RADEON_PRIO_SHADER_BINARY, /* the hw can't hide instruction cache misses */

    RADEON_PRIO_SHADER_RINGS = 56,

    RADEON_PRIO_SCRATCH_BUFFER = 60,
    /* 63 is the maximum value */
};

struct winsys_handle;
struct radeon_winsys_ctx;

struct radeon_winsys_cs_chunk {
    unsigned cdw;  /* Number of used dwords. */
    unsigned max_dw; /* Maximum number of dwords. */
    uint32_t *buf; /* The base pointer of the chunk. */
};

struct radeon_winsys_cs {
    struct radeon_winsys_cs_chunk current;
    struct radeon_winsys_cs_chunk *prev;
    unsigned                      num_prev; /* Number of previous chunks. */
    unsigned                      max_prev; /* Space in array pointed to by prev. */
    unsigned                      prev_dw; /* Total number of dwords in previous chunks. */

    /* Memory usage of the buffer list. These are always 0 for CE and preamble
     * IBs. */
    uint64_t                      used_vram;
    uint64_t                      used_gart;
};

struct radeon_info {
    /* PCI info: domain:bus:dev:func */
    uint32_t                    pci_domain;
    uint32_t                    pci_bus;
    uint32_t                    pci_dev;
    uint32_t                    pci_func;

    /* Device info. */
    uint32_t                    pci_id;
    enum radeon_family          family;
    enum chip_class             chip_class;
    uint32_t                    gart_page_size;
    uint64_t                    gart_size;
    uint64_t                    vram_size;
    uint64_t                    max_alloc_size;
    uint32_t                    min_alloc_size;
    bool                        has_dedicated_vram;
    bool                        has_virtual_memory;
    bool                        gfx_ib_pad_with_type2;
    bool                        has_sdma;
    bool                        has_uvd;
    uint32_t                    uvd_fw_version;
    uint32_t                    vce_fw_version;
    uint32_t                    me_fw_version;
    uint32_t                    pfp_fw_version;
    uint32_t                    ce_fw_version;
    uint32_t                    vce_harvest_config;
    uint32_t                    clock_crystal_freq;

    /* Kernel info. */
    uint32_t                    drm_major; /* version */
    uint32_t                    drm_minor;
    uint32_t                    drm_patchlevel;
    bool                        has_userptr;

    /* Shader cores. */
    uint32_t                    r600_max_quad_pipes; /* wave size / 16 */
    uint32_t                    max_shader_clock;
    uint32_t                    num_good_compute_units;
    uint32_t                    max_se; /* shader engines */
    uint32_t                    max_sh_per_se; /* shader arrays per shader engine */

    /* Render backends (color + depth blocks). */
    uint32_t                    r300_num_gb_pipes;
    uint32_t                    r300_num_z_pipes;
    uint32_t                    r600_gb_backend_map; /* R600 harvest config */
    bool                        r600_gb_backend_map_valid;
    uint32_t                    r600_num_banks;
    uint32_t                    num_render_backends;
    uint32_t                    num_tile_pipes; /* pipe count from PIPE_CONFIG */
    uint32_t                    pipe_interleave_bytes;
    uint32_t                    enabled_rb_mask; /* GCN harvest config */

    /* Tile modes. */
    uint32_t                    si_tile_mode_array[32];
    uint32_t                    cik_macrotile_mode_array[16];
};

/* Tiling info for display code, DRI sharing, and other data. */
struct radeon_bo_metadata {
    /* Tiling flags describing the texture layout for display code
     * and DRI sharing.
     */
    enum radeon_bo_layout   microtile;
    enum radeon_bo_layout   macrotile;
    unsigned                pipe_config;
    unsigned                bankw;
    unsigned                bankh;
    unsigned                tile_split;
    unsigned                mtilea;
    unsigned                num_banks;
    unsigned                stride;
    bool                    scanout;

    /* Additional metadata associated with the buffer, in bytes.
     * The maximum size is 64 * 4. This is opaque for the winsys & kernel.
     * Supported by amdgpu only.
     */
    uint32_t                size_metadata;
    uint32_t                metadata[64];
};

enum radeon_feature_id {
    RADEON_FID_R300_HYPERZ_ACCESS,     /* ZMask + HiZ */
    RADEON_FID_R300_CMASK_ACCESS,
};

#define RADEON_SURF_MAX_LEVELS                  15

enum radeon_surf_mode {
    RADEON_SURF_MODE_LINEAR_ALIGNED = 1,
    RADEON_SURF_MODE_1D = 2,
    RADEON_SURF_MODE_2D = 3,
};

/* These are defined exactly like GB_TILE_MODEn.MICRO_TILE_MODE_NEW. */
enum radeon_micro_mode {
    RADEON_MICRO_MODE_DISPLAY = 0,
    RADEON_MICRO_MODE_THIN = 1,
    RADEON_MICRO_MODE_DEPTH = 2,
    RADEON_MICRO_MODE_ROTATED = 3,
};

/* the first 16 bits are reserved for libdrm_radeon, don't use them */
#define RADEON_SURF_SCANOUT                     (1 << 16)
#define RADEON_SURF_ZBUFFER                     (1 << 17)
#define RADEON_SURF_SBUFFER                     (1 << 18)
#define RADEON_SURF_Z_OR_SBUFFER                (RADEON_SURF_ZBUFFER | RADEON_SURF_SBUFFER)
/* bits 19 and 20 are reserved for libdrm_radeon, don't use them */
#define RADEON_SURF_FMASK                       (1 << 21)
#define RADEON_SURF_DISABLE_DCC                 (1 << 22)
#define RADEON_SURF_TC_COMPATIBLE_HTILE         (1 << 23)
#define RADEON_SURF_IMPORTED                    (1 << 24)
#define RADEON_SURF_OPTIMIZE_FOR_SPACE          (1 << 25)

struct radeon_surf_level {
    uint64_t                    offset;
    uint64_t                    slice_size;
    uint64_t                    dcc_offset;
    uint64_t                    dcc_fast_clear_size;
    uint16_t                    nblk_x;
    uint16_t                    nblk_y;
    enum radeon_surf_mode       mode;
};

struct radeon_surf {
    /* Format properties. */
    unsigned                    blk_w:4;
    unsigned                    blk_h:4;
    unsigned                    bpe:5;
    /* Number of mipmap levels where DCC is enabled starting from level 0.
     * Non-zero levels may be disabled due to alignment constraints, but not
     * the first level.
     */
    unsigned                    num_dcc_levels:4;
    unsigned                    is_linear:1;
    uint32_t                    flags;

    /* These are return values. Some of them can be set by the caller, but
     * they will be treated as hints (e.g. bankw, bankh) and might be
     * changed by the calculator.
     */
    uint64_t                    surf_size;
    uint64_t                    dcc_size;
    uint64_t                    htile_size;

    uint32_t                    surf_alignment;
    uint32_t                    dcc_alignment;
    uint32_t                    htile_alignment;

    /* This applies to EG and later. */
    unsigned                    bankw:4;  /* max 8 */
    unsigned                    bankh:4;  /* max 8 */
    unsigned                    mtilea:4; /* max 8 */
    unsigned                    tile_split:13;         /* max 4K */
    unsigned                    stencil_tile_split:13; /* max 4K */
    unsigned                    pipe_config:5;      /* max 17 */
    unsigned                    num_banks:5;        /* max 16 */
    unsigned                    macro_tile_index:4; /* max 15 */
    unsigned                    micro_tile_mode:3; /* displayable, thin, depth, rotated */

    /* Whether the depth miptree or stencil miptree as used by the DB are
     * adjusted from their TC compatible form to ensure depth/stencil
     * compatibility. If either is true, the corresponding plane cannot be
     * sampled from.
     */
    unsigned                    depth_adjusted:1;
    unsigned                    stencil_adjusted:1;

    struct radeon_surf_level    level[RADEON_SURF_MAX_LEVELS];
    struct radeon_surf_level    stencil_level[RADEON_SURF_MAX_LEVELS];
    uint8_t                     tiling_index[RADEON_SURF_MAX_LEVELS];
    uint8_t                     stencil_tiling_index[RADEON_SURF_MAX_LEVELS];
};

struct radeon_bo_list_item {
    uint64_t bo_size;
    uint64_t vm_address;
    uint64_t priority_usage; /* mask of (1 << RADEON_PRIO_*) */
};

struct radeon_winsys {
    /**
     * The screen object this winsys was created for
     */
    struct pipe_screen *screen;

    /**
     * Decrement the winsys reference count.
     *
     * \param ws  The winsys this function is called for.
     * \return    True if the winsys and screen should be destroyed.
     */
    bool (*unref)(struct radeon_winsys *ws);

    /**
     * Destroy this winsys.
     *
     * \param ws        The winsys this function is called from.
     */
    void (*destroy)(struct radeon_winsys *ws);

    /**
     * Query an info structure from winsys.
     *
     * \param ws        The winsys this function is called from.
     * \param info      Return structure
     */
    void (*query_info)(struct radeon_winsys *ws,
                       struct radeon_info *info);

    /**************************************************************************
     * Buffer management. Buffer attributes are mostly fixed over its lifetime.
     *
     * Remember that gallium gets to choose the interface it needs, and the
     * window systems must then implement that interface (rather than the
     * other way around...).
     *************************************************************************/

    /**
     * Create a buffer object.
     *
     * \param ws        The winsys this function is called from.
     * \param size      The size to allocate.
     * \param alignment An alignment of the buffer in memory.
     * \param use_reusable_pool Whether the cache buffer manager should be used.
     * \param domain    A bitmask of the RADEON_DOMAIN_* flags.
     * \return          The created buffer object.
     */
    struct pb_buffer *(*buffer_create)(struct radeon_winsys *ws,
                                       uint64_t size,
                                       unsigned alignment,
                                       enum radeon_bo_domain domain,
                                       enum radeon_bo_flag flags);

    /**
     * Map the entire data store of a buffer object into the client's address
     * space.
     *
     * \param buf       A winsys buffer object to map.
     * \param cs        A command stream to flush if the buffer is referenced by it.
     * \param usage     A bitmask of the PIPE_TRANSFER_* flags.
     * \return          The pointer at the beginning of the buffer.
     */
    void *(*buffer_map)(struct pb_buffer *buf,
                        struct radeon_winsys_cs *cs,
                        enum pipe_transfer_usage usage);

    /**
     * Unmap a buffer object from the client's address space.
     *
     * \param buf       A winsys buffer object to unmap.
     */
    void (*buffer_unmap)(struct pb_buffer *buf);

    /**
     * Wait for the buffer and return true if the buffer is not used
     * by the device.
     *
     * The timeout of 0 will only return the status.
     * The timeout of PIPE_TIMEOUT_INFINITE will always wait until the buffer
     * is idle.
     */
    bool (*buffer_wait)(struct pb_buffer *buf, uint64_t timeout,
                        enum radeon_bo_usage usage);

    /**
     * Return buffer metadata.
     * (tiling info for display code, DRI sharing, and other data)
     *
     * \param buf       A winsys buffer object to get the flags from.
     * \param md        Metadata
     */
    void (*buffer_get_metadata)(struct pb_buffer *buf,
                                struct radeon_bo_metadata *md);

    /**
     * Set buffer metadata.
     * (tiling info for display code, DRI sharing, and other data)
     *
     * \param buf       A winsys buffer object to set the flags for.
     * \param md        Metadata
     */
    void (*buffer_set_metadata)(struct pb_buffer *buf,
                                struct radeon_bo_metadata *md);

    /**
     * Get a winsys buffer from a winsys handle. The internal structure
     * of the handle is platform-specific and only a winsys should access it.
     *
     * \param ws        The winsys this function is called from.
     * \param whandle   A winsys handle pointer as was received from a state
     *                  tracker.
     * \param stride    The returned buffer stride in bytes.
     */
    struct pb_buffer *(*buffer_from_handle)(struct radeon_winsys *ws,
                                            struct winsys_handle *whandle,
                                            unsigned *stride, unsigned *offset);

    /**
     * Get a winsys buffer from a user pointer. The resulting buffer can't
     * be exported. Both pointer and size must be page aligned.
     *
     * \param ws        The winsys this function is called from.
     * \param pointer   User pointer to turn into a buffer object.
     * \param Size      Size in bytes for the new buffer.
     */
    struct pb_buffer *(*buffer_from_ptr)(struct radeon_winsys *ws,
                                         void *pointer, uint64_t size);

    /**
     * Whether the buffer was created from a user pointer.
     *
     * \param buf       A winsys buffer object
     * \return          whether \p buf was created via buffer_from_ptr
     */
    bool (*buffer_is_user_ptr)(struct pb_buffer *buf);

    /**
     * Get a winsys handle from a winsys buffer. The internal structure
     * of the handle is platform-specific and only a winsys should access it.
     *
     * \param buf       A winsys buffer object to get the handle from.
     * \param whandle   A winsys handle pointer.
     * \param stride    A stride of the buffer in bytes, for texturing.
     * \return          true on success.
     */
    bool (*buffer_get_handle)(struct pb_buffer *buf,
                              unsigned stride, unsigned offset,
                              unsigned slice_size,
                              struct winsys_handle *whandle);

    /**
     * Return the virtual address of a buffer.
     *
     * When virtual memory is not in use, this is the offset relative to the
     * relocation base (non-zero for sub-allocated buffers).
     *
     * \param buf       A winsys buffer object
     * \return          virtual address
     */
    uint64_t (*buffer_get_virtual_address)(struct pb_buffer *buf);

    /**
     * Return the offset of this buffer relative to the relocation base.
     * This is only non-zero for sub-allocated buffers.
     *
     * This is only supported in the radeon winsys, since amdgpu uses virtual
     * addresses in submissions even for the video engines.
     *
     * \param buf      A winsys buffer object
     * \return         the offset for relocations
     */
    unsigned (*buffer_get_reloc_offset)(struct pb_buffer *buf);

    /**
     * Query the initial placement of the buffer from the kernel driver.
     */
    enum radeon_bo_domain (*buffer_get_initial_domain)(struct pb_buffer *buf);

    /**************************************************************************
     * Command submission.
     *
     * Each pipe context should create its own command stream and submit
     * commands independently of other contexts.
     *************************************************************************/

    /**
     * Create a command submission context.
     * Various command streams can be submitted to the same context.
     */
    struct radeon_winsys_ctx *(*ctx_create)(struct radeon_winsys *ws);

    /**
     * Destroy a context.
     */
    void (*ctx_destroy)(struct radeon_winsys_ctx *ctx);

    /**
     * Query a GPU reset status.
     */
    enum pipe_reset_status (*ctx_query_reset_status)(struct radeon_winsys_ctx *ctx);

    /**
     * Create a command stream.
     *
     * \param ctx       The submission context
     * \param ring_type The ring type (GFX, DMA, UVD)
     * \param flush     Flush callback function associated with the command stream.
     * \param user      User pointer that will be passed to the flush callback.
     */
    struct radeon_winsys_cs *(*cs_create)(struct radeon_winsys_ctx *ctx,
                                          enum ring_type ring_type,
                                          void (*flush)(void *ctx, unsigned flags,
							struct pipe_fence_handle **fence),
                                          void *flush_ctx);

    /**
     * Add a constant engine IB to a graphics CS. This makes the graphics CS
     * from "cs_create" a group of two IBs that share a buffer list and are
     * flushed together.
     *
     * The returned constant CS is only a stream for writing packets to the new
     * IB. Calling other winsys functions with it is not allowed, not even
     * "cs_destroy".
     *
     * In order to add buffers and check memory usage, use the graphics CS.
     * In order to flush it, use the graphics CS, which will flush both IBs.
     * Destroying the graphics CS will destroy both of them.
     *
     * \param cs  The graphics CS from "cs_create" that will hold the buffer
     *            list and will be used for flushing.
     */
    struct radeon_winsys_cs *(*cs_add_const_ib)(struct radeon_winsys_cs *cs);

     /**
     * Add a constant engine preamble IB to a graphics CS. This add an extra IB
     * in similar manner to cs_add_const_ib. This should always be called after
     * cs_add_const_ib.
     *
     * The returned IB is a constant engine IB that only gets flushed if the
     * context changed.
     *
     * \param cs  The graphics CS from "cs_create" that will hold the buffer
     *            list and will be used for flushing.
     */
    struct radeon_winsys_cs *(*cs_add_const_preamble_ib)(struct radeon_winsys_cs *cs);
    /**
     * Destroy a command stream.
     *
     * \param cs        A command stream to destroy.
     */
    void (*cs_destroy)(struct radeon_winsys_cs *cs);

    /**
     * Add a buffer. Each buffer used by a CS must be added using this function.
     *
     * \param cs      Command stream
     * \param buf     Buffer
     * \param usage   Whether the buffer is used for read and/or write.
     * \param domain  Bitmask of the RADEON_DOMAIN_* flags.
     * \param priority  A higher number means a greater chance of being
     *                  placed in the requested domain. 15 is the maximum.
     * \return Buffer index.
     */
    unsigned (*cs_add_buffer)(struct radeon_winsys_cs *cs,
                             struct pb_buffer *buf,
                             enum radeon_bo_usage usage,
                             enum radeon_bo_domain domain,
                             enum radeon_bo_priority priority);

    /**
     * Return the index of an already-added buffer.
     *
     * Not supported on amdgpu. Drivers with GPUVM should not care about
     * buffer indices.
     *
     * \param cs        Command stream
     * \param buf       Buffer
     * \return          The buffer index, or -1 if the buffer has not been added.
     */
    int (*cs_lookup_buffer)(struct radeon_winsys_cs *cs,
                            struct pb_buffer *buf);

    /**
     * Return true if there is enough memory in VRAM and GTT for the buffers
     * added so far. If the validation fails, all buffers which have
     * been added since the last call of cs_validate will be removed and
     * the CS will be flushed (provided there are still any buffers).
     *
     * \param cs        A command stream to validate.
     */
    bool (*cs_validate)(struct radeon_winsys_cs *cs);

    /**
     * Check whether the given number of dwords is available in the IB.
     * Optionally chain a new chunk of the IB if necessary and supported.
     *
     * \param cs        A command stream.
     * \param dw        Number of CS dwords requested by the caller.
     */
    bool (*cs_check_space)(struct radeon_winsys_cs *cs, unsigned dw);

    /**
     * Return the buffer list.
     *
     * This is the buffer list as passed to the kernel, i.e. it only contains
     * the parent buffers of sub-allocated buffers.
     *
     * \param cs    Command stream
     * \param list  Returned buffer list. Set to NULL to query the count only.
     * \return      The buffer count.
     */
    unsigned (*cs_get_buffer_list)(struct radeon_winsys_cs *cs,
                                   struct radeon_bo_list_item *list);

    /**
     * Flush a command stream.
     *
     * \param cs          A command stream to flush.
     * \param flags,      RADEON_FLUSH_ASYNC or 0.
     * \param fence       Pointer to a fence. If non-NULL, a fence is inserted
     *                    after the CS and is returned through this parameter.
     * \return Negative POSIX error code or 0 for success.
     *         Asynchronous submissions never return an error.
     */
    int (*cs_flush)(struct radeon_winsys_cs *cs,
                    unsigned flags,
                    struct pipe_fence_handle **fence);

    /**
     * Create a fence before the CS is flushed.
     * The user must flush manually to complete the initializaton of the fence.
     * The fence must not be used before the flush.
     */
    struct pipe_fence_handle *(*cs_get_next_fence)(struct radeon_winsys_cs *cs);

    /**
     * Return true if a buffer is referenced by a command stream.
     *
     * \param cs        A command stream.
     * \param buf       A winsys buffer.
     */
    bool (*cs_is_buffer_referenced)(struct radeon_winsys_cs *cs,
                                    struct pb_buffer *buf,
                                    enum radeon_bo_usage usage);

    /**
     * Request access to a feature for a command stream.
     *
     * \param cs        A command stream.
     * \param fid       Feature ID, one of RADEON_FID_*
     * \param enable    Whether to enable or disable the feature.
     */
    bool (*cs_request_feature)(struct radeon_winsys_cs *cs,
                               enum radeon_feature_id fid,
                               bool enable);
     /**
      * Make sure all asynchronous flush of the cs have completed
      *
      * \param cs        A command stream.
      */
    void (*cs_sync_flush)(struct radeon_winsys_cs *cs);

    /**
     * Wait for the fence and return true if the fence has been signalled.
     * The timeout of 0 will only return the status.
     * The timeout of PIPE_TIMEOUT_INFINITE will always wait until the fence
     * is signalled.
     */
    bool (*fence_wait)(struct radeon_winsys *ws,
                       struct pipe_fence_handle *fence,
                       uint64_t timeout);

    /**
     * Reference counting for fences.
     */
    void (*fence_reference)(struct pipe_fence_handle **dst,
                            struct pipe_fence_handle *src);

    /**
     * Initialize surface
     *
     * \param ws        The winsys this function is called from.
     * \param tex       Input texture description
     * \param flags     Bitmask of RADEON_SURF_* flags
     * \param bpe       Bytes per pixel, it can be different for Z buffers.
     * \param mode      Preferred tile mode. (linear, 1D, or 2D)
     * \param surf      Output structure
     */
    int (*surface_init)(struct radeon_winsys *ws,
                        const struct pipe_resource *tex,
                        unsigned flags, unsigned bpe,
                        enum radeon_surf_mode mode,
                        struct radeon_surf *surf);

    uint64_t (*query_value)(struct radeon_winsys *ws,
                            enum radeon_value_id value);

    bool (*read_registers)(struct radeon_winsys *ws, unsigned reg_offset,
                           unsigned num_registers, uint32_t *out);
};

static inline bool radeon_emitted(struct radeon_winsys_cs *cs, unsigned num_dw)
{
    return cs && (cs->prev_dw + cs->current.cdw > num_dw);
}

static inline void radeon_emit(struct radeon_winsys_cs *cs, uint32_t value)
{
    cs->current.buf[cs->current.cdw++] = value;
}

static inline void radeon_emit_array(struct radeon_winsys_cs *cs,
				     const uint32_t *values, unsigned count)
{
    memcpy(cs->current.buf + cs->current.cdw, values, count * 4);
    cs->current.cdw += count;
}

#endif
