cmake_minimum_required(VERSION 3.10)
project(httpproxy C)

set(CMAKE_C_STANDARD 11)

add_executable(httpproxy main.c http_message.c http_message.h proxy.h net.c net.h list.c list.h)