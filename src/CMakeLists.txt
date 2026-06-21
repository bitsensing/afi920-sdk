add_library(afi_sdk STATIC
    afi_sdk.cpp
    protocol/someip.cpp
    protocol/someip_tp.cpp
    protocol/e2e.cpp
    protocol/csii_builder.cpp
    protocol/rdi_parser.cpp
    protocol/shi_parser.cpp
    protocol/spi_parser.cpp
    transport/udp_receiver.cpp
    transport/tcp_client.cpp
    transport/tcp_sender.cpp
    transport/tcp_stream_client.cpp
    config_api/config_client.cpp
    discovery/discovery.cpp
    util/logger.cpp
)

target_include_directories(afi_sdk
    PUBLIC  ${PROJECT_SOURCE_DIR}/include
    PUBLIC  ${PROJECT_SOURCE_DIR}/third_party/afi920_interface/src/c
    PRIVATE ${PROJECT_SOURCE_DIR}/src
    PRIVATE ${PROJECT_SOURCE_DIR}/third_party
)

target_compile_features(afi_sdk PUBLIC cxx_std_17)

# Platform-specific linking
if(WIN32)
    target_link_libraries(afi_sdk PRIVATE ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(afi_sdk PRIVATE Threads::Threads)
endif()
