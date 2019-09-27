cmake_minimum_required(VERSION 3.14)
project(mrsw_ptr)

set(CMAKE_CXX_STANDARD 14)

add_executable(mrsw_ptr main.cpp mrsw_ptr_trail.cpp)
target_link_libraries(mrsw_ptr pthread)