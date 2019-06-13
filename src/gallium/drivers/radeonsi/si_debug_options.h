OPT_BOOL(clear_db_cache_before_clear, false, "Clear DB cache before fast depth clear")
OPT_BOOL(enable_nir, false, "Enable NIR")
OPT_BOOL(aux_debug, false, "Generate ddebug_dumps for the auxiliary context")
OPT_BOOL(sync_compile, false, "Always compile synchronously (will cause stalls)")

#undef OPT_BOOL
