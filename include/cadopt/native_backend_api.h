#pragma once

#include <stddef.h>

#define CADOPT_BACKEND_API_VERSION 1

#if defined(_WIN32)
  #if defined(CADOPT_BACKEND_BUILD)
    #define CADOPT_BACKEND_EXPORT __declspec(dllexport)
  #else
    #define CADOPT_BACKEND_EXPORT
  #endif
#else
  #if defined(CADOPT_BACKEND_BUILD)
    #define CADOPT_BACKEND_EXPORT __attribute__((visibility("default")))
  #else
    #define CADOPT_BACKEND_EXPORT
  #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

// All paths and diagnostics are UTF-8.
// Conversion functions return 0 on success and non-zero on failure.
// Implementations should write a null-terminated diagnostic when possible.
CADOPT_BACKEND_EXPORT int cadopt_backend_api_version(void);
CADOPT_BACKEND_EXPORT const char* cadopt_backend_name(void);
CADOPT_BACKEND_EXPORT int cadopt_backend_dxf_to_dwg(const char* input_utf8,
                                                     const char* output_utf8,
                                                     char* diagnostic_utf8,
                                                     size_t diagnostic_capacity);
CADOPT_BACKEND_EXPORT int cadopt_backend_dwg_to_dxf(const char* input_utf8,
                                                     const char* output_utf8,
                                                     char* diagnostic_utf8,
                                                     size_t diagnostic_capacity);

#ifdef __cplusplus
}
#endif
