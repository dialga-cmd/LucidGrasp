cmake_minimum_required(VERSION 3.16)
project(image-search VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

set(SOURCES
    src/main.cpp
    src/selftest.cpp
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

add_executable(image_search ${SOURCES})

target_include_directories(image_search PRIVATE src)
find_package(OpenCV REQUIRED)
target_link_libraries(image_search PRIVATE ${OpenCV_LIBS})
target_link_libraries(image_search PRIVATE Qt6::Widgets Threads::Threads)

if(MSVC)
    target_compile_options(image_search PRIVATE /W3)
else()
    target_compile_options(image_search PRIVATE -Wall -Wextra)
endif()

# Linux system installation
if(UNIX AND NOT APPLE)
    install(TARGETS image_search DESTINATION bin)
    install(FILES lucidgrasp.desktop DESTINATION share/applications)
    install(FILES lucidgrasp.png DESTINATION share/icons/hicolor/256x256/apps RENAME lucidgrasp.png)
endif()
