#include "dxil_validator.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif

#include <windows.h>
#include <unknwn.h>

#include "util/ralloc.h"
#include "util/u_debug.h"

#include "dxcapi.h"

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

struct dxil_validator {
   HMODULE dxil_mod;
   HMODULE dxcompiler_mod;

   IDxcValidator *dxc_validator;
   IDxcLibrary *dxc_library;
   IDxcCompiler *dxc_compiler;
};

extern "C" extern IMAGE_DOS_HEADER __ImageBase;

static HMODULE
load_dxil_mod()
{
   /* First, try to load DXIL.dll from the default search-path */
   HMODULE mod = LoadLibraryA("DXIL.dll");
   if (mod)
      return mod;

   /* If that fails, try to load it next to the current module, so we can
    * ship DXIL.dll next to the GLon12 DLL.
    */

   char self_path[MAX_PATH];
   uint32_t path_size = GetModuleFileNameA((HINSTANCE)&__ImageBase,
                                           self_path, sizeof(self_path));
   if (!path_size || path_size == sizeof(self_path)) {
      debug_printf("DXIL: Unable to get path to self");
      return NULL;
   }

   auto last_slash = strrchr(self_path, '\\');
   if (!last_slash) {
      debug_printf("DXIL: Unable to get path to self");
      return NULL;
   }

   *(last_slash + 1) = '\0';
   if (strcat_s(self_path, "DXIL.dll") != 0) {
      debug_printf("DXIL: Unable to get path to DXIL.dll next to self");
      return NULL;
   }

   return LoadLibraryA(self_path);
}

static IDxcValidator *
create_dxc_validator(HMODULE dxil_mod)
{
   DxcCreateInstanceProc dxil_create_func =
      (DxcCreateInstanceProc)GetProcAddress(dxil_mod, "DxcCreateInstance");
   if (!dxil_create_func) {
      debug_printf("DXIL: Failed to load DxcCreateInstance from DXIL.dll\n");
      return NULL;
   }

   IDxcValidator *dxc_validator;
   HRESULT hr = dxil_create_func(CLSID_DxcValidator,
                                 IID_PPV_ARGS(&dxc_validator));
   if (FAILED(hr)) {
      debug_printf("DXIL: Failed to create validator\n");
      return NULL;
   }

   return dxc_validator;
}

struct dxil_validator *
dxil_create_validator(const void *ctx)
{
   struct dxil_validator *val = rzalloc(ctx, struct dxil_validator);
   if (!val)
      return NULL;

   /* Load DXIL.dll. This is a hard requirement on Windows, so we error
    * out if this fails.
    */
   val->dxil_mod = load_dxil_mod();
   if (!val->dxil_mod) {
      debug_printf("DXIL: Failed to load DXIL.dll\n");
      goto fail;
   }

   /* Create IDxcValidator. This is a hard requirement on Windows, so we
    * error out if this fails.
    */
   val->dxc_validator = create_dxc_validator(val->dxil_mod);
   if (!val->dxc_validator)
      goto fail;

   /* Try to load dxcompiler.dll. This is just used for diagnostics, and
    * will fail on most end-users install. So we do not error out if this
    * fails.
    */
   val->dxcompiler_mod = LoadLibraryA("dxcompiler.dll");
   if (val->dxcompiler_mod) {
      /* If we managed to load dxcompiler.dll, but either don't find
       * DxcCreateInstance, or fail to create IDxcLibrary or
       * IDxcCompiler, this is a good indication that the user wants
       * diagnostics, but something went wrong. Print warnings to help
       * figuring out what's wrong, but do not treat it as an error.
       */
      DxcCreateInstanceProc compiler_create_func =
         (DxcCreateInstanceProc)GetProcAddress(val->dxcompiler_mod,
                                               "DxcCreateInstance");
      if (!compiler_create_func) {
         debug_printf("DXIL: Failed to load DxcCreateInstance from "
                      "dxcompiler.dll\n");
      } else {
         if (FAILED(compiler_create_func(CLSID_DxcLibrary,
                                         IID_PPV_ARGS(&val->dxc_library))))
            debug_printf("DXIL: Unable to create IDxcLibrary instance\n");

         if (FAILED(compiler_create_func(CLSID_DxcCompiler,
                                         IID_PPV_ARGS(&val->dxc_compiler))))
            debug_printf("DXIL: Unable to create IDxcCompiler instance\n");
      }
   }

   return val;

fail:
   if (val->dxil_mod)
      FreeLibrary(val->dxil_mod);

   ralloc_free(val);
   return NULL;
}

