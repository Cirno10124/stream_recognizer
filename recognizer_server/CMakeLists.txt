cmake_minimum_required(VERSION 3.14)
project(recognizer_server VERSION 1.0 LANGUAGES CXX C CUDA)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CUDA设置
set(CMAKE_CUDA_STANDARD 14)
set(CMAKE_CUDA_ARCHITECTURES 86) # 对应RTX 3060
set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} --extended-lambda --use_fast_math -lineinfo")

# 项目目录设置
set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd_party)
set(WHISPER_DIR ${THIRD_PARTY_DIR}/whisper.cpp)
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 指定whisper.cpp构建目录
set(WHISPER_BUILD_DIR ${WHISPER_DIR}/build)

# 设置包含目录
include_directories(
    ${INCLUDE_DIR}
    ${WHISPER_DIR}
    ${WHISPER_DIR}/include
    ${WHISPER_DIR}/ggml/include
    ${WHISPER_DIR}/ggml/src
)

# 直接指定找到的动态库
set(WHISPER_LIBRARY "/home/chess/recognizer_server/3rd_party/whisper.cpp/build/src/libwhisper.so")
set(GGML_BASE_LIBRARY "/home/chess/recognizer_server/3rd_party/whisper.cpp/build/ggml/src/libggml-base.so")
set(GGML_LIBRARY "/home/chess/recognizer_server/3rd_party/whisper.cpp/build/ggml/src/libggml.so")
set(GGML_CPU_LIBRARY "/home/chess/recognizer_server/3rd_party/whisper.cpp/build/ggml/src/libggml-cpu.so")
set(GGML_CUDA_LIBRARY "/home/chess/recognizer_server/3rd_party/whisper.cpp/build/ggml/src/ggml-cuda/libggml-cuda.so")

# 打印找到的库
message(STATUS "Found whisper library: ${WHISPER_LIBRARY}")
message(STATUS "Found ggml library: ${GGML_LIBRARY}")

# 定义编译标志
add_definitions(
    -DGGML_CUDA=1
    -DGGML_USE_CUDA=1
    -DGGML_USE_K_QUANTS
    -DGGML_USE_TINYBLAS
)

# 添加源文件
set(SOURCES
    src/main.cpp
    src/recognition_service.cpp
    src/file_handler.cpp
    src/cuda_memory_manager.cpp
    src/fattn_dummy.cu
)

# 设置可执行文件
add_executable(recognizer_server ${SOURCES})

# 查找CUDA库
find_package(CUDAToolkit REQUIRED)

# 链接库
target_link_libraries(recognizer_server PRIVATE
    ${WHISPER_LIBRARY}
    ${GGML_LIBRARY}
    ${GGML_BASE_LIBRARY}
    ${GGML_CPU_LIBRARY}
    ${GGML_CUDA_LIBRARY}
    pthread
    m
    CUDA::cudart
    CUDA::cublas
    CUDA::cublasLt
    dl
)

# 设置库搜索路径
target_link_directories(recognizer_server PRIVATE
    /usr/local/cuda/lib64
    /usr/lib/x86_64-linux-gnu
)

# 复制配置文件
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.json 
    ${CMAKE_CURRENT_BINARY_DIR}/config.json 
    COPYONLY
)

# 创建必要的目录
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/models)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/storage)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/logs)

# 打印配置信息
message(STATUS "Project configured: ${PROJECT_NAME}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "CUDA architecture: ${CMAKE_CUDA_ARCHITECTURES}")

# 添加运行时路径设置
set_target_properties(recognizer_server PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "/home/chess/recognizer_server/3rd_party/whisper.cpp/build/src:/home/chess/recognizer_server/3rd_party/whisper.cpp/build/ggml/src:/home/chess/recognizer_server/3rd_party/whisper.cpp/build/ggml/src/ggml-cuda"
)
