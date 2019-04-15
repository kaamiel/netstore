cmake_minimum_required(VERSION 3.10)
project(netstore C)

set(CMAKE_C_STANDARD 11)

add_executable(netstore-server serwer.c structs.h)
add_executable(netstore-client klient.c structs.h)