cmake_minimum_required(VERSION 3.16)
project(LucidGrasp VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
find_package(Threads REQUIRED)

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
