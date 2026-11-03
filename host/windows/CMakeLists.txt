cmake_minimum_required(VERSION 3.24)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" DSO138_STUDIO_VERSION LIMIT_COUNT 1)
project(DSO138Studio VERSION ${DSO138_STUDIO_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets SerialPort)

function(dso_compile_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(dso_capture STATIC
    src/capture.cpp src/capture.hpp
    src/spectrum.cpp src/spectrum.hpp)
target_include_directories(dso_capture PUBLIC src)
target_link_libraries(dso_capture PUBLIC Qt6::Core)
dso_compile_options(dso_capture)

add_library(dso_session STATIC src/protocol.cpp src/protocol.hpp src/session.cpp src/session.hpp)
target_link_libraries(dso_session PUBLIC dso_capture Qt6::SerialPort)
dso_compile_options(dso_session)

add_library(dso_ui STATIC
    src/waveform.cpp src/waveform.hpp
    src/spectrum_view.cpp src/spectrum_view.hpp
    src/trigger_controls.cpp src/trigger_controls.hpp
    src/window.cpp src/window.hpp)
target_link_libraries(dso_ui PUBLIC dso_session Qt6::Widgets)
dso_compile_options(dso_ui)

qt_add_executable(DSO138Studio WIN32 src/main.cpp)
target_link_libraries(DSO138Studio PRIVATE dso_ui)
target_compile_definitions(DSO138Studio PRIVATE DSO138_STUDIO_VERSION="${PROJECT_VERSION}")
dso_compile_options(DSO138Studio)
install(TARGETS DSO138Studio RUNTIME DESTINATION .)
install(FILES README.md THIRD_PARTY_NOTICES.md DESTINATION .)
