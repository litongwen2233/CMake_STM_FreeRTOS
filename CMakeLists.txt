## ============================================================================
## 文件: CMakeLists.txt
## 说明: 这是整个项目的"总指挥"文件。CMake 会读取这个文件来知道如何编译你的代码。
##       简单来说，它定义了：用什么编译器、编译哪些文件、头文件在哪里、链接什么库。
##       如果你完全没用过 CMake，可以把这个文件想象成一个"编译脚本"。
## ============================================================================

## ---------- 第一步：声明项目的基本信息 ----------
## cmake_minimum_required 告诉 CMake：我这个文件需要至少哪个版本的 CMake 才能正常工作。
## 如果你电脑上的 CMake 版本比这个低，编译时会直接报错。
cmake_minimum_required(VERSION 3.22)

## project() 是整个项目的核心声明，做了三件事：
##   1. 给项目起名字（这里叫 CMake_STM_demo，最终生成的固件文件也会叫这个名字）
##   2. 声明项目使用的编程语言（C 语言 + 汇编语言）
##      - C: 你的 .c 源文件
##      - ASM: STM32 的启动文件 startup_stm32f103xb.s（汇编语言写的启动代码）
##   3. 这个命令之后，CMake 会自动设置好编译器等工具链
## 注意：project() 应该尽早调用，越早越好！
project(CMake_STM_demo LANGUAGES C ASM)

## ---------- 第二步：打印编译类型，方便调试 ----------
## message() 会在编译时打印一条信息到终端，帮助你确认当前的编译模式。
## ${CMAKE_BUILD_TYPE} 的值取决于你在 CMakePresets.json 里选择的预设，
## 常见的有：Debug（调试，不优化，带调试信息）、Release（发布，最大优化）。
message(STATUS "当前编译类型: ${CMAKE_BUILD_TYPE}")

## ---------- 第三步：设置 C 语言标准 ----------
## CMake 支持设置 C 语言的版本标准，就像你在 Keil 里可以选择用 C99 还是 C11 一样。
## 这里我们选择 C11 标准（STM32CubeMX 生成的代码需要至少 C11）。
set(CMAKE_C_STANDARD 11)
## ON 表示"必须严格执行这个标准"，如果编译器不支持 C11 就直接报错，而不是悄悄降级。
set(CMAKE_C_STANDARD_REQUIRED ON)
## ON 表示允许使用 C 语言的"扩展语法"（GNU 扩展），STM32 HAL 库会用到一些扩展。
set(CMAKE_C_EXTENSIONS ON)

## ---------- 第四步：设置编译器参数 ----------
## 下面这些参数告诉编译器"怎么编译你的代码"，相当于 Keil 里的编译选项设置。
## 注意：这些是"全局"设置，对项目中所有源文件都生效。

## --- 警告开关 ---
## -Wall: 开启大部分常见警告（Wall = Warning All）
## -Wextra: 开启额外的警告
## -Wpedantic: 严格遵循 C 标准，不符合标准的写法会警告
## 建议新手把这些都打开，它们能帮你发现很多隐藏的 Bug！
add_compile_options(-Wall -Wextra -Wpedantic)

## --- 代码段优化 ---
## -fdata-sections: 把每个全局变量放到独立的"段"（section）里
## -ffunction-sections: 把每个函数放到独立的"段"（section）里
## 为什么要这样做？配合链接时的 --gc-sections 参数，
## 编译器可以自动删除那些你代码里"写了但没调用"的函数和变量，
## 这样最终固件的体积会更小。这对 Flash 空间有限的 STM32 非常重要！
add_compile_options(-fdata-sections -ffunction-sections)

## --- 根据编译类型选择优化选项 ---
## 这里用 generator expression（生成器表达式）来根据编译类型自动选择参数：
##   Debug 模式: -O0（不优化，方便单步调试）+ -g3（生成最详细的调试信息）
##   Release 模式: -Os（优化代码体积，适合 Flash 空间有限的 MCU）+ -g0（不生成调试信息）
add_compile_options(
    "$<$<CONFIG:Debug>:-O0;-g3>"
    "$<$<CONFIG:Release>:-Os;-g0>"
)

