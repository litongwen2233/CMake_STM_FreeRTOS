## ============================================================================
## 文件: gcc-arm-none-eabi.cmake
## 说明: 这是"交叉编译工具链文件"。
##
##       什么是交叉编译？
##       你的电脑是 x86 架构（Windows/Linux），但我们要编译的程序是运行在 ARM 架构的
##       STM32 芯片上的。所以我们需要用一个专门的编译器（arm-none-eabi-gcc），
##       它能在你的电脑上运行，但生成的程序是给 ARM 芯片用的。
##
##       这个文件的作用就是告诉 CMake：
##         1. 用哪个编译器？（arm-none-eabi-gcc）
##         2. 目标芯片是什么架构？（ARM Cortex-M3）
##         3. 链接时需要什么参数？（链接脚本、内存占用打印等）
##
##       CMake 会在 project() 调用之前读取这个文件。
##       读取方式有两种（任选其一）：
##         方式一：在 CMakePresets.json 里指定 "toolchainFile"（本项目用的这种方式）
##         方式二：在命令行指定 cmake -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake ..
## ============================================================================

## ---------- 一、声明目标系统信息 ----------
## 因为我们在电脑（宿主机）上编译，但目标是 STM32（ARM 芯片），
## 所以必须告诉 CMake：这不是在编译电脑程序，是在编译"嵌入式"程序。
## "Generic" 表示通用嵌入式系统（没有操作系统，直接跑在硬件上）。
set(CMAKE_SYSTEM_NAME Generic)
## 目标处理器架构是 ARM。
set(CMAKE_SYSTEM_PROCESSOR arm)

## ---------- 二、指定编译器 ----------
## "arm-none-eabi-" 是编译器的前缀。
## 例如：gcc -> arm-none-eabi-gcc，objcopy -> arm-none-eabi-objcopy
## 重要：你的电脑必须把 arm-none-eabi-gcc 所在的目录加到系统 PATH 环境变量里！
##        否则 CMake 找不到编译器会报错。
set(TOOLCHAIN_PREFIX arm-none-eabi-)

set(CMAKE_C_COMPILER        ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER      ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER      ${CMAKE_C_COMPILER})   # 汇编器用 gcc 就行
set(CMAKE_OBJCOPY           ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE              ${TOOLCHAIN_PREFIX}size)

## 设置可执行文件的默认后缀为 .elf
## （默认是无后缀的，但嵌入式习惯用 .elf）
set(CMAKE_EXECUTABLE_SUFFIX_ASM   ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX   ".elf")

## ---------- 三、跳过编译器能力检测 ----------
## 交叉编译时，CMake 默认会尝试编译一个小程序来检测编译器是否正常工作。
## 但对于嵌入式交叉编译，这通常会失败（因为生成的程序没法在电脑上运行）。
## 设置为 STATIC_LIBRARY 可以跳过这个检测——只编译不链接，就不会报错了。
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

## ---------- 四、MCU 相关的编译和链接参数 ----------
## 这里定义了"给编译器和链接器的命令行参数"。

## --- 目标芯片参数 ---
## -mcpu=cortex-m3: 告诉编译器目标 CPU 是 ARM Cortex-M3（STM32F103 系列用的就是这个）
set(TARGET_FLAGS "-mcpu=cortex-m3")

## --- C 语言编译参数（使用 _INIT 变量，这是工具链文件的规范写法） ---
## _INIT 后缀的变量专门用于工具链文件，CMake 会把它们作为"初始值"，
## 然后用户可以在 CMakeLists.txt 里追加更多参数。
## 两者会自动合并，不会互相覆盖。
##   -Wall -Wextra -Wpedantic: 开启各种警告，帮你发现潜在 Bug
##   -fdata-sections -ffunction-sections: 让每个函数/变量独立成段，配合 --gc-sections 删除未使用的代码，缩小固件体积
set(CMAKE_C_FLAGS_INIT
    "${TARGET_FLAGS} -Wall -Wextra -Wpedantic -fdata-sections -ffunction-sections"
)

## --- 汇编语言编译参数 ---
## -x assembler-with-cpp: 让 gcc 把 .s 文件当作"带 C 预处理的汇编"来编译
##   （这样汇编文件里也能用 #include、#define 等 C 预处理指令）
## -MMD -MP: 生成依赖文件，让 CMake 知道头文件变化时需要重新编译
set(CMAKE_ASM_FLAGS_INIT
    "${TARGET_FLAGS} -x assembler-with-cpp -MMD -MP"
)

## --- 链接器参数 ---
## 链接是编译的最后一步：把所有 .o 文件合并成一个 .elf 文件。
##   -T xxx.ld: 指定链接脚本（定义了芯片的内存布局：Flash 和 RAM 的地址与大小）
##   --specs=nano.specs: 使用 Newlib-nano 库（比标准库小很多，适合嵌入式）
##   -Wl,-Map=xxx.map: 生成 .map 文件（内存映射图，调试时很有用）
##   -Wl,--gc-sections: 删除未被调用的函数和变量（配合上面的 -fdata-sections）
##   -Wl,--start-group -lc -lm -Wl,--end-group: 链接 C 标准库（libc）和数学库（libm）
##   -Wl,--print-memory-usage: 编译完成后打印 Flash/RAM 的占用情况
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${TARGET_FLAGS} \
    -T \"${CMAKE_SOURCE_DIR}/STM32F103CBTx_FLASH.ld\" \
    --specs=nano.specs \
    -Wl,-Map=${CMAKE_PROJECT_NAME}.map \
    -Wl,--gc-sections \
    -Wl,--start-group -lc -lm -Wl,--end-group \
    -Wl,--print-memory-usage"
)
