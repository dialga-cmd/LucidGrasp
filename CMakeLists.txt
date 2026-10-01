cmake_minimum_required(VERSION 3.16)
project(LucidGrasp VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
find_package(Threads REQUIRED)
find_package(OpenCV REQUIRED)

# ONNX Runtime is optional. When it is found, "Similar" mode is compiled in;
# when it is not, the build is exactly what it was before and the mode does not
# appear in the UI at all. It is a build-time dependency only -- the model
# weights are looked for at runtime and are never bundled.
#
# The official release tarball is a bare directory with include/ and lib/
# inside, which no installed CMake package config describes, so the headers and
# library are located directly. Point ONNXRUNTIME_ROOT at the extracted
# directory (or at its parent, where the versioned subdirectory sits), or add
# the install prefix to CMAKE_PREFIX_PATH.
set(ONNXRUNTIME_ROOT "" CACHE PATH
    "Root of an onnxruntime release: the directory containing include/ and lib/")

set(LUCIDGRASP_HAVE_ORT OFF)
if(ONNXRUNTIME_ROOT)
    file(GLOB _ort_candidates
         "${ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h"
         "${ONNXRUNTIME_ROOT}/onnxruntime-linux-*/include/onnxruntime_cxx_api.h"
         "${ONNXRUNTIME_ROOT}/onnxruntime-*/include/onnxruntime_cxx_api.h")
    list(GET _ort_candidates 0 _ort_include_marker)
    get_filename_component(_ort_include "${_ort_include_marker}" DIRECTORY)
    get_filename_component(_ort_root "${_ort_include}" DIRECTORY)
else()
    find_path(_ort_include onnxruntime_cxx_api.h PATH_SUFFIXES onnxruntime-core)
    if(_ort_include)
        get_filename_component(_ort_root "${_ort_include}" DIRECTORY)
    endif()
endif()

find_library(_ort_lib
             NAMES onnxruntime
             PATHS "${_ort_root}/lib" "${_ort_root}/lib64"
                   "${_ort_root}/lib/x64" ENV ONNXRUNTIME_ROOT
             PATH_SUFFIXES "")

if(_ort_include AND _ort_lib)
    set(LUCIDGRASP_HAVE_ORT ON)
    message(STATUS "ONNX Runtime found: ${_ort_lib}")
else()
    message(STATUS "ONNX Runtime not found -- building Lookalike only "
                   "(set -DONNXRUNTIME_ROOT=<dir> to enable Similar mode)")
endif()

# The repository the update checker polls, overridable at configure time for
# forks and local testing.
set(LUCIDGRASP_REPO "dialga-cmd/LucidGrasp"
    CACHE STRING "GitHub owner/repo that the update checker queries")

set(SOURCES
    src/main.cpp
    src/selftest.cpp
    src/app/update_checker.h
    src/app/update_checker.cpp
    src/core/cv_matcher.h
    src/core/cv_matcher.cpp
    src/core/embedding_cache.h
    src/core/embedding_cache.cpp
    src/core/features.h
    src/core/features.cpp
    src/core/index.h
    src/core/index.cpp
    src/ui/mainwindow.h
    src/ui/mainwindow.cpp
)

# The Similar-mode code. Without ONNX Runtime these two files compile to
# "Similar mode unavailable" stubs, so no caller needs an #ifdef.
list(APPEND SOURCES
    src/core/embedder.h
    src/core/embedder.cpp
    src/core/embedder_builder.h
    src/core/embedder_builder.cpp
)

# On Windows, embed the application icon into the executable
if(WIN32)
    list(APPEND SOURCES lucidgrasp.rc)
endif()

add_executable(LucidGrasp ${SOURCES})

target_include_directories(LucidGrasp PRIVATE src)
target_link_libraries(LucidGrasp PRIVATE ${OpenCV_LIBS})
target_link_libraries(LucidGrasp PRIVATE Qt6::Widgets Qt6::Network Threads::Threads)

if(LUCIDGRASP_HAVE_ORT)
    target_compile_definitions(LucidGrasp PRIVATE LUCIDGRASP_HAVE_ORT)
    target_include_directories(LucidGrasp PRIVATE "${_ort_include}")
    target_link_libraries(LucidGrasp PRIVATE "${_ort_lib}")
endif()

# One place to bump a release. main.cpp used to carry a literal version string
# of its own, which is exactly the kind of drift that makes an update checker
# report a phantom update on every launch for every user; the tag, this
# definition and the shipped binary now have to agree or the checker falls
# silent instead of lying.
target_compile_definitions(LucidGrasp PRIVATE
    LUCIDGRASP_VERSION="${PROJECT_VERSION}"
    LUCIDGRASP_REPO="${LUCIDGRASP_REPO}"
)

if(MSVC)
    target_compile_options(LucidGrasp PRIVATE /W3)
else()
    target_compile_options(LucidGrasp PRIVATE -Wall -Wextra)
endif()

# Linux system installation
if(UNIX AND NOT APPLE)
    install(TARGETS LucidGrasp DESTINATION bin)
    install(FILES lucidgrasp.desktop DESTINATION share/applications)
    install(FILES lucidgrasp.png DESTINATION share/icons/hicolor/256x256/apps RENAME lucidgrasp.png)
endif()
