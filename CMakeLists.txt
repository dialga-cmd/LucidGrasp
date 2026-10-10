cmake_minimum_required(VERSION 3.16)
project(LucidGrasp VERSION 1.2.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
find_package(Threads REQUIRED)

if(UNIX AND NOT APPLE)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
endif()

set(LUCIDGRASP_REPO "dialga-cmd/LucidGrasp"
    CACHE STRING "GitHub owner/repo that the update checker queries")

set(SOURCES
    src/main.cpp
    src/selftest.cpp
    src/app/update_checker.h
    src/app/update_checker.cpp
    src/core/cv_matcher.h
    src/core/cv_matcher.cpp
    src/core/features.h
    src/core/features.cpp
    src/core/index.h
    src/core/index.cpp
    src/core/semantic_embedder.h
    src/core/semantic_embedder.cpp
    src/ui/mainwindow.h
    src/ui/mainwindow.cpp
    src/ui/file_actions.h
    src/ui/file_actions.cpp
    src/ui/model_download.h
    src/ui/model_download.cpp
)

set(LUCIDGRASP_ONNXRUNTIME_ROOT ""
    CACHE PATH "Root directory of a prebuilt ONNX Runtime package")

if(NOT LUCIDGRASP_ONNXRUNTIME_ROOT
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/include/onnxruntime_cxx_api.h")
    set(LUCIDGRASP_ONNXRUNTIME_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime")
endif()

if(WIN32)
    list(APPEND SOURCES lucidgrasp.rc)
endif()

add_executable(LucidGrasp ${SOURCES})

target_include_directories(LucidGrasp PRIVATE src)
find_package(OpenCV REQUIRED)
target_link_libraries(LucidGrasp PRIVATE ${OpenCV_LIBS})
find_package(ONNXRuntime REQUIRED)
target_link_libraries(LucidGrasp PRIVATE ONNXRuntime::ONNXRuntime)
target_link_libraries(LucidGrasp PRIVATE Qt6::Widgets Qt6::Network Threads::Threads)

if(ONNXRUNTIME_LIBRARY)
    get_filename_component(ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_LIBRARY}" DIRECTORY)
    if(UNIX AND NOT APPLE)
        # $ORIGIN lets a relocated Linux build (release tarball, AppImage,
        # RPM) find the ONNX Runtime shared library shipped alongside it.
        set_target_properties(LucidGrasp PROPERTIES
            BUILD_RPATH "${ONNXRUNTIME_LIB_DIR};$ORIGIN"
            INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib")
    else()
        set_target_properties(LucidGrasp PROPERTIES
            BUILD_RPATH "${ONNXRUNTIME_LIB_DIR}")
    endif()
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(LucidGrasp PRIVATE Qt6::DBus)
endif()

target_compile_definitions(LucidGrasp PRIVATE
    LUCIDGRASP_VERSION="${PROJECT_VERSION}"
    LUCIDGRASP_REPO="${LUCIDGRASP_REPO}"
)

if(MSVC)
    target_compile_options(LucidGrasp PRIVATE /W3)
else()
    target_compile_options(LucidGrasp PRIVATE -Wall -Wextra)
endif()

set(MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")

if(APPLE)
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")

    set(LUCIDGRASP_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/lucidgrasp.icns"
        CACHE FILEPATH "Path to the macOS .icns; see make-icns.sh")

    set_target_properties(LucidGrasp PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "LucidGrasp"
        MACOSX_BUNDLE_GUI_IDENTIFIER "local.lucidgrasp.LucidGrasp"
    )

    if(EXISTS "${LUCIDGRASP_ICNS}")
        set_source_files_properties("${LUCIDGRASP_ICNS}"
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        target_sources(LucidGrasp PRIVATE "${LUCIDGRASP_ICNS}")
        set_target_properties(LucidGrasp PROPERTIES
            MACOSX_BUNDLE_ICON_FILE "lucidgrasp.icns")
    else()
        message(WARNING
            "lucidgrasp.icns not found at ${LUCIDGRASP_ICNS}; the bundle will "
            "have no icon. Run make-icns.sh to generate it. This is "
            "expected for a local build and not for a release.")
    endif()
endif()

if(UNIX AND NOT APPLE)
    install(TARGETS LucidGrasp DESTINATION bin)
    install(FILES io.github.dialga_cmd.LucidGrasp.desktop DESTINATION share/applications)
    install(FILES io.github.dialga_cmd.LucidGrasp.metainfo.xml DESTINATION share/metainfo)
    install(FILES lucidgrasp.png DESTINATION share/icons/hicolor/256x256/apps RENAME io.github.dialga_cmd.LucidGrasp.png)
    install(FILES LICENSE README.md PRIVACY.md TERMS.md DISCLAIMER.md
            ACCEPTABLE_USE.md LEGAL.md CHANGELOG.md
            DESTINATION share/doc/lucidgrasp)
endif()