void
dxil_destroy_validator(struct dxil_validator *val)
{
   /* if we have a validator, we have these */
   FreeLibrary(val->dxil_mod);
   val->dxc_validator->Release();

   if (val->dxcompiler_mod) {
      if (val->dxc_library)
         val->dxc_library->Release();

      if (val->dxc_compiler)
         val->dxc_compiler->Release();

      FreeLibrary(val->dxcompiler_mod);
   }

   ralloc_free(val);
}

class ShaderBlob : public IDxcBlob {
public:
   ShaderBlob(void *data, size_t size) :
      m_data(data),
      m_size(size)
   {
   }

   LPVOID STDMETHODCALLTYPE
   GetBufferPointer(void) override
   {
      return m_data;
   }

   SIZE_T STDMETHODCALLTYPE
   GetBufferSize() override
   {
      return m_size;
   }

   HRESULT STDMETHODCALLTYPE
   QueryInterface(REFIID, void **) override
   {
      return E_NOINTERFACE;
   }

   ULONG STDMETHODCALLTYPE
   AddRef() override
   {
      return 1;
   }

   ULONG STDMETHODCALLTYPE
   Release() override
   {
      return 0;
   }

   void *m_data;
   size_t m_size;
};

bool
dxil_validate_module(struct dxil_validator *val, void *data, size_t size, char **error)
{
   ShaderBlob source(data, size);

   ComPtr<IDxcOperationResult> result;
   val->dxc_validator->Validate(&source, DxcValidatorFlags_InPlaceEdit,
                                &result);

   HRESULT hr;
   result->GetStatus(&hr);

   if (FAILED(hr) && error) {
      /* try to resolve error message */
      *error = NULL;
      if (!val->dxc_library) {
         debug_printf("DXIL: validation failed, but lacking IDxcLibrary"
                      "from dxcompiler.dll for proper diagnostics.\n");
         return false;
      }

      ComPtr<IDxcBlobEncoding> blob, blob_utf8;

      if (FAILED(result->GetErrorBuffer(&blob)))
         fprintf(stderr, "DXIL: IDxcOperationResult::GetErrorBuffer() failed\n");
      else if (FAILED(val->dxc_library->GetBlobAsUtf8(blob.Get(),
                                                      blob_utf8.GetAddressOf())))
         fprintf(stderr, "DXIL: IDxcLibrary::GetBlobAsUtf8() failed\n");
      else {
         char *str = reinterpret_cast<char *>(blob_utf8->GetBufferPointer());
         str[blob_utf8->GetBufferSize() - 1] = 0;
         *error = ralloc_strdup(val, str);
      }
   }

   return SUCCEEDED(hr);
}

char *
dxil_disasm_module(struct dxil_validator *val, void *data, size_t size)
{
   if (!val->dxc_compiler || !val->dxc_library) {
      fprintf(stderr, "DXIL: disassembly requires IDxcLibrary and "
              "IDxcCompiler from dxcompiler.dll\n");
      return NULL;
   }

   ShaderBlob source(data, size);
   ComPtr<IDxcBlobEncoding> blob, blob_utf8;

   if (FAILED(val->dxc_compiler->Disassemble(&source, &blob))) {
      fprintf(stderr, "DXIL: IDxcCompiler::Disassemble() failed\n");
      return NULL;
   }

   if (FAILED(val->dxc_library->GetBlobAsUtf8(blob.Get(), blob_utf8.GetAddressOf()))) {
      fprintf(stderr, "DXIL: IDxcLibrary::GetBlobAsUtf8() failed\n");
      return NULL;
   }

   char *str = reinterpret_cast<char*>(blob_utf8->GetBufferPointer());
   str[blob_utf8->GetBufferSize() - 1] = 0;
   return ralloc_strdup(val, str);
}
