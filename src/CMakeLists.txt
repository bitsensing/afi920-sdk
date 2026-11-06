include(GNUInstallDirs)

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
add_library(afi::afi_sdk ALIAS afi_sdk)

# Interface headers the public API includes; the install copies these only.
set(AFI_INTERFACE_DIR ${PROJECT_SOURCE_DIR}/third_party/afi920_interface/src/c)
set(AFI_INTERFACE_HEADERS
    bts_iso23150.h
    bts_iso23150_e2e.h
    AFI920/bts_iso23150_rdi.h
    AFI920/bts_iso23150_shi.h
    AFI920/bts_iso23150_spi.h
    AFI920/bts_iso23150_csii.h
)

target_include_directories(afi_sdk
    PUBLIC  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${AFI_INTERFACE_DIR}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/afi920_interface>
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

# ─── Install: find_package(afi_sdk) provides afi::afi_sdk ───

include(CMakePackageConfigHelpers)
set(AFI_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/afi_sdk)

install(TARGETS afi_sdk EXPORT afi_sdkTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/afi
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
foreach(header IN LISTS AFI_INTERFACE_HEADERS)
    get_filename_component(subdir ${header} DIRECTORY)
    install(FILES ${AFI_INTERFACE_DIR}/${header}
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/afi920_interface/${subdir})
endforeach()
install(EXPORT afi_sdkTargets NAMESPACE afi:: DESTINATION ${AFI_CMAKE_DIR})

configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/afi_sdkConfig.cmake.in
                              ${CMAKE_CURRENT_BINARY_DIR}/afi_sdkConfig.cmake
                              INSTALL_DESTINATION ${AFI_CMAKE_DIR})
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/afi_sdkConfigVersion.cmake
                                 VERSION ${PROJECT_VERSION}
                                 COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/afi_sdkConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/afi_sdkConfigVersion.cmake
        DESTINATION ${AFI_CMAKE_DIR})
