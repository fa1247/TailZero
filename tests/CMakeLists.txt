# Plain C++ backend/service tests; no display or real Tailscale required.
file(GLOB TAILZERO_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_executable(tailzero-tests ${TAILZERO_TEST_SOURCES}
    ../src/app/app_service.cpp ../src/app/backend_factory.cpp)
target_include_directories(tailzero-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
target_compile_options(tailzero-tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(tailzero-tests PRIVATE tailzero_backend pthread)
add_test(NAME tailzero-tests COMMAND tailzero-tests)
set_tests_properties(tailzero-tests PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
add_test(NAME tailzero-ui-exit-nodes COMMAND tailzero-ui-preview)
set_tests_properties(tailzero-ui-exit-nodes PROPERTIES
    ENVIRONMENT "TAILZERO_DEMO_SMOKE=1"
    TIMEOUT 30)
