# Locates a prebuilt ONNX Runtime package.

set(LUCIDGRASP_ONNXRUNTIME_ROOT "" CACHE PATH
    "Root directory of a prebuilt ONNX Runtime package (third_party/onnxruntime)")

find_path(ONNXRUNTIME_INCLUDE_DIR
  NAMES onnxruntime_cxx_api.h
  HINTS "${LUCIDGRASP_ONNXRUNTIME_ROOT}/include"
  PATH_SUFFIXES include)

find_library(ONNXRUNTIME_LIBRARY
  NAMES onnxruntime libonnxruntime
  HINTS "${LUCIDGRASP_ONNXRUNTIME_ROOT}/lib"
  PATH_SUFFIXES lib)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(ONNXRuntime
  REQUIRED_VARS ONNXRUNTIME_INCLUDE_DIR ONNXRUNTIME_LIBRARY)

if(ONNXRuntime_FOUND AND NOT TARGET ONNXRuntime::ONNXRuntime)
  add_library(ONNXRuntime::ONNXRuntime SHARED IMPORTED)
  set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}")
  if(WIN32)
    # The Windows package ships an import library (onnxruntime.lib) to link
    # against plus a runtime DLL loaded at run time. MSVC links through
    # IMPORTED_IMPLIB, so it has to be set explicitly; otherwise CMake injects
    # a "ONNXRuntime::ONNXRuntime-NOTFOUND" location into the generated link
    # line and the build fails with a syntax error. IMPORTED_LOCATION keeps
    # pointing at the DLL so <TARGET_FILE> and CMP0111 stay satisfied.
    find_file(ONNXRUNTIME_DLL
      NAMES onnxruntime.dll
      HINTS "${LUCIDGRASP_ONNXRUNTIME_ROOT}/lib"
            "${LUCIDGRASP_ONNXRUNTIME_ROOT}/bin"
      PATH_SUFFIXES lib bin)
    if(NOT ONNXRUNTIME_DLL)
      set(ONNXRUNTIME_DLL "${ONNXRUNTIME_LIBRARY}")
    endif()
    set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
      IMPORTED_IMPLIB "${ONNXRUNTIME_LIBRARY}"
      IMPORTED_LOCATION "${ONNXRUNTIME_DLL}")
  else()
    set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
      IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}")
  endif()
endif()

mark_as_advanced(ONNXRUNTIME_INCLUDE_DIR ONNXRUNTIME_LIBRARY ONNXRUNTIME_DLL)