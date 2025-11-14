cmake_minimum_required(VERSION 3.8)
project(NetworkInfoServer C CXX)
include(../cmake/common.cmake)

get_filename_component(proto_file "../proto/network.proto" ABSOLUTE)
get_filename_component(proto_path "${proto_file}" PATH)

set(proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/network.pb.cc")
set(proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/network.pb.h")
set(grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/network.grpc.pb.cc")
set(grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/network.grpc.pb.h")

add_custom_command(
  OUTPUT "${proto_srcs}" "${proto_hdrs}" "${grpc_srcs}" "${grpc_hdrs}"
  COMMAND ${_PROTOBUF_PROTOC}
  ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
       --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
       -I "${proto_path}"
       --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
       "${proto_file}"
  DEPENDS "${proto_file}"
)

include_directories("${CMAKE_CURRENT_BINARY_DIR}")

add_executable(server
  server.cpp
  ${proto_srcs}
  ${grpc_srcs}
)

target_link_libraries(server
  ${_REFLECTION}
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF}
)