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
    target_compile_definitions(${name} PRIVATE
        AFI_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Unit tests (no sensor required)
add_unit_test(test_someip)
add_unit_test(test_someip_tp)
add_unit_test(test_e2e)
add_unit_test(test_rdi_parser)
add_unit_test(test_pmoi_parser)
target_sources(test_pmoi_parser PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/afi920_interface/src/c/AFI920/bts_iso23150_pmoi.c)
add_unit_test(test_shi_parser)
add_unit_test(test_spi_parser)
add_unit_test(test_csii_builder)
add_unit_test(test_wire_payload)
add_unit_test(test_config_e2e)
add_unit_test(test_config_transport)
add_unit_test(test_discovery_parse)

# Integration tests (sensor required, public API only — not registered with CTest)
add_sdk_test(test_discovery)
add_sdk_test(test_config_api)
add_sdk_test(test_data_receive)
add_sdk_test(test_csii_send)
add_sdk_test(test_firmware_live)

# Python unit tests (no sensor required) — run if a Python interpreter exists
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_test(NAME test_python_streams
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_python_streams.py)
    add_test(NAME test_python_sdk
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_python_sdk.py)
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import pytest"
                    RESULT_VARIABLE _pytest_dep OUTPUT_QUIET ERROR_QUIET)
    if(_pytest_dep EQUAL 0)
        add_test(NAME test_python_config_transport
                 COMMAND ${Python3_EXECUTABLE} -m pytest -q
                         ${CMAKE_CURRENT_SOURCE_DIR}/test_python_config_transport.py)
        add_test(NAME test_python_firmware
                 COMMAND ${Python3_EXECUTABLE} -m pytest -q
                         ${CMAKE_CURRENT_SOURCE_DIR}/test_python_firmware.py)
    else()
        message(STATUS "pytest-based Python tests skipped: pip install pytest")
    endif()
endif()
