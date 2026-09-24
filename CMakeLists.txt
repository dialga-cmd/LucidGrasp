cmake_minimum_required(VERSION 3.16)
project(image-search VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

add_executable(image_search
    src/main.cpp
    src/selftest.cpp
    src/core/clip_embedder.h
    src/core/clip_embedder.cpp
    src/core/features.h
    src/core/features.cpp
    src/core/index.h
    src/core/index.cpp
    src/ui/mainwindow.h
    src/ui/mainwindow.cpp
)

target_include_directories(image_search PRIVATE src)
set(ORT_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime")
if(EXISTS "${ORT_ROOT}/include/onnxruntime_cxx_api.h")
  target_include_directories(image_search PRIVATE "${ORT_ROOT}/include")
endif()
find_library(ONNXRUNTIME_LIB onnxruntime PATHS "${ORT_ROOT}/lib" NO_DEFAULT_PATH)
if(ONNXRUNTIME_LIB)
  target_link_libraries(image_search PRIVATE "${ONNXRUNTIME_LIB}")
endif()
target_link_libraries(image_search PRIVATE Qt6::Widgets Threads::Threads)
target_compile_options(image_search PRIVATE -Wall -Wextra)
