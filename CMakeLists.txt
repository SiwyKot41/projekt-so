cmake_minimum_required(VERSION 3.17)
project(main C)

set(CMAKE_C_STANDARD 11)


add_executable(main main.c)

target_link_libraries(main PUBLIC pthread)