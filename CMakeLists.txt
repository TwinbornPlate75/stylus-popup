cmake_minimum_required(VERSION 3.16)

project(stylus-popup VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Gui)

# ── Wayland layer-shell protocol code generation ───────────────────────────
include(FindPkgConfig)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)

set(PROTOCOL_XML
    ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml)

set(PROTOCOL_HEADER
    ${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h)
set(PROTOCOL_SRC
    ${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-client-protocol.c)

add_custom_command(
    OUTPUT  ${PROTOCOL_HEADER}
    COMMAND ${WAYLAND_SCANNER} client-header ${PROTOCOL_XML} ${PROTOCOL_HEADER}
    DEPENDS ${PROTOCOL_XML}
    COMMENT "Generating layer-shell client header"
)
add_custom_command(
    OUTPUT  ${PROTOCOL_SRC}
    COMMAND ${WAYLAND_SCANNER} private-code ${PROTOCOL_XML} ${PROTOCOL_SRC}
    DEPENDS ${PROTOCOL_XML}
    COMMENT "Generating layer-shell client code"
)

set_source_files_properties(${PROTOCOL_SRC}    PROPERTIES GENERATED TRUE LANGUAGE C)
set_source_files_properties(${PROTOCOL_HEADER} PROPERTIES GENERATED TRUE)

# ── Sources ────────────────────────────────────────────────────────────────
set(PROJECT_SOURCES
    main.cpp
    stylusmonitor.h
    stylusmonitor.cpp
    waylandlayersurface.h
    waylandlayersurface.cpp
    popupwidget.h
    popupwidget.cpp
    colortheme.h
    colortheme.cpp
    ${PROTOCOL_HEADER}
    ${PROTOCOL_SRC}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(stylus-popup
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    if(ANDROID)
        add_library(stylus-popup SHARED ${PROJECT_SOURCES})
    else()
        add_executable(stylus-popup ${PROJECT_SOURCES})
    endif()
endif()

# Generated headers are in the build dir
target_include_directories(stylus-popup PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(stylus-popup PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Gui
    ${WAYLAND_CLIENT_LIBRARIES}
)

target_include_directories(stylus-popup PRIVATE
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
)

if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.stylus-popup)
endif()
set_target_properties(stylus-popup PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS stylus-popup
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(stylus-popup)
endif()
