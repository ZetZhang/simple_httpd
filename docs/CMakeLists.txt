set(CMAKE_CXX_COMPILER "/usr/bin/g++")
set(CMAKE_CXX_STANDARD 11)
add_executable(color.cgi color.cpp)
target_link_libraries(color.cgi cgicc)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin/httpd/docs)

# file(COPY index.html DESTINATION ${PROJECT_BINARY_DIR}/bin/httpd/docs)
# message(STATUS "COPIED FILE")
