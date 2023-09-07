#define blob_finish __blob_finish
#define disk_cache_get_function_identifier __disk_cache_get_function_identifier
#define mesa_bytes_to_hex __mesa_bytes_to_hex
#define nir_shader_get_entrypoint __nir_shader_get_entrypoint_wraped
#define pipe_resource_reference __pipe_resource_reference_wraped
#define util_format_pack_rgba __util_format_pack_rgba
#include "nir.h"
#include "util/blob.h"
#include "util/disk_cache.h"
#include "util/hex.h"
#include "util/u_inlines.h"
#include "util/format/u_format.h"
#undef blob_finish
#undef mesa_bytes_to_hex
#undef disk_cache_get_function_identifier
#undef nir_shader_get_entrypoint
#undef pipe_resource_reference
#undef util_format_pack_rgba

void blob_finish(struct blob *);
char *mesa_bytes_to_hex(char *buf, const uint8_t *hex_id, unsigned size);
bool disk_cache_get_function_identifier(void *ptr, struct mesa_sha1 *ctx);
const char* mesa_version_string(void);
nir_function_impl *nir_shader_get_entrypoint(const nir_shader *shader);
void pipe_resource_reference(struct pipe_resource **dst, struct pipe_resource *src);
void util_format_pack_rgba(enum pipe_format format, void *dst, const void *src, unsigned w);
