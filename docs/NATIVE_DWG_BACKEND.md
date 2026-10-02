# Native DWG Backend Integration

This repository intentionally does not redistribute Autodesk RealDWG or ODA Drawings SDK binaries. A production integration keeps the licensed SDK in a separately built host DLL/SO and exposes the stable C ABI declared in `include/cadopt/native_backend_api.h`.

## ABI contract

API version: `CADOPT_BACKEND_API_VERSION == 1`.

Required exports:

```c
int cadopt_backend_api_version(void);
int cadopt_backend_dxf_to_dwg(const char* input_utf8,
                              const char* output_utf8,
                              char* diagnostic_utf8,
                              size_t diagnostic_capacity);
int cadopt_backend_dwg_to_dxf(const char* input_utf8,
                              const char* output_utf8,
                              char* diagnostic_utf8,
                              size_t diagnostic_capacity);
```

Optional export:

```c
const char* cadopt_backend_name(void);
```

Paths and diagnostics use UTF-8. Conversion returns `0` only when the requested output was successfully produced. The optimizer independently checks output existence and file size even when the plugin returns success.

## ODA host

A licensed ODA Drawings SDK host should implement the two conversion exports with the SDK's DXF/DWG database load/save APIs. The CAD optimizer does not assume a specific ODA sample executable or command syntax.

Use backend profile `OdaSdk` when constructing `NativeLibraryDwgBackend` so reports preserve the correct licensing/capability provenance.

## RealDWG host

A licensed RealDWG host should implement the same C ABI around Autodesk's database read/write APIs. Keep Autodesk headers, libraries, deployment files, and license-controlled artifacts outside this repository.

Use backend profile `RealDwgHost` when constructing `NativeLibraryDwgBackend`.

## Fail-closed rules

The native adapter rejects the host before optimization if:

- the shared library cannot be loaded;
- `cadopt_backend_api_version` is missing or does not return `1`;
- either conversion export is missing.

A conversion is rejected if:

- the host returns a non-zero code;
- output is missing;
- output is empty;
- output size cannot be measured.

Even a successful conversion is not a valid optimization result until M6 converts the DWG back to DXF and the independent geometry + semantic + interaction verifier passes.

## Repository test host

`tests/fake_native_backend.cpp` implements the same ABI without any proprietary SDK. It only copies bytes and exists to verify DLL/SO loading, symbol resolution, diagnostics, two-way calls, and output measurement at PR-time test execution. It is not a DWG implementation.

## Production materializer boundary

The generic `PlanAwareMaterializer` only emits representations that can be safely expressed as standard DXF while preserving customer-visible entity units. Structural representations such as reference/grid/grammar/tensor candidates remain analysis-only until a backend-specific materializer can reconstruct them into standard CAD entities without changing geometry, semantic structure, or selection/edit behavior.
