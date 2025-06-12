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
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# 指定whisper.cpp构建目录
set(WHISPER_BUILD_DIR ${WHISPER_DIR}/build)

# 设置包含目录
include_directories(
    ${INCLUDE_DIR}
    ${SRC_DIR}
    ${WHISPER_DIR}
    ${WHISPER_DIR}/include
    ${WHISPER_DIR}/ggml/include
    ${WHISPER_DIR}/ggml/src
)

# 动态查找库文件路径（支持不同的构建环境）
if(EXISTS "${WHISPER_BUILD_DIR}/src/libwhisper.so")
    set(WHISPER_LIBRARY "${WHISPER_BUILD_DIR}/src/libwhisper.so")
    set(GGML_BASE_LIBRARY "${WHISPER_BUILD_DIR}/ggml/src/libggml-base.so")
    set(GGML_LIBRARY "${WHISPER_BUILD_DIR}/ggml/src/libggml.so")
    set(GGML_CPU_LIBRARY "${WHISPER_BUILD_DIR}/ggml/src/libggml-cpu.so")
    set(GGML_CUDA_LIBRARY "${WHISPER_BUILD_DIR}/ggml/src/ggml-cuda/libggml-cuda.so")
else()
    # 备用硬编码路径
    set(WHISPER_LIBRARY "/home/chess/recognizer_server/3rd_party/whisper.cpp/build/src/libwhisper.so")
    set(GGML_BASE_LIBRARY "/home/chess/recognizer_server/3rd_party/whisper.cpp/build/ggml/src/libggml-base.so")
    set(GGML_LIBRARY "/home/chess/recognizer_server/3rd_party/whisper.cpp/build/ggml/src/libggml.so")
    set(GGML_CPU_LIBRARY "/home/chess/recognizer_server/3rd_party/whisper.cpp/build/ggml/src/libggml-cpu.so")
    set(GGML_CUDA_LIBRARY "/home/chess/recognizer_server/3rd_party/whisper.cpp/build/ggml/src/ggml-cuda/libggml-cuda.so")
endif()

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

# 收集所有源文件（包括新的text_corrector）
set(SOURCES
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/recognition_service.cpp
    ${SRC_DIR}/file_handler.cpp
    ${SRC_DIR}/cuda_memory_manager.cpp
    ${SRC_DIR}/text_corrector.cpp
    ${SRC_DIR}/fattn_dummy.cu
)

# 收集所有头文件
set(HEADERS
    ${INCLUDE_DIR}/recognition_service.h
    ${INCLUDE_DIR}/file_handler.h
    ${INCLUDE_DIR}/cuda_memory_manager.h
    ${INCLUDE_DIR}/text_corrector.h
    ${SRC_DIR}/cuda_override.h
)

# 设置可执行文件
add_executable(recognizer_server ${SOURCES} ${HEADERS})

# 查找必要的依赖库
find_package(CUDAToolkit REQUIRED)

# 查找cpp-httplib库
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(HTTPLIB cpp-httplib)
endif()

# 如果没有找到pkg-config或cpp-httplib，使用系统安装的库
if(NOT HTTPLIB_FOUND)
    find_library(HTTPLIB_LIBRARIES cpp-httplib)
    if(NOT HTTPLIB_LIBRARIES)
        message(WARNING "cpp-httplib not found via pkg-config, using system default")
        set(HTTPLIB_LIBRARIES "")
    endif()
endif()

# 查找必要的系统库
find_package(Threads REQUIRED)

# 链接库
target_link_libraries(recognizer_server PRIVATE
    ${WHISPER_LIBRARY}
    ${GGML_LIBRARY}
    ${GGML_BASE_LIBRARY}
    ${GGML_CPU_LIBRARY}
    ${GGML_CUDA_LIBRARY}
    ${HTTPLIB_LIBRARIES}
    Threads::Threads
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
    ${WHISPER_BUILD_DIR}/src
    ${WHISPER_BUILD_DIR}/ggml/src
    ${WHISPER_BUILD_DIR}/ggml/src/ggml-cuda
)

# 添加包含目录
if(HTTPLIB_FOUND)
    target_include_directories(recognizer_server PRIVATE ${HTTPLIB_INCLUDE_DIRS})
    target_link_directories(recognizer_server PRIVATE ${HTTPLIB_LIBRARY_DIRS})
endif()

# 复制配置文件和资源
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.json 
    ${CMAKE_CURRENT_BINARY_DIR}/config.json 
    COPYONLY
)

# 创建必要的目录
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/models)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/storage)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/logs)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/temp)

# 设置编译器特定的选项
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(recognizer_server PRIVATE -Wall -Wextra -O2)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(recognizer_server PRIVATE -Wall -Wextra -O2)
endif()

# 打印配置信息
message(STATUS "Project configured: ${PROJECT_NAME}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "CUDA architecture: ${CMAKE_CUDA_ARCHITECTURES}")
message(STATUS "Source directory: ${SRC_DIR}")
message(STATUS "Include directory: ${INCLUDE_DIR}")
message(STATUS "Sources found: ${SOURCES}")

# 添加运行时路径设置
set_target_properties(recognizer_server PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../3rd_party/whisper.cpp/build/src:$ORIGIN/../3rd_party/whisper.cpp/build/ggml/src:$ORIGIN/../3rd_party/whisper.cpp/build/ggml/src/ggml-cuda"
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

# 添加自定义构建目标
add_custom_target(clean-build
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Cleaning build directory"
)

# 添加安装规则
install(TARGETS recognizer_server
    RUNTIME DESTINATION bin
)

install(FILES config.json
    DESTINATION bin
)
