cmake_minimum_required(VERSION 3.20)
project(FriendGrpcSDK LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force Windows 10+ target to avoid _WIN32_WINNT redefinition warnings
if (WIN32)
    add_definitions(-D_WIN32_WINNT=0x0A00)
endif ()
# If building on windows, this makes it enormously faster.
if (MSVC)
    add_compile_options($<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:/MP>)
endif ()

# Release is required for unreal compatability as unreal assumes _ITERATOR_DEBUG_LEVEL=0
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif ()

# This is also required for unreal compatability
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "" FORCE)


set(gRPC_INSTALL ON CACHE BOOL "" FORCE)
set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(gRPC_PROTOBUF_PROVIDER "module" CACHE STRING "" FORCE)
set(gRPC_ZLIB_PROVIDER "module" CACHE STRING "" FORCE)
set(gRPC_CARES_PROVIDER "module" CACHE STRING "" FORCE)
set(gRPC_SSL_PROVIDER "module" CACHE STRING "" FORCE)
set(gRPC_ABSL_PROVIDER "module" CACHE STRING "" FORCE)
set(gRPC_RE2_PROVIDER "module" CACHE STRING "" FORCE)
set(ZLIB_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)

set(gRPC_BUILD_CSHARP_EXT OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_CSHARP_PLUGIN OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_NODE_PLUGIN OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_PHP_PLUGIN OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_PYTHON_PLUGIN OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_RUBY_PLUGIN OFF CACHE BOOL "" FORCE)

if (ANDROID)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/outputs/android" CACHE PATH "" FORCE)
elseif (WIN32)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/outputs/windows" CACHE PATH "" FORCE)
elseif (APPLE)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/outputs/macos" CACHE PATH "" FORCE)
elseif (UNIX)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/outputs/linux" CACHE PATH "" FORCE)
endif ()

message(STATUS "Install path: ${CMAKE_INSTALL_PREFIX}")

if (ANDROID)
    # Android builds require the gRPC C++ plugin and protoc from the host system. Let people pass it in, but if they dont, look for it based on platform.
    # todo: figure out how to use the right platform (current platform) to find the plugin. currently can only build from windows.
    if (NOT _gRPC_CPP_PLUGIN)
        if (WIN32)
            set(_gRPC_CPP_PLUGIN "${CMAKE_SOURCE_DIR}/outputs/windows/bin/grpc_cpp_plugin.exe" CACHE FILEPATH "Path to Windows gRPC plugin" FORCE)
        elseif (APPLE)
            set(_gRPC_CPP_PLUGIN "${CMAKE_SOURCE_DIR}/outputs/macos/bin/grpc_cpp_plugin" CACHE FILEPATH "Path to Mac gRPC plugin" FORCE)
        else ()
            set(_gRPC_CPP_PLUGIN "${CMAKE_SOURCE_DIR}/outputs/linux/bin/grpc_cpp_plugin" CACHE FILEPATH "Path to Linux gRPC plugin" FORCE)
        endif ()
    endif ()
    if (NOT _gRPC_PROTOBUF_PROTOC_EXECUTABLE)
        if (WIN32)
            set(_gRPC_PROTOBUF_PROTOC_EXECUTABLE "${CMAKE_SOURCE_DIR}/outputs/windows/bin/protoc.exe" CACHE FILEPATH "Path to Windows protoc" FORCE)
        elseif (APPLE)
            set(_gRPC_PROTOBUF_PROTOC_EXECUTABLE "${CMAKE_SOURCE_DIR}/outputs/macos/bin/protoc" CACHE FILEPATH "Path to Mac protoc" FORCE)
        else ()
            set(_gRPC_PROTOBUF_PROTOC_EXECUTABLE "${CMAKE_SOURCE_DIR}/outputs/linux/bin/protoc" CACHE FILEPATH "Path to Linux protoc" FORCE)
        endif ()
    endif ()

    message(STATUS "Using gRPC Plugin: ${_gRPC_CPP_PLUGIN}")
    message(STATUS "Using Protoc: ${_gRPC_PROTOBUF_PROTOC_EXECUTABLE}")
endif ()

add_subdirectory(grpc)
if (NOT ANDROID)
    add_subdirectory(plugin)
endif ()