cmake_minimum_required(VERSION 3.24)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" DSO138_STUDIO_VERSION LIMIT_COUNT 1)
project(DSO138Studio VERSION ${DSO138_STUDIO_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets SerialPort)
set(DSO_SHARED_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/include")

add_library(dso_shared_contracts INTERFACE)
target_include_directories(dso_shared_contracts
    INTERFACE "${DSO_SHARED_INCLUDE_DIR}")
# Keep shared headers listed on their actual consumers below so IDE targets
# show direct use; this interface target propagates only the include requirement.

function(dso_compile_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(dso_capture STATIC
    "${DSO_SHARED_INCLUDE_DIR}/dso_adc_contract.hpp"
    "${DSO_SHARED_INCLUDE_DIR}/dso_capture_contract.hpp"
    "${DSO_SHARED_INCLUDE_DIR}/dso_calibration_contract.hpp"
    "${DSO_SHARED_INCLUDE_DIR}/dso_instrument_codes.hpp"
    "${DSO_SHARED_INCLUDE_DIR}/dso_settings_contract.hpp"
    "${DSO_SHARED_INCLUDE_DIR}/dso_timebase_contract.hpp"
    "${DSO_SHARED_INCLUDE_DIR}/dso_voltage_math.hpp"
    src/instrument_codes.hpp
    src/capture.cpp src/capture_model.hpp src/capture.hpp
    src/demo_capture.cpp src/demo_capture.hpp
    src/capture_io.hpp
    src/capture_internal.hpp src/capture_io.cpp
    src/measurements.cpp src/measurements.hpp
    src/spectrum.cpp src/spectrum.hpp)
target_include_directories(dso_capture PUBLIC src)
target_link_libraries(dso_capture
    PUBLIC dso_shared_contracts Qt6::Core)
dso_compile_options(dso_capture)

add_library(dso_protocol STATIC
    src/instrument_codes.hpp
    src/protocol_request_payload.hpp
    src/wire_endian.hpp
    src/protocol.cpp src/protocol.hpp
    "${DSO_SHARED_INCLUDE_DIR}/dso_adc_contract.hpp"
    "${DSO_SHARED_INCLUDE_DIR}/dso_capture_contract.hpp"
    "${DSO_SHARED_INCLUDE_DIR}/dso_instrument_codes.hpp"
    "${DSO_SHARED_INCLUDE_DIR}/dso_settings_contract.hpp"
    "${DSO_SHARED_INCLUDE_DIR}/dso_usb_protocol_contract.hpp"
    "${DSO_SHARED_INCLUDE_DIR}/dso_binary_endian.hpp"
    "${DSO_SHARED_INCLUDE_DIR}/dso_crc32.hpp")
target_include_directories(dso_protocol PUBLIC src)
target_link_libraries(dso_protocol
    PUBLIC dso_shared_contracts Qt6::Core)
dso_compile_options(dso_protocol)

add_library(dso_transport STATIC src/byte_transport.hpp)
set_target_properties(dso_transport PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(dso_transport PUBLIC src)
target_link_libraries(dso_transport PUBLIC Qt6::Core)
dso_compile_options(dso_transport)

add_library(dso_serial_transport STATIC
    src/serial_transport.cpp src/serial_transport.hpp)
target_include_directories(dso_serial_transport PUBLIC src)
target_link_libraries(dso_serial_transport
    PUBLIC dso_transport
    PRIVATE Qt6::SerialPort)
dso_compile_options(dso_serial_transport)

add_library(dso_session STATIC
    src/instrument_codes.hpp
    src/instrument_labels.hpp
    src/protocol_request_payload.hpp
    src/wire_endian.hpp
    src/session_wire.cpp src/session_wire.hpp
    src/session_timing.hpp
    src/session_capture.cpp
    src/session_frame_transfer.cpp
    src/session_settings.cpp
    src/session.cpp src/session_reply.cpp src/session_serial_adapter.cpp src/session.hpp
    "${DSO_SHARED_INCLUDE_DIR}/dso_adc_contract.hpp"
    "${DSO_SHARED_INCLUDE_DIR}/dso_settings_contract.hpp"
    "${DSO_SHARED_INCLUDE_DIR}/dso_timebase_contract.hpp"
    "${DSO_SHARED_INCLUDE_DIR}/dso_usb_protocol_contract.hpp"
    "${DSO_SHARED_INCLUDE_DIR}/dso_binary_endian.hpp")
target_link_libraries(dso_session
    PRIVATE dso_shared_contracts)
# Qt's generated Session metatype references its public default constructor,
# even for injected-transport clients. Carry its serial adapter link here.
target_link_libraries(dso_session
    PUBLIC dso_capture dso_protocol dso_transport Qt6::Core
    PRIVATE dso_serial_transport)
dso_compile_options(dso_session)

add_library(dso_ui STATIC
    "${DSO_SHARED_INCLUDE_DIR}/dso_adc_contract.hpp"
    "${DSO_SHARED_INCLUDE_DIR}/dso_settings_contract.hpp"
    "${DSO_SHARED_INCLUDE_DIR}/dso_timebase_contract.hpp"
    src/instrument_codes.hpp
    src/instrument_labels.hpp
    src/waveform.cpp src/waveform.hpp
    src/spectrum_view.cpp src/spectrum_view.hpp
    src/trigger_controls.cpp src/trigger_controls.hpp
    src/window_capture_files.cpp
    src/window_format.cpp src/window_format.hpp
    src/window_records.cpp
    src/window_ui.cpp
    src/window.cpp src/window.hpp)
target_link_libraries(dso_ui
    PRIVATE dso_shared_contracts)
target_link_libraries(dso_ui
    PUBLIC dso_session Qt6::Widgets
    PRIVATE dso_serial_transport)
dso_compile_options(dso_ui)

qt_add_executable(DSO138Studio WIN32 src/main.cpp)
target_link_libraries(DSO138Studio PRIVATE dso_ui)
target_compile_definitions(DSO138Studio PRIVATE DSO138_STUDIO_VERSION="${PROJECT_VERSION}")
dso_compile_options(DSO138Studio)
install(TARGETS DSO138Studio RUNTIME DESTINATION .)
install(FILES README.md THIRD_PARTY_NOTICES.md DESTINATION .)
