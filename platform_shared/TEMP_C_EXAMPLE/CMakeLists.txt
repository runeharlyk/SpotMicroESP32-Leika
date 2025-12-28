cmake_minimum_required(VERSION 3.10)

# Force GCC compiler
set(CMAKE_C_COMPILER gcc)

# Project name
project(protobuftest C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/pb
    ${CMAKE_SOURCE_DIR}/platform_shared
)

# Nanopb library sources
set(NANOPB_SRCS
    pb/pb_common.c
    pb/pb_decode.c
    pb/pb_encode.c
)

# Generated protobuf sources
set(PROTO_SRCS
    platform_shared/example.pb.c
)

# Main application sources
set(APP_SRCS
    main.c
)

# Create executable
add_executable(${PROJECT_NAME}
    ${APP_SRCS}
    ${NANOPB_SRCS}
    ${PROTO_SRCS}
)

# Optional: Create a nanopb library (alternative approach)
# add_library(nanopb STATIC ${NANOPB_SRCS})
# target_link_libraries(${PROJECT_NAME} nanopb)
