#include "rusticl_mesa_inline_bindings_wrapper.h"
#include "git_sha1.h"

void
blob_finish(struct blob *blob)
{
    __blob_finish(blob);
}

bool
disk_cache_get_function_identifier(void *ptr, struct mesa_sha1 *ctx)
{
   return __disk_cache_get_function_identifier(ptr, ctx);
}

char *
mesa_bytes_to_hex(char *buf, const uint8_t *hex_id, unsigned size)
{
   return __mesa_bytes_to_hex(buf, hex_id, size);
}

nir_function_impl *
nir_shader_get_entrypoint(const nir_shader *shader)
{
   return __nir_shader_get_entrypoint_wraped(shader);
}

void
pipe_resource_reference(struct pipe_resource **dst, struct pipe_resource *src)
{
   __pipe_resource_reference_wraped(dst, src);
}

void
util_format_pack_rgba(enum pipe_format format, void *dst, const void *src, unsigned w)
{
    return __util_format_pack_rgba(format, dst, src, w);
}

const char*
mesa_version_string(void)
{
    return PACKAGE_VERSION MESA_GIT_SHA1;
}
