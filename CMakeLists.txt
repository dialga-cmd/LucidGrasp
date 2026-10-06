cmake_minimum_required(VERSION 3.16)
project(LucidGrasp VERSION 1.2.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
find_package(Threads REQUIRED)

# Qt6::DBus is Linux/BSD only, and only because ui::revealInFileManager() speaks
# org.freedesktop.FileManager1 over the session bus there. Windows uses
# explorer.exe /select and macOS uses open -R, neither of which touches D-Bus.
#
# Requested conditionally rather than listed unconditionally because Qt does ship
# a QtDBus module for macOS, but it is a stub there: there is no session bus, so
# the module cannot do anything this app needs. Requiring it would add a real
# build dependency for code that is compiled out by the Q_OS_* guards anyway.
if(UNIX AND NOT APPLE)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
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
    src/core/features.h
    src/core/features.cpp
    src/core/index.h
    src/core/index.cpp
    src/ui/mainwindow.h
    src/ui/mainwindow.cpp
    src/ui/file_actions.h
    src/ui/file_actions.cpp
)

# On Windows, embed the application icon into the executable
if(WIN32)
    list(APPEND SOURCES lucidgrasp.rc)
endif()

add_executable(LucidGrasp ${SOURCES})

target_include_directories(LucidGrasp PRIVATE src)
find_package(OpenCV REQUIRED)
target_link_libraries(LucidGrasp PRIVATE ${OpenCV_LIBS})
target_link_libraries(LucidGrasp PRIVATE Qt6::Widgets Qt6::Network Threads::Threads)

if(UNIX AND NOT APPLE)
    target_link_libraries(LucidGrasp PRIVATE Qt6::DBus)
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

# On macOS the executable has to become an application bundle, not a bare binary
# in a directory. Finder will not launch a loose Mach-O, and macdeployqt deploys
# Qt's frameworks into a bundle rather than beside a file.
set(MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")

if(APPLE)
    # Set from project(... VERSION ...) rather than typed out, for the same reason
    # LUCIDGRASP_VERSION is: the bundle's own metadata, the binary's compiled-in
    # version and the git tag all have to agree, and there is only one place to
    # get that right.
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")

    # Generated here rather than committed as a binary, so the icns cannot drift
    # out of sync with lucidgrasp.png. iconutil is the only supported route: it
    # wants a fixed set of named PNGs in a .iconset directory, and rejecting a
    # hand-assembled .icns for having a size it does not recognise is the usual
    # failure mode. Runs at build time because iconutil is a macOS-only tool and
    # committing the artefact would put an opaque binary in the tree.
    set(_iconset "${CMAKE_CURRENT_BINARY_DIR}/lucidgrasp.iconset")
    file(MAKE_DIRECTORY "${_iconset}")
    foreach(_size 16 32 128 256 512)
        # Each logical size needs a 1x and a 2x rendition; Finder picks between
        # them per display density.
        find_program(SIPS_EXECUTABLE sips REQUIRED)
        execute_process(
            COMMAND "${SIPS_EXECUTABLE}" -z ${_size} ${_size}
                    "${CMAKE_CURRENT_SOURCE_DIR}/lucidgrasp.png"
                    --out "${_iconset}/icon_${_size}x${_size}.png"
            RESULT_VARIABLE _sips_rc OUTPUT_QUIET ERROR_QUIET)
        if(NOT _sips_rc EQUAL 0)
            message(FATAL_ERROR "sips failed to build the ${_size}px icon")
        endif()
        math(EXPR _double "${_size} * 2")
        execute_process(
            COMMAND "${SIPS_EXECUTABLE}" -z ${_double} ${_double}
                    "${CMAKE_CURRENT_SOURCE_DIR}/lucidgrasp.png"
                    --out "${_iconset}/icon_${_size}x${_size}@2x.png"
            RESULT_VARIABLE _sips_rc OUTPUT_QUIET ERROR_QUIET)
        if(NOT _sips_rc EQUAL 0)
            message(FATAL_ERROR "sips failed to build the ${_size}px@2x icon")
        endif()
    endforeach()

    find_program(ICONUTIL_EXECUTABLE iconutil REQUIRED)
    # iconutil refuses to overwrite, so the output is cleared first. Without this
    # a rebuild fails on a stale .icns from a previous configure.
    file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/lucidgrasp.icns")
    execute_process(
        COMMAND "${ICONUTIL_EXECUTABLE}" -c icns "${_iconset}"
                "${CMAKE_CURRENT_BINARY_DIR}/lucidgrasp.icns"
        RESULT_VARIABLE _iconutil_rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT _iconutil_rc EQUAL 0)
        message(FATAL_ERROR "iconutil failed to produce lucidgrasp.icns")
    endif()

    set_target_properties(LucidGrasp PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "LucidGrasp"
        MACOSX_BUNDLE_GUI_IDENTIFIER "local.lucidgrasp.LucidGrasp"
        # The bundle looks for this next to the built executable, and the
        # generated file lands in the binary dir, which is exactly there.
        MACOSX_BUNDLE_ICON_FILE "lucidgrasp.icns"
    )
endif()

# Linux system installation
if(UNIX AND NOT APPLE)
    install(TARGETS LucidGrasp DESTINATION bin)
    install(FILES lucidgrasp.desktop DESTINATION share/applications)
    install(FILES lucidgrasp.png DESTINATION share/icons/hicolor/256x256/apps RENAME lucidgrasp.png)
endif()
