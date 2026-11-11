# SPDX-License-Identifier: MIT
add_executable(opendialctl main.cpp)
target_link_libraries(opendialctl PRIVATE opendialcore Qt6::Core Qt6::DBus)
install(TARGETS opendialctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
