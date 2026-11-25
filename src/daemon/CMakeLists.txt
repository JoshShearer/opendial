# SPDX-License-Identifier: MIT
set(SRC ${CMAKE_SOURCE_DIR}/src)

set(SERVICE_SOURCES
  ${SRC}/device/DialDevice.cpp
  ${SRC}/inject/Injector.h
  ${SRC}/inject/InjectorManager.cpp
  ${SRC}/inject/Keymap.cpp
  ${SRC}/inject/PortalInjector.cpp
  ${SRC}/inject/UInputInjector.cpp
  ${SRC}/context/FocusTrackers.cpp
  ${SRC}/integrations/Integrations.cpp
  ${SRC}/common/IconProvider.cpp
  ${SRC}/common/SystemTheme.cpp
  ${SRC}/common/icons.qrc
  ActionRunner.cpp
  CommandRunner.cpp
  Engine.cpp
  DBusService.cpp
  HttpApi.cpp
  Overlay.cpp
  Tray.cpp
  qml/overlay.qrc
)

set(SERVICE_DEFS "")
set(SERVICE_LIBS Qt6::Core Qt6::DBus Qt6::Gui Qt6::Network Qt6::Widgets Qt6::Quick Qt6::Svg PkgConfig::UDEV PkgConfig::XKBCOMMON opendialcore)

if(OPENDIAL_WITH_WAYLAND)
  set(PROTO_OUT ${CMAKE_CURRENT_BINARY_DIR}/protocols)
  file(MAKE_DIRECTORY ${PROTO_OUT})
  foreach(proto virtual-keyboard-unstable-v1 wlr-virtual-pointer-unstable-v1 wlr-foreign-toplevel-management-unstable-v1
                ext-foreign-toplevel-list-v1 cosmic-toplevel-info-unstable-v1)
    set(xml ${CMAKE_SOURCE_DIR}/protocols/${proto}.xml)
    add_custom_command(
      OUTPUT ${PROTO_OUT}/${proto}-client-protocol.h ${PROTO_OUT}/${proto}-protocol.c
      COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${PROTO_OUT}/${proto}-client-protocol.h
      COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${PROTO_OUT}/${proto}-protocol.c
      DEPENDS ${xml} VERBATIM)
    list(APPEND SERVICE_SOURCES ${PROTO_OUT}/${proto}-client-protocol.h ${PROTO_OUT}/${proto}-protocol.c)
  endforeach()
  list(APPEND SERVICE_SOURCES
    ${SRC}/inject/WaylandClient.cpp
    ${SRC}/inject/WaylandInjector.cpp
    ${SRC}/context/WlrToplevelTracker.cpp
    ${SRC}/context/CosmicToplevelTracker.cpp)
  list(APPEND SERVICE_DEFS OPENDIAL_HAVE_WAYLAND)
  list(APPEND SERVICE_LIBS PkgConfig::WAYLAND_CLIENT)
endif()

if(OPENDIAL_WITH_X11)
  list(APPEND SERVICE_SOURCES ${SRC}/inject/X11Injector.cpp ${SRC}/context/X11Tracker.cpp)
  list(APPEND SERVICE_DEFS OPENDIAL_HAVE_X11)
  list(APPEND SERVICE_LIBS PkgConfig::X11DEPS)
endif()

if(OPENDIAL_HAVE_LAYERSHELL)
  list(APPEND SERVICE_DEFS OPENDIAL_HAVE_LAYERSHELL)
  list(APPEND SERVICE_LIBS LayerShellQt::Interface)
  # 6.6 replaced setScreenConfiguration() with setWantsToBeOnActiveScreen()
  if(LayerShellQt_VERSION VERSION_GREATER_EQUAL 6.6)
    list(APPEND SERVICE_DEFS OPENDIAL_LAYERSHELL_ACTIVE_SCREEN_API)
  endif()
endif()

# Everything except main(), so integration tests link the real service.
add_library(opendialsvc STATIC ${SERVICE_SOURCES})
target_include_directories(opendialsvc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR} ${SRC}/device ${SRC}/inject ${SRC}/context ${SRC}/integrations ${SRC}/common
  ${CMAKE_CURRENT_BINARY_DIR}/protocols)
target_compile_definitions(opendialsvc PUBLIC ${SERVICE_DEFS})
target_link_libraries(opendialsvc PUBLIC ${SERVICE_LIBS})

add_executable(opendiald main.cpp)
target_link_libraries(opendiald PRIVATE opendialsvc)
install(TARGETS opendiald RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