## ---------- 第五步：引入 STM32CubeMX 自动生成的代码 ----------
## STM32CubeMX 会生成一套 CMake 子项目（在 cmake/stm32cubemx/ 目录下），
## 里面定义了 HAL 库的源文件、头文件路径、编译宏等。
## add_subdirectory() 的意思是："请去读那个目录下的 CMakeLists.txt，并把它的内容合并进来"。
## 这样做的好处是：如果以后你用 CubeMX 重新生成代码，这部分会自动更新。
add_subdirectory(cmake/stm32cubemx)

## ---------- 引入 FreeRTOS 模块 ----------
## 这个目录下的 CMakeLists.txt 定义了一个名为 "freertos_kernel" 的静态库，
## 并自动管理 FreeRTOS 的源文件、头文件路径和 FreeRTOSConfig.h。
## 移植到新项目时，把这一行和下面的链接一起复制即可。
add_subdirectory(Core/FreeRTOS)

## ---------- 第六步：创建可执行目标 ----------
## add_executable() 告诉 CMake："我要生成一个可执行文件"。
## 对于嵌入式开发，最终生成的是 .elf 文件（可以理解为"烧录前的固件"）。
## 注意：必须先 add_subdirectory() 再创建目标，因为源文件来自 INTERFACE 库。
add_executable(${CMAKE_PROJECT_NAME})

## 链接 STM32CubeMX INTERFACE 库。
## target_link_libraries() 的意思是："把 stm32cubemx 库的所有信息（源文件、头文件、宏定义）
## 传递给我们的目标"。这样目标就能自动获得 HAL 库的全部编译信息。
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE stm32cubemx freertos_kernel)

## ---------- 第七步：添加你自己的源文件 ----------
## 当你以后写了新的 .c 文件（比如 my_driver.c），在这里添加它的路径。
## target_sources() 的意思是："把这个文件加到编译列表里"。
## PRIVATE 表示：只有编译这个目标时才使用，不会传递给依赖它的其他目标。
## 示例：
## target_sources(${CMAKE_PROJECT_NAME} PRIVATE
##     Core/Src/my_driver.c    # 你自己的驱动文件
## )
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # 在这里添加你自己的源文件
)

## ---------- 第八步：添加头文件搜索路径 ----------
## 当你 #include "xxx.h" 时，编译器需要知道去哪里找这些头文件。
## target_include_directories() 就是告诉编译器："去这些目录里找 .h 文件"。
## 注意：FreeRTOS 的头文件路径已经由 Core/FreeRTOS/CMakeLists.txt 通过
##       target_link_libraries 自动带过来了，这里不需要再单独添加。
## 示例（你自己的头文件）：
## target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
##     ${CMAKE_SOURCE_DIR}/Core/Inc
## )
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # 在这里添加你的头文件目录
)

## ---------- 第九步：添加宏定义（编译开关）----------
## 有些代码会用 #ifdef 来做条件编译，比如：
##   #ifdef DEBUG
##       printf("调试信息");
##   #endif
## 下面这行就会在编译时定义 DEBUG 宏，让上面的代码生效。
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    USE_HAL_DRIVER
    STM32F103xB
)

## ---------- 第十步：生成 .bin 和 .hex 烧录文件 ----------
## .elf 文件是调试用的，但大多数烧录工具（如 STM32CubeProgrammer）需要 .bin 或 .hex 文件。
## 这里通过"后构建命令"（POST_BUILD）在链接完成后自动转换：
##   - objcopy -O binary: 把 .elf 转成 .bin（纯二进制文件，体积最小）
##   - objcopy -O ihex:    把 .elf 转成 .hex（Intel HEX 格式，包含地址信息）
## $<TARGET_FILE:xxx> 是 CMake 的生成器表达式，自动指向 .elf 文件的完整路径。
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
        $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        ${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex
        $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        ${CMAKE_PROJECT_NAME}.hex
    COMMENT ">>> 正在生成烧录文件: ${CMAKE_PROJECT_NAME}.bin 和 .hex"
)

## ---------- 第十一步：显示固件大小信息 ----------
## 编译完成后自动调用 arm-none-eabi-size 工具，显示固件占用了多少 Flash 和 RAM，
## 这样你就不用手动去查了，非常方便。
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    COMMENT ">>> 固件大小信息:"
)
