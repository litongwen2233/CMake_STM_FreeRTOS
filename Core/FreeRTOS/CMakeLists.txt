## ============================================================================
## 文件: Core/FreeRTOS/CMakeLists.txt
## 说明: 这是 FreeRTOS 内核的"模块构建文件"。
##
##   设计目标：让 FreeRTOS 成为一个独立、可移植的模块。
##   移植到新项目时，只要把整个 Core/FreeRTOS 目录复制过去，
##   然后在顶层 CMakeLists.txt 里加两行即可：
##
##     add_subdirectory(Core/FreeRTOS)                # 引入本模块
##     target_link_libraries(你的目标 PRIVATE freertos_kernel)
##
##   本文件会生成一个名为 "freertos_kernel" 的静态库（STATIC），
##   并把 FreeRTOS 的头文件路径、配置（FreeRTOSConfig.h）一起通过
##   target_link_libraries 自动传递给链接它的目标。
## ============================================================================

cmake_minimum_required(VERSION 3.22)

## ---------- 1. 配置（config）接口目标 ----------
## FreeRTOS 在编译时需要能找到 FreeRTOSConfig.h。
## 本项目把 FreeRTOSConfig.h 放在 Core/FreeRTOS 目录下，因此把该目录
## 加入头文件搜索路径。如果你的项目把它放在别处，改这一行即可。
add_library(freertos_config INTERFACE)
target_include_directories(freertos_config INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
)

## ---------- 2. 创建内核静态库 ----------
add_library(freertos_kernel STATIC)

## --- 2.1 内核核心源码 ---
## 这些是 FreeRTOS 内核必编译的源文件（位于 src/ 目录）。
target_sources(freertos_kernel PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src/croutine.c
    ${CMAKE_CURRENT_LIST_DIR}/src/event_groups.c
    ${CMAKE_CURRENT_LIST_DIR}/src/list.c
    ${CMAKE_CURRENT_LIST_DIR}/src/queue.c
    ${CMAKE_CURRENT_LIST_DIR}/src/stream_buffer.c
    ${CMAKE_CURRENT_LIST_DIR}/src/tasks.c
    ${CMAKE_CURRENT_LIST_DIR}/src/timers.c
)

## --- 2.2 移植层（port）源码 ---
## 重要：必须与你的编译器、芯片内核对应！
##   本项目使用 arm-none-eabi-gcc + STM32F103(Cortex-M3)，
##   因此选择 port/GCC/ARM_CM3。
##   注意：RVDS 目录是给 ARM 官方编译器（armcc/armclang）用的，GCC 无法编译。
##   如果换芯片（如 M4/M7/M0），把下面这行指到对应的 port 目录即可。
target_sources(freertos_kernel PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/port/GCC/ARM_CM3/port.c
)

## --- 2.3 内存管理（heap）实现 ---
## FreeRTOS 提供 5 种堆实现，常用的是 heap_4.c（支持合并相邻空闲块）。
## 如需更换，把文件名改成 heap_1/2/3/5.c 即可。
target_sources(freertos_kernel PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/port/MemMang/heap_4.c
)

## ---------- 3. 头文件路径 ----------
## PUBLIC 表示：链接 freertos_kernel 的目标也能自动获得这些头文件路径，
## 这样你的应用代码里写 #include "FreeRTOS.h" / "task.h" 就能直接找到。
target_include_directories(freertos_kernel PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/port/GCC/ARM_CM3
)

## ---------- 4. 关联配置目标 ----------
## 这样 freertos_kernel 以及链接它的目标都能找到 FreeRTOSConfig.h。
target_link_libraries(freertos_kernel PUBLIC freertos_config)

## ---------- 5. 编译宏（可选）----------
## 一般不需要额外宏。如需关闭某些功能可在此添加，例如：
## target_compile_definitions(freertos_kernel PRIVATE
##     $<$<CONFIG:Debug>:DEBUG>
## )
