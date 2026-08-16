cmake_minimum_required(VERSION 3.16)
project(indi_inequator_tracker VERSION 1.0 LANGUAGES CXX)

find_package(INDI REQUIRED)

add_executable(indi_inequator_tracker
    inequator.cpp
    inequator_protocol.cpp
)

target_compile_features(indi_inequator_tracker PRIVATE cxx_std_17)
target_include_directories(indi_inequator_tracker PRIVATE ${INDI_INCLUDE_DIR})
target_link_libraries(indi_inequator_tracker PRIVATE ${INDI_LIBRARIES})

install(TARGETS indi_inequator_tracker RUNTIME DESTINATION bin)

set(INEQUATOR_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(INEQUATOR_VERSION_MINOR ${PROJECT_VERSION_MINOR})
configure_file(inequator.xml.cmake inequator.xml @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/inequator.xml DESTINATION ${INDI_DATA_DIR})

include(CTest)
if(BUILD_TESTING)
    add_executable(inequator_protocol_tests
        tests/inequator_protocol_tests.cpp
        inequator_protocol.cpp
    )
    target_compile_features(inequator_protocol_tests PRIVATE cxx_std_17)
    target_include_directories(inequator_protocol_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME inequator_protocol_tests COMMAND inequator_protocol_tests)
endif()
