# SPDX-License-Identifier: MIT
add_executable(test_engine test_engine.cpp)
target_link_libraries(test_engine PRIVATE opendialsvc Qt6::Test)
target_compile_definitions(test_engine PRIVATE OPENDIAL_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
# Tests run on a private session bus (dbus-run-session) so the desktop's own
# MPRIS players, portals and services cannot influence them.
find_program(DBUS_RUN_SESSION dbus-run-session)
if(DBUS_RUN_SESSION)
  add_test(NAME test_engine COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:test_engine>)
else()
  add_test(NAME test_engine COMMAND test_engine)
endif()
set_tests_properties(test_engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

find_package(Python3 COMPONENTS Interpreter)
if(DBUS_RUN_SESSION AND Python3_FOUND)
  add_test(NAME test_dbus_cli
           COMMAND ${DBUS_RUN_SESSION} -- ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_dbus_cli.py
                   $<TARGET_FILE:opendiald> $<TARGET_FILE:opendialctl>)
  set_tests_properties(test_dbus_cli PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
  add_test(NAME test_commands
           COMMAND ${DBUS_RUN_SESSION} -- ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_commands.py
                   $<TARGET_FILE:opendiald> $<TARGET_FILE:opendialctl>)
  set_tests_properties(test_commands PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
  add_test(NAME test_plugins_http
           COMMAND ${DBUS_RUN_SESSION} -- ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_plugins_http.py
                   $<TARGET_FILE:opendiald> $<TARGET_FILE:opendialctl> ${PROJECT_SOURCE_DIR})
  set_tests_properties(test_plugins_http PROPERTIES TIMEOUT 120)
endif()
if(Python3_FOUND)
  add_test(NAME test_replay
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_replay.py
                   $<TARGET_FILE:opendiald> ${PROJECT_SOURCE_DIR}/tests/replay-fixtures)
  set_tests_properties(test_replay PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
endif()
