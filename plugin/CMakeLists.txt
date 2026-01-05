# plugin/CMakeLists.txt
project(protoc-gen-unreal LANGUAGES CXX)

add_executable(protoc-gen-unreal protoc_gen_unreal.cpp)

# Link against the libraries built by the grpc subdirectory
target_link_libraries(protoc-gen-unreal 
    PRIVATE 
    libprotoc 
    libprotobuf
)

# Ensure the include paths for gRPC/Protobuf are found
target_include_directories(protoc-gen-unreal PRIVATE 
    ${CMAKE_SOURCE_DIR}/grpc/include
    ${CMAKE_SOURCE_DIR}/grpc/third_party/protobuf/src
)

# Automatically move the exe to your outputs/bin folder on build
set_target_properties(protoc-gen-unreal PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/outputs/bin"
)

# Add to the 'install' target we used earlier
install(TARGETS protoc-gen-unreal DESTINATION bin)