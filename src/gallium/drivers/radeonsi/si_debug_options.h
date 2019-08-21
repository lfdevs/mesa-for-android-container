OPT_BOOL(clear_db_cache_before_clear, false, "Clear DB cache before fast depth clear")
OPT_BOOL(enable_nir, false, "Enable NIR")
OPT_BOOL(aux_debug, false, "Generate ddebug_dumps for the auxiliary context")
OPT_BOOL(sync_compile, false, "Always compile synchronously (will cause stalls)")
OPT_BOOL(dump_shader_binary, false, "Dump shader binary as part of ddebug_dumps")
OPT_BOOL(debug_disassembly, false, "Report shader disassembly as part of driver debug messages (for shader db)")
OPT_BOOL(halt_shaders, false, "Halt shaders at the start (will hang)")
OPT_BOOL(vs_fetch_always_opencode, false, "Always open code vertex fetches (less efficient, purely for testing)")
OPT_BOOL(prim_restart_tri_strips_only, false, "Only enable primitive restart for triangle strips")

#undef OPT_BOOL
