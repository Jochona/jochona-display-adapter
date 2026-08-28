# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jochona project contributors
#
# Portable unit tests for the Jochona Display Adapter protocol/state-machine
# code (src/protocol). Deliberately has NO dependency on the WDK/IddCx SDK
# so it builds and runs on any platform with a C++17 compiler — the driver
# project (driver/JochonaDisplayAdapter) links the same src/protocol
# sources unmodified inside the Windows/WDF build.
cmake_minimum_required(VERSION 3.20)
project(JochonaProtocolTests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(jochona_protocol STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/protocol/SlotStateMachine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/protocol/ProtocolDispatcher.cpp
)
target_include_directories(jochona_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/protocol
)

add_executable(jochona_protocol_tests
    JochonaProtocolTests/main.cpp
    JochonaProtocolTests/SlotStateMachineTests.cpp
    JochonaProtocolTests/ProtocolDispatcherTests.cpp
    JochonaProtocolTests/MalformedBufferTests.cpp
)
target_include_directories(jochona_protocol_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(jochona_protocol_tests PRIVATE jochona_protocol)

enable_testing()
add_test(NAME JochonaProtocolTests COMMAND jochona_protocol_tests)
