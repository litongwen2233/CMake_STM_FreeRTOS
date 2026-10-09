## ============================================================================
## 文件: cmake/stm32cubemx/CMakeLists.txt
## 说明: 这个文件由 STM32CubeMX 工具自动生成（也可能被你手动修改）。
##       它定义了一个名为 "stm32cubemx" 的"库目标"，集中管理 STM32 HAL 库的所有
##       源文件、头文件路径和编译宏定义。
##
##       什么是"INTERFACE 库"？
##       INTERFACE 库不会被编译成 .a 或 .lib 文件，它的作用是：
##       把"源文件"、"头文件路径"、"编译宏"等信息打包在一起，
##       然后通过 target_link_libraries() 传递给主项目。
##       这样主项目的 CMakeLists.txt 就不用写一大堆 HAL 库的路径了，
##       只要链接这个 INTERFACE 库就能获得所有需要的信息。
## ============================================================================

cmake_minimum_required(VERSION 3.22)

project(stm32cubemx)

## 创建一个 INTERFACE 类型的库
## INTERFACE 表示：这个库"只提供头文件和编译信息"，本身不编译任何代码。
## 所有通过 target_xxx(... INTERFACE ...) 设置的属性，
## 只有"链接了这个库的目标"才能使用。
add_library(stm32cubemx INTERFACE)

## ---------- 编译宏定义 ----------
## USE_HAL_DRIVER: 告诉 STM32 HAL 库 "我要使用 HAL 驱动"（不定义的话很多 HAL 功能会编译不过）
## STM32F103xB: 指定芯片型号（STM32F103 中等容量系列），HAL 库内部会根据这个宏选择对应的芯片头文件
## DEBUG: 仅在 Debug 编译模式下定义，让调试信息输出生效
target_compile_definitions(stm32cubemx INTERFACE
    USE_HAL_DRIVER
    STM32F103xB
    $<$<CONFIG:Debug>:DEBUG>
)

## ---------- 头文件搜索路径 ----------
## 当你 #include "stm32f1xx_hal.h" 之类的头文件时，编译器需要知道去哪里找。
## 这里列出了所有 STM32 HAL 库和 CMSIS 库的头文件目录。
##   Core/Inc:                              你自己的头文件（main.h, stm32f1xx_hal_conf.h 等）
##   Drivers/STM32F1xx_HAL_Driver/Inc:      HAL 驱动库的头文件
##   Drivers/STM32F1xx_HAL_Driver/Inc/Legacy: 一些旧版兼容头文件
##   Drivers/CMSIS/Device/ST/STM32F1xx/Include: 芯片寄存器定义头文件
##   Drivers/CMSIS/Include:                  ARM CMSIS 核心头文件（Cortex-M 内核相关）
target_include_directories(stm32cubemx INTERFACE
    ${CMAKE_SOURCE_DIR}/Core/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Include
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Include
)

## ---------- 源文件列表 ----------
## 这里列出了所有需要编译的源文件，包括：
##   - Core/Src/*.c:         STM32CubeMX 生成的初始化代码和你的代码
##   - Drivers/.../*.c:      ST 官方提供的 HAL 驱动库源文件
##   - startup_stm32f103xb.s: 芯片启动文件（汇编语言，包含中断向量表和启动代码）
## 以后如果 CubeMX 重新生成代码，或者你添加了新的 HAL 模块，
## 需要在这里更新对应的 .c 文件。
target_sources(stm32cubemx INTERFACE
    ## --- 用户代码（Core）---
    ${CMAKE_SOURCE_DIR}/Core/Src/main.c
    ${CMAKE_SOURCE_DIR}/Core/Src/stm32f1xx_it.c
    ${CMAKE_SOURCE_DIR}/Core/Src/stm32f1xx_hal_msp.c
    ${CMAKE_SOURCE_DIR}/Core/Src/system_stm32f1xx.c
    ${CMAKE_SOURCE_DIR}/Core/Src/sysmem.c
    ${CMAKE_SOURCE_DIR}/Core/Src/syscalls.c

    ## --- HAL 驱动库 ---
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dma.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cortex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pwr.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_exti.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim_ex.c

    ## --- 启动文件（汇编）---
    ${CMAKE_SOURCE_DIR}/startup_stm32f103xb.s
)

## ---------- C 标准版本检查 ----------
## STM32CubeMX 生成的代码需要至少 C11 标准才能编译通过。
## 如果你在主 CMakeLists.txt 里把 CMAKE_C_STANDARD 设得太低，这里会报错提醒你。
if(CMAKE_C_STANDARD LESS 11)
    message(FATAL_ERROR "STM32CubeMX 生成的代码需要 C11 或更高版本的 C 标准！")
endif()
