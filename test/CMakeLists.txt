function(add_sdk_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE afi_sdk)
endfunction()

# Unit tests need internal SDK headers (src/protocol/, util/)
function(add_unit_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE afi_sdk)
    target_include_directories(${name} PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/third_party
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Unit tests (no sensor required)
add_unit_test(test_someip)
add_unit_test(test_someip_tp)
add_unit_test(test_e2e)
add_unit_test(test_rdi_parser)
add_unit_test(test_shi_parser)
add_unit_test(test_spi_parser)
add_unit_test(test_csii_builder)

# Integration tests (sensor required, public API only — not registered with CTest)
add_sdk_test(test_discovery)
add_sdk_test(test_config_api)
add_sdk_test(test_data_receive)
add_sdk_test(test_csii_send)

# Python unit tests (no sensor required) — run if a Python interpreter exists
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_test(NAME test_python_streams
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_python_streams.py)
endif()
