cmake_minimum_required(VERSION 3.24)

project(tunlet VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(GNUInstallDirs)
include(CTest)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Svg)
find_package(yaml-cpp REQUIRED)
find_library(MAXMINDDB_LIBRARY NAMES maxminddb REQUIRED)
find_path(MAXMINDDB_INCLUDE_DIR NAMES maxminddb.h REQUIRED)

qt_standard_project_setup()

set(TUNLET_SOURCES
    src/app/application_paths.cpp
    src/app/runtime_config_applier.cpp
    src/core/logical_mode.cpp
    src/config/config_loader.cpp
    src/config/config_file_service.cpp
    src/clash/clash_api_client.cpp
    src/diagnostics/geoip_provider.cpp
    src/clash/mode_controller.cpp
    src/diagnostics/diagnostics_parsing.cpp
    src/rules/ruleset_service.cpp
    src/diagnostics/diagnostics_service.cpp
    src/logging/logging_service.cpp
    src/theme/theme_loader.cpp
    src/ui/editor_highlighting.cpp
    src/ui/main_window.cpp
    src/ui/tray_controller.cpp
    resources/resources.qrc
)

add_library(tunlet_core STATIC ${TUNLET_SOURCES})
target_include_directories(tunlet_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(tunlet_core PRIVATE ${MAXMINDDB_INCLUDE_DIR})
target_link_libraries(
    tunlet_core
    PUBLIC
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
        Qt6::Svg
        yaml-cpp
        ${MAXMINDDB_LIBRARY}
)

qt_add_executable(tunlet src/app/main.cpp)
target_link_libraries(tunlet PRIVATE tunlet_core)

set_target_properties(tunlet PROPERTIES
    OUTPUT_NAME tunlet
)

install(TARGETS tunlet RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES resources/tunlet.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/icons/tunlet.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES config/config.example.yaml DESTINATION ${CMAKE_INSTALL_DATADIR}/tunlet)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    find_package(Catch2 3 REQUIRED)
    add_executable(tunlet_tests
        tests/clash_api_client_tests.cpp
        tests/config_file_service_tests.cpp
        tests/config_loader_tests.cpp
        tests/diagnostics_parsing_tests.cpp
        tests/geoip_provider_tests.cpp
        tests/logging_service_tests.cpp
        tests/logical_mode_tests.cpp
        tests/mode_selection_state_tests.cpp
        tests/ruleset_service_tests.cpp
        tests/theme_loader_tests.cpp
    )
    target_link_libraries(
        tunlet_tests
        PRIVATE
            tunlet_core
            Catch2::Catch2WithMain
            Qt6::Test
    )
    include(Catch)
    catch_discover_tests(tunlet_tests)
endif()
