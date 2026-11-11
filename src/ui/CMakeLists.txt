# SPDX-License-Identifier: MIT
set(SRC ${CMAKE_SOURCE_DIR}/src)
add_executable(opendial
  main.cpp
  Backend.cpp
  ${SRC}/inject/Keymap.cpp
  ${SRC}/common/IconProvider.cpp
  ${SRC}/common/SystemTheme.cpp
  ${SRC}/common/icons.qrc
  ui.qrc)
target_include_directories(opendial PRIVATE ${SRC}/inject ${SRC}/common)
target_link_libraries(opendial PRIVATE opendialcore Qt6::Core Qt6::DBus Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::QuickControls2 Qt6::Svg
                      PkgConfig::XKBCOMMON)
install(TARGETS opendial RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
