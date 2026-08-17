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
set(gRPC_CARES_PROVIDER "module" CACHE STRING "" FORCE)
set(gRPC_ABSL_PROVIDER "module" CACHE STRING "" FORCE)
set(gRPC_RE2_PROVIDER "module" CACHE STRING "" FORCE)
set(ZLIB_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)

# ------------------------------------------------------------------------------------------------
# SSL and zlib provider
#
# Unreal ships with SSL and zlib. Pass -DUNREAL_ENGINE_DIR<path to the engine  folder> to build against the Unreal Engine's
# Versions of SSL and Zlib. If nothing is passed through, it'll just build with the defaults. If you do not do this, packaged
# Builds will throw a linker error.
# ------------------------------------------------------------------------------------------------
set(UNREAL_ENGINE_DIR "" CACHE PATH
        "Unreal Engine root (the folder containing Engine/). When set, gRPC is built against the engine's OpenSSL and zlib instead of its own vendored BoringSSL and zlib, which is required for a monolithic packaged game build to link.")

if (UNREAL_ENGINE_DIR)
    if (NOT WIN32)
        message(FATAL_ERROR "UNREAL_ENGINE_DIR is only implemented for Win64 builds.")
    endif ()
    if (NOT EXISTS "${UNREAL_ENGINE_DIR}/Engine/Build/Build.version")
        message(FATAL_ERROR "UNREAL_ENGINE_DIR='${UNREAL_ENGINE_DIR}' does not look like an Unreal Engine root (no Engine/Build/Build.version).")
    endif ()

    set(_ue_third_party "${UNREAL_ENGINE_DIR}/Engine/Source/ThirdParty")

    function(_ue_read_version OUT_VAR BUILD_CS PATTERN WHAT)
        if (NOT EXISTS "${BUILD_CS}")
            message(FATAL_ERROR "Missing ${BUILD_CS}")
        endif ()
        file(READ "${BUILD_CS}" _contents)
        string(REGEX MATCH "${PATTERN}" _matched "${_contents}")
        if (NOT _matched)
            message(FATAL_ERROR "Could not parse the ${WHAT} version out of ${BUILD_CS}")
        endif ()
        set(${OUT_VAR} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    endfunction()

    _ue_read_version(_ue_openssl_version "${_ue_third_party}/OpenSSL/OpenSSL.Build.cs"
            "OpenSSLVersion[ \t]*=[ \t]*\"([^\"]+)\"" "OpenSSL")
    _ue_read_version(_ue_zlib_version "${_ue_third_party}/zlib/zlib.Build.cs"
            "Version[ \t]*=[ \t]*\"([^\"]+)\"" "zlib")

    # OpenSSL.Build.cs maps every VS2022+ toolchain back to the "VS2015" folder, and picks the
    # Release libraries unless bDebugBuildsActuallyUseDebugCRT is set -- it is not, by default, so
    # one Release build covers both the Development and Shipping game targets.
    set(_ue_openssl_include "${_ue_third_party}/OpenSSL/${_ue_openssl_version}/include/Win64/VS2015")
    set(_ue_openssl_lib_dir "${_ue_third_party}/OpenSSL/${_ue_openssl_version}/lib/Win64/VS2015/Release")
    set(_ue_zlib_include "${_ue_third_party}/zlib/${_ue_zlib_version}/include")
    set(_ue_zlib_library "${_ue_third_party}/zlib/${_ue_zlib_version}/lib/Win64/Release/zlibstatic.lib")

    foreach (_required
            "${_ue_openssl_include}/openssl/ssl.h"
            "${_ue_openssl_lib_dir}/libssl.lib"
            "${_ue_openssl_lib_dir}/libcrypto.lib"
            "${_ue_zlib_include}/zlib.h"
            "${_ue_zlib_library}")
        if (NOT EXISTS "${_required}")
            message(FATAL_ERROR "Engine dependency not found: ${_required}")
        endif ()
    endforeach ()

    # OPENSSL_ROOT_DIR cannot point at the engine directly: its include/Win64/VS2015 +
    # lib/Win64/VS2015/Release layout is not one FindOpenSSL searches, and on MSVC FindOpenSSL
    # *discards* an explicitly-passed OPENSSL_CRYPTO_LIBRARY and overwrites it from its own
    # LIB_EAY_* search. So mirror the two .lib files and the headers into the conventional
    # <root>/lib layout and point it there instead. file(COPY) is copy-if-different, so a
    # reconfigure does not touch timestamps and force a rebuild.
    set(_ue_openssl_shim "${CMAKE_BINARY_DIR}/unreal-openssl")
    file(COPY "${_ue_openssl_include}/openssl" DESTINATION "${_ue_openssl_shim}/include")
    file(COPY "${_ue_openssl_lib_dir}/libssl.lib" "${_ue_openssl_lib_dir}/libcrypto.lib"
            DESTINATION "${_ue_openssl_shim}/lib")

    # Swapping the libraries at link time is not enough. Around twenty OpenSSL entry points
    # (EVP_MD_CTX_create, OPENSSL_free, BIO_pending, SSL_set_tlsext_host_name, the SSL_CTX_*
    # setters) are preprocessor macros in OpenSSL 1.1.1t's headers but real exported functions in
    # BoringSSL. Objects compiled against BoringSSL call them as functions and leave unresolvable
    # externals, so gRPC has to be *compiled* against the engine's headers.
    set(gRPC_SSL_PROVIDER "package" CACHE STRING "" FORCE)
    set(OPENSSL_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
    set(OPENSSL_ROOT_DIR "${_ue_openssl_shim}" CACHE PATH "" FORCE)
    set(OPENSSL_INCLUDE_DIR "${_ue_openssl_shim}/include" CACHE PATH "" FORCE)

    set(gRPC_ZLIB_PROVIDER "package" CACHE STRING "" FORCE)
    set(ZLIB_INCLUDE_DIR "${_ue_zlib_include}" CACHE PATH "" FORCE)
    set(ZLIB_LIBRARY "${_ue_zlib_library}" CACHE FILEPATH "" FORCE)
    set(ZLIB_LIBRARY_RELEASE "${_ue_zlib_library}" CACHE FILEPATH "" FORCE)

    message(STATUS "Unreal Engine: ${UNREAL_ENGINE_DIR}")
    message(STATUS "  OpenSSL ${_ue_openssl_version} -> ${_ue_openssl_lib_dir}")
    message(STATUS "  zlib    ${_ue_zlib_version} -> ${_ue_zlib_library}")
else ()
    set(gRPC_SSL_PROVIDER "module" CACHE STRING "" FORCE)
    set(gRPC_ZLIB_PROVIDER "module" CACHE STRING "" FORCE)
endif ()

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

if (UNREAL_ENGINE_DIR)
    foreach (_entry LIB_EAY_RELEASE SSL_EAY_RELEASE OPENSSL_CRYPTO_LIBRARY OPENSSL_SSL_LIBRARY)
        if (DEFINED ${_entry})
            file(TO_CMAKE_PATH "${${_entry}}" _resolved)
            file(TO_CMAKE_PATH "${_ue_openssl_shim}/lib" _expected)
            string(FIND "${_resolved}" "${_expected}/" _found_at)
            if (_found_at EQUAL -1)
                message(FATAL_ERROR "CMake resolved ${_entry} to '${_resolved}' instead of the engine OpenSSL under '${_expected}'. A system or vcpkg OpenSSL has taken priority.")
            endif ()
        endif ()
    endforeach ()

    # The engine's zlib module owns these headers;
    install(CODE "
        foreach (_stray zlib.h zconf.h)
            if (EXISTS \"\${CMAKE_INSTALL_PREFIX}/include/\${_stray}\")
                message(STATUS \"Removing stray \${_stray} (the engine's zlib owns it)\")
                file(REMOVE \"\${CMAKE_INSTALL_PREFIX}/include/\${_stray}\")
            endif ()
        endforeach ()
        if (EXISTS \"\${CMAKE_INSTALL_PREFIX}/include/openssl\")
            message(STATUS \"Removing stray openssl/ headers (the engine's OpenSSL owns them)\")
            file(REMOVE_RECURSE \"\${CMAKE_INSTALL_PREFIX}/include/openssl\")
        endif ()

        # The guard rail. These are the exact symbol sets that collide with the engine in a
        # monolithic link, so their presence means the provider flags above did not take effect.
        # This single check is what stops the whole thing silently regressing.
        set(_forbidden crypto.lib ssl.lib decrepit.lib zlib.lib zlibstatic.lib zlibstaticd.lib)
        set(_leaked \"\")
        foreach (_lib \${_forbidden})
            if (EXISTS \"\${CMAKE_INSTALL_PREFIX}/lib/\${_lib}\")
                list(APPEND _leaked \${_lib})
            endif ()
        endforeach ()
        if (_leaked)
            message(FATAL_ERROR \"These libraries duplicate symbols Unreal already links and must not ship: \${_leaked}. The SSL/zlib provider flags did not take effect.\")
        endif ()
        message(STATUS \"Verified: no BoringSSL or zlib libraries in the install tree.\")
    ")
endif ()