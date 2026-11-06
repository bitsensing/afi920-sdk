function(add_example name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE afi_sdk)
endfunction()

add_example(sample_discovery)
add_example(sample_pointcloud)
add_example(sample_data_stream)
add_example(sample_config)
add_example(sample_multi_sensor)
add_example(set_stream_destination)
add_example(restart_sensor)

# sample_discovery enumerates adapters with GetAdaptersAddresses. MSVC picks the
# library up from the #pragma in the source; MinGW ignores that pragma.
if(WIN32)
    target_link_libraries(sample_discovery PRIVATE iphlpapi)
endif()
