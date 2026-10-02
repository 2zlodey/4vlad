cmake_minimum_required(VERSION 3.16)

project(orkestr_scanner_node VERSION 0.1.0 LANGUAGES C)

find_package(Threads REQUIRED)

option(SCANNER_ENABLE_STUB_SDR "Provide an in-memory SDR frontend when no physical radio is found" ON)
option(SCANNER_ENABLE_PERF "Enable performance probes and the hardware sweep benchmark" ON)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

function(scanner_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(cjson STATIC ../cJSON/cJSON.c)
target_include_directories(cjson PUBLIC ../cJSON)

add_library(scanner_protocol STATIC
    src/device_config.c
    src/protocol.c
)
target_include_directories(scanner_protocol PUBLIC include)
target_link_libraries(scanner_protocol PUBLIC cjson)

add_library(scanner_dsp STATIC src/dsp.c src/dsp_features.c)
target_include_directories(scanner_dsp PUBLIC include)
if(UNIX)
    target_link_libraries(scanner_dsp PUBLIC m)
endif()

add_library(scanner_classifier STATIC src/signal_classifier.c)
target_include_directories(scanner_classifier PUBLIC include)
target_link_libraries(scanner_classifier PUBLIC scanner_dsp)

add_library(scanner_analysis STATIC src/analysis_worker.c)
target_include_directories(scanner_analysis PUBLIC include)
target_link_libraries(scanner_analysis PUBLIC scanner_classifier Threads::Threads)

add_library(scanner_perf STATIC
    perf_lib/perf_probe.c
    perf_lib/perf_probe_hal_host.c
)
target_include_directories(scanner_perf PUBLIC perf_lib)
target_link_libraries(scanner_perf PUBLIC Threads::Threads)
if(SCANNER_ENABLE_PERF)
    target_compile_definitions(scanner_perf PUBLIC CONFIG_PERF_ENABLE=1 CONFIG_PERF_STRINGS=1
                                                   CONFIG_PERF_LOG_ENABLE=1)
endif()

add_library(scanner_radio STATIC
    src/radio_frontend.c
    src/radio_capture_backend.c
)
target_include_directories(scanner_radio PUBLIC include)
target_link_libraries(scanner_radio PUBLIC scanner_dsp)
if(SCANNER_ENABLE_STUB_SDR)
    target_compile_definitions(scanner_radio PUBLIC SCANNER_ENABLE_STUB_SDR=1)
    message(STATUS "Stub SDR fallback enabled")
else()
    message(STATUS "Stub SDR fallback disabled")
endif()

find_path(SCANNER_BLADERF_INCLUDE_DIR NAMES libbladeRF.h)
find_library(SCANNER_BLADERF_LIBRARY NAMES bladerf bladeRF)
if(SCANNER_BLADERF_INCLUDE_DIR AND SCANNER_BLADERF_LIBRARY)
    target_compile_definitions(scanner_radio PRIVATE SCANNER_HAVE_BLADERF)
    target_include_directories(scanner_radio PRIVATE ${SCANNER_BLADERF_INCLUDE_DIR})
    target_link_libraries(scanner_radio PUBLIC ${SCANNER_BLADERF_LIBRARY})
    message(STATUS "BladeRF support enabled")
else()
    message(STATUS "BladeRF development files not found; backend disabled")
endif()

find_path(SCANNER_HACKRF_INCLUDE_DIR NAMES libhackrf/hackrf.h)
find_library(SCANNER_HACKRF_LIBRARY NAMES hackrf libhackrf)
if(SCANNER_HACKRF_INCLUDE_DIR AND SCANNER_HACKRF_LIBRARY)
    target_compile_definitions(scanner_radio PRIVATE SCANNER_HAVE_HACKRF)
    target_include_directories(scanner_radio PRIVATE ${SCANNER_HACKRF_INCLUDE_DIR})
    target_link_libraries(scanner_radio PUBLIC ${SCANNER_HACKRF_LIBRARY})
    message(STATUS "HackRF support enabled")
else()
    message(STATUS "HackRF development files not found; backend disabled")
endif()

add_executable(scanner_node
    src/main.c
    src/iq_recording.c
    src/radio_commands.c
    src/scanner_options.c
    src/scanner_session.c
    src/radio_worker.c
    src/udp_socket.c
)
target_include_directories(scanner_node PRIVATE include)
target_link_libraries(scanner_node PRIVATE scanner_protocol scanner_radio scanner_analysis scanner_perf
                                           Threads::Threads)

add_executable(scanner_bladerf_band_capture tools/bladerf_band_capture.c)
target_include_directories(scanner_bladerf_band_capture PRIVATE include)
target_link_libraries(scanner_bladerf_band_capture PRIVATE scanner_radio scanner_classifier)

add_executable(scanner_sweep_benchmark tools/sdr_sweep_benchmark.c)
target_include_directories(scanner_sweep_benchmark PRIVATE include)
target_link_libraries(scanner_sweep_benchmark PRIVATE scanner_radio scanner_analysis scanner_perf
                                                     Threads::Threads)

if(WIN32)
    target_link_libraries(scanner_node PRIVATE ws2_32)
endif()

foreach(target scanner_protocol scanner_dsp scanner_classifier scanner_analysis scanner_perf scanner_radio
               scanner_node scanner_bladerf_band_capture scanner_sweep_benchmark)
    scanner_enable_warnings(${target})
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_executable(scanner_protocol_tests tests/protocol_tests.c)
    target_link_libraries(scanner_protocol_tests PRIVATE scanner_protocol scanner_radio)
    scanner_enable_warnings(scanner_protocol_tests)
    add_test(NAME scanner_protocol_tests COMMAND scanner_protocol_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/../device.json"
        "${CMAKE_CURRENT_BINARY_DIR}/device-roundtrip.json")

    add_executable(scanner_dsp_tests tests/dsp_tests.c)
    target_link_libraries(scanner_dsp_tests PRIVATE scanner_classifier)
    scanner_enable_warnings(scanner_dsp_tests)
    add_test(NAME scanner_dsp_tests COMMAND scanner_dsp_tests)

    add_executable(scanner_analysis_worker_tests tests/analysis_worker_tests.c)
    target_link_libraries(scanner_analysis_worker_tests PRIVATE scanner_analysis)
    scanner_enable_warnings(scanner_analysis_worker_tests)
    add_test(NAME scanner_analysis_worker_tests COMMAND scanner_analysis_worker_tests)

    add_executable(scanner_udp_integration_tests
        tests/udp_integration_tests.c
        src/udp_socket.c
        src/protocol.c
    )
    target_include_directories(scanner_udp_integration_tests PRIVATE include)
    target_link_libraries(scanner_udp_integration_tests PRIVATE cjson)
    if(WIN32)
        target_link_libraries(scanner_udp_integration_tests PRIVATE ws2_32)
    endif()
    scanner_enable_warnings(scanner_udp_integration_tests)
    add_test(NAME scanner_udp_integration_tests COMMAND scanner_udp_integration_tests)

    add_executable(scanner_radio_worker_tests
        tests/radio_worker_tests.c
        src/radio_worker.c
    )
    target_include_directories(scanner_radio_worker_tests PRIVATE include)
    target_link_libraries(scanner_radio_worker_tests PRIVATE scanner_radio Threads::Threads)
    if(UNIX)
        target_compile_definitions(scanner_radio_worker_tests PRIVATE _POSIX_C_SOURCE=200809L)
    endif()
    if(WIN32)
        target_link_libraries(scanner_radio_worker_tests PRIVATE ws2_32)
    endif()
    scanner_enable_warnings(scanner_radio_worker_tests)
    add_test(NAME scanner_radio_worker_tests COMMAND scanner_radio_worker_tests)
endif()