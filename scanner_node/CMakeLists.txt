cmake_minimum_required(VERSION 3.16)

project(orkestr_scanner_node VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(cjson STATIC ../cJSON/cJSON.c)
target_include_directories(cjson PUBLIC ../cJSON)

add_library(scanner_protocol STATIC
    src/device_config.c
    src/protocol.c
)
target_include_directories(scanner_protocol PUBLIC include)
target_link_libraries(scanner_protocol PUBLIC cjson)
if(UNIX)
    target_link_libraries(scanner_protocol PUBLIC m)
endif()

add_executable(scanner_node
    src/main.c
    src/udp_socket.c
)
target_include_directories(scanner_node PRIVATE include)
target_link_libraries(scanner_node PRIVATE scanner_protocol)

if(WIN32)
    target_link_libraries(scanner_node PRIVATE ws2_32)
endif()

if(MSVC)
    target_compile_options(scanner_protocol PRIVATE /W4)
    target_compile_options(scanner_node PRIVATE /W4)
else()
    target_compile_options(scanner_protocol PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(scanner_node PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(scanner_protocol_tests tests/protocol_tests.c)
    target_link_libraries(scanner_protocol_tests PRIVATE scanner_protocol)
    if(MSVC)
        target_compile_options(scanner_protocol_tests PRIVATE /W4)
    else()
        target_compile_options(scanner_protocol_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME scanner_protocol_tests COMMAND scanner_protocol_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/../device.json"
        "${CMAKE_CURRENT_BINARY_DIR}/device-roundtrip.json")

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
    if(MSVC)
        target_compile_options(scanner_udp_integration_tests PRIVATE /W4)
    else()
        target_compile_options(scanner_udp_integration_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME scanner_udp_integration_tests COMMAND scanner_udp_integration_tests)
endif()