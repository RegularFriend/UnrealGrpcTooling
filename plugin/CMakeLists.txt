# plugin/CMakeLists.txt
project(protoc-gen-unreal LANGUAGES CXX)
add_executable(protoc-gen-unreal protoc_gen_unreal.cpp)
target_link_libraries(protoc-gen-unreal
    PRIVATE 
    libprotoc 
    libprotobuf
)
target_include_directories(protoc-gen-unreal PRIVATE
    ${CMAKE_SOURCE_DIR}/grpc/include
    ${CMAKE_SOURCE_DIR}/grpc/third_party/protobuf/src
)
set_target_properties(protoc-gen-unreal PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/outputs/bin"
)
install(TARGETS protoc-gen-unreal DESTINATION bin)