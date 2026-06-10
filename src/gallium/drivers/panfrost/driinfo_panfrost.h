/* panfrost specific driconf options */
DRI_CONF_SECTION_PERFORMANCE
   /* AFBC options. */
   DRI_CONF_OPT_B(pan_afbc_tiled, true, "Use AFBC tiled layout whenever "
                  "possible")

   /* AFBC packing options. */
   DRI_CONF_OPT_B(pan_force_afbc_packing, false, "Pack AFBC textures "
                  "progressively in the background")
   DRI_CONF_OPT_I(pan_afbcp_reads_threshold, 10, 1, 255, "Consecutive reads "
                  "theshold after which an AFBC texture is packed")
   DRI_CONF_OPT_B(pan_afbcp_gpu_payload_sizes, false, "Compute AFBC-P "
                  "payload sizes on GPU")

   /* 2M chunks. */
   DRI_CONF_OPT_I(pan_csf_chunk_size, 2 * 1024 * 1024, 256 * 1024, 8 * 1024 * 1024, "CSF Tiler Chunk Size")
   DRI_CONF_OPT_I(pan_csf_initial_chunks, 5, 1, 65535, "CSF Tiler Initial Chunks")
   /* 64 x 2M = 128M, which matches the tiler_heap BO allocated in
    * panfrost_open_device() for pre-v10 HW.
    */
   DRI_CONF_OPT_I(pan_csf_max_chunks, 64, 1, 65535, "CSF Tiler Max Chunks")
DRI_CONF_SECTION_END

DRI_CONF_SECTION_MISCELLANEOUS
   DRI_CONF_HEAP_MEMORY_PERCENT(0)
   DRI_CONF_PAN_COMPUTE_CORE_MASK(~0ull)
   DRI_CONF_PAN_FRAGMENT_CORE_MASK(~0ull)
   DRI_CONF_OPT_B(pan_relax_afbc_yuv_imports, false, "Use relaxed import rules for AFBC(YUV)")
DRI_CONF_SECTION_END
