cmake_minimum_required(VERSION 3.27)
project(test03)

set(CMAKE_CXX_STANDARD 17)

add_executable(test03 main.cpp
        node.cpp
        node.h
        number.cpp
        number.h)
