cmake_minimum_required(VERSION 3.16)

project(stylus-popup VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Install layout ─────────────────────────────────────────────────────────
# Land in the system prefix (/usr/bin, /usr/lib/systemd/user, ...) instead of
# the CMake default of /usr/local, so a plain `cmake --install` ends up next to
# the rest of the system. The RPM spec overrides the prefix explicitly.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install prefix" FORCE)
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Gui DBus)

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
    src/main.cpp
    src/stylusmonitor.h
    src/stylusmonitor.cpp
    src/waylandlayersurface.h
    src/waylandlayersurface.cpp
    src/popupwidget.h
    src/popupwidget.cpp
    src/popupconfig.h
    src/spring.h
    src/islandstrings.h
    src/islandstrings.cpp
    src/appconfig.h
    src/appconfig.cpp
    src/colortheme.h
    src/colortheme.cpp
    src/bluezmanager.h
    src/bluezmanager.cpp
    src/stylusgestures.h
    src/stylusgestures.cpp
    src/stylusbuttons.h
    src/stylusbuttons.cpp
    ${PROTOCOL_HEADER}
    ${PROTOCOL_SRC}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(stylus-popup
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    add_executable(stylus-popup ${PROJECT_SOURCES})
endif()

# Generated headers are in the build dir
target_include_directories(stylus-popup PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(stylus-popup PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::DBus
    ${WAYLAND_CLIENT_LIBRARIES}
)

target_include_directories(stylus-popup PRIVATE
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
)

# ── Install ────────────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS stylus-popup
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Session-scoped systemd *user* unit: packaged system-wide, started by the
# user manager once the graphical session appears. Enable it per user with
#   systemctl --user enable --now stylus-popup.service
# Only the binary path is substituted, so no user has to be resolved at
# configure time and no build machine detail leaks into the unit.
set(STYLUS_USER_UNIT_DIR "${CMAKE_INSTALL_LIBDIR}/systemd/user" CACHE PATH
    "Directory for the systemd user unit")

configure_file(packaging/stylus-popup.service.in
    "${CMAKE_CURRENT_BINARY_DIR}/stylus-popup.service" @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/stylus-popup.service"
    DESTINATION ${STYLUS_USER_UNIT_DIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(stylus-popup)
endif()
