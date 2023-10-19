cmake_minimum_required(VERSION 3.5)

project(asiocommunicator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(asiocommunicator_server main.cpp)
target_compile_definitions(asiocommunicator_server PRIVATE SERVER)

add_executable(asiocommunicator_client main.cpp)


include(GNUInstallDirs)

install(TARGETS asiocommunicator_server asiocommunicator_client
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)
