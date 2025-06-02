cmake_minimum_required(VERSION 3.10)
project(fvad VERSION 1.0.0 LANGUAGES C)

# 设置C标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 头文件路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/signal_processing
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vad
)

# 源文件
file(GLOB MAIN_SOURCES "src/*.c")
file(GLOB SP_SOURCES "src/signal_processing/*.c")
file(GLOB VAD_SOURCES "src/vad/*.c")

# 主库目标
add_library(fvad STATIC
    ${MAIN_SOURCES}
    ${SP_SOURCES}
    ${VAD_SOURCES}
)

# 安装规则
install(TARGETS fvad
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(FILES include/fvad.h DESTINATION include)

# 示例程序（可选）
option(BUILD_EXAMPLES "Build example programs" OFF)
if(BUILD_EXAMPLES)
    find_package(SndFile)
    if(SndFile_FOUND)
        add_executable(fvadwav examples/fvadwav.c)
        target_link_libraries(fvadwav PRIVATE fvad SndFile::sndfile)
        install(TARGETS fvadwav RUNTIME DESTINATION bin)
    else()
        message(WARNING "libsndfile not found, examples will not be built")
    endif()
endif()