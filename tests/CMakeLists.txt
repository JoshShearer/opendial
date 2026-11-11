function(opendial_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE opendialcore Qt6::Test)
  target_compile_definitions(${name} PRIVATE
    OPENDIAL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/replay-fixtures"
    OPENDIAL_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

opendial_test(test_gesture unit/test_gesture.cpp)
opendial_test(test_rotation unit/test_rotation.cpp)
opendial_test(test_keychord unit/test_keychord.cpp)
opendial_test(test_actions unit/test_actions.cpp)
opendial_test(test_config unit/test_config.cpp)
opendial_test(test_profiles unit/test_profiles.cpp)
opendial_test(test_radial unit/test_radial.cpp)
opendial_test(test_decoder unit/test_decoder.cpp)

add_subdirectory(integration)
