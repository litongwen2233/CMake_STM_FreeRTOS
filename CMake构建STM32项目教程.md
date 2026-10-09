# 使用 CMake 构建 STM32 项目完全教程

> 本教程面向零 CMake 基础的 STM32 开发者，手把手教你如何用 CMake + GCC 交叉编译工具链来构建 STM32 嵌入式项目，告别 Keil/IAR 的束缚。

---

## 目录

1. [为什么用 CMake 构建 STM32？](#1-为什么用-cmake-构建-stm32)
2. [环境准备：安装所需的工具](#2-环境准备安装所需的工具)
3. [从 STM32CubeMX 导出项目](#3-从-stm32cubemx-导出项目)
4. [项目目录结构一览](#4-项目目录结构一览)
5. [CMake 核心概念](#5-cmake-核心概念先搞懂这几个名词)
6. [逐行详解：工具链文件](#6-逐行详解工具链文件)
7. [逐行详解：CMake 预设文件](#7-逐行详解cmake-预设文件)
8. [逐行详解：主 CMakeLists.txt](#8-逐行详解主-cmakeliststxt)
9. [逐行详解：CubeMX 子项目](#9-逐行详解cubemx-子项目)
10. [一键编译：如何构建项目](#10-一键编译如何构建项目)
11. [添加你自己的代码](#11-添加你自己的代码)
12. [常见问题排查](#12-常见问题排查)

---

## 1. 为什么用 CMake 构建 STM32？

你可能习惯了 Keil MDK 或 IAR EWARM，它们确实好用，但有一些"硬伤"：

| 对比项 | Keil / IAR | CMake + GCC |
|--------|-----------|-------------|
| 费用 | 需要付费授权（社区版有代码限制） | 完全免费、开源 |
| 跨平台 | 仅 Windows | Windows / Linux / macOS 都能编译 |
| 版本管理 | 工程文件是二进制格式，Git diff 看不懂 | 所有构建配置都是纯文本，Git 友好 |
| 代码补全/跳转 | 依赖 IDE 自带功能 | 生成 compile_commands.json，支持 VS Code + clangd |
| 工具链 | 绑定自家编译器 | 自由选择 GCC、Clang 等 |
| CI/CD | 难以集成 | 轻松接入 GitHub Actions 等自动编译流水线 |

简单来说：**CMake 是构建系统的"普通话"，学会了它，你的工程可以到处跑。**

---

## 2. 环境准备：安装所需的工具

你需要安装以下 **3 个工具**，缺一不可：

### 2.1 安装 ARM GCC 交叉编译工具链

这是能在电脑上运行、但生成 ARM 芯片程序的编译器。

1. 下载：[GNU Arm Embedded Toolchain](https://developer.arm.com/downloads/-/gnu-rm)
   - 选择你操作系统的安装包（Windows 选 .exe 或 .zip）
2. 安装/解压后，**把 bin 目录加到系统 PATH 环境变量**
   - 例如：`C:\Program Files (x86)\GNU Arm Embedded Toolchain\10 2021.10\bin`
3. 验证安装：打开终端（CMD 或 PowerShell），输入：
   ```bash
   arm-none-eabi-gcc --version
   ```
   如果能看到版本信息（如 `arm-none-eabi-gcc (GNU Arm Embedded Toolchain 10.3)`），说明安装成功。

### 2.2 安装 CMake

1. 下载：[cmake.org/download](https://cmake.org/download/)
   - Windows 用户选择 .msi 安装包，安装时勾选 **"Add CMake to the system PATH"**
2. 验证安装：
   ```bash
   cmake --version
   ```
   建议版本 **3.22** 或更高。

### 2.3 安装 Ninja 构建工具

Ninja 是一个小型快速的构建工具，比传统的 Make 快很多。

1. 下载：[ninja-build.org](https://ninja-build.org/)
   - Windows 用户直接下载 ninja-win.zip，解压后把 ninja.exe 所在目录加到 PATH
2. 或者用包管理器安装：
   ```bash
   # 如果你装了 Chocolatey
   choco install ninja

   # 如果你装了 Scoop
   scoop install ninja
   ```
3. 验证安装：
   ```bash
   ninja --version
   ```

### 2.4 可选：安装 VS Code + clangd 插件

CMake 编译时会生成 `compile_commands.json` 文件，配合 VS Code 的 clangd 插件，可以获得**代码跳转、自动补全、错误提示**等功能，体验不输 Keil。

---

## 3. 从 STM32CubeMX 导出项目

如果你已经有 CubeMX 生成的项目，可以跳过这一步。

1. 打开 STM32CubeMX，配置好你的芯片和外设
2. 菜单栏 -> **Project Manager** 选项卡：
   - **Toolchain/IDE** 选择 `CMake`
   - 其他设置照常
3. 点击 **Generate Code**
4. CubeMX 会在项目目录下自动生成 CMakeLists.txt、cmake/ 目录等文件

> **提示**：CubeMX 生成的 CMake 文件可能比较基础，本教程会在它基础上进行"现代化改造"。

---

## 4. 项目目录结构一览

一个典型的 CMake STM32 项目的文件结构如下：

```
CMake_STM_demo/
|
+-- CMakeLists.txt                    [核心] 主构建文件，项目的"总指挥"
+-- CMakePresets.json                 [核心] CMake 预设配置（定义编译类型等）
+-- STM32F103CBTx_FLASH.ld           [核心] 链接脚本（定义芯片内存布局）
+-- startup_stm32f103xb.s            [核心] 启动汇编文件（芯片上电后最先执行的代码）
|
+-- cmake/
|   +-- gcc-arm-none-eabi.cmake       [核心] 交叉编译工具链文件（告诉 CMake 用哪个编译器）
|   +-- stm32cubemx/
|       +-- CMakeLists.txt            CubeMX 子项目（管理 HAL 库源文件和头文件）
|
+-- Core/                             CubeMX 生成的用户代码
|   +-- Inc/                          你自己的头文件
|   |   +-- main.h
|   |   +-- stm32f1xx_hal_conf.h
|   |   +-- stm32f1xx_it.h
|   +-- Src/                          你自己的源文件
|       +-- main.c                    程序入口（main 函数在这里）
|       +-- stm32f1xx_it.c            中断处理函数
|       +-- stm32f1xx_hal_msp.c       HAL 底层初始化
|       +-- system_stm32f1xx.c        系统初始化
|       +-- sysmem.c                  内存管理
|       +-- syscalls.c                C 库系统调用桩函数
|
+-- Drivers/                          ST 官方提供的驱动库
|   +-- CMSIS/                        ARM CMSIS 核心库（芯片内核相关）
|   +-- STM32F1xx_HAL_Driver/         ST HAL 驱动库
|
+-- build/                            [自动生成] 编译输出目录（不需要提交到 Git）
    +-- Debug/
        +-- CMake_STM_demo.elf        可执行固件（调试用）
        +-- CMake_STM_demo.bin        烧录用二进制文件
        +-- CMake_STM_demo.hex        烧录用十六进制文件
        +-- CMake_STM_demo.map        内存映射文件
```

### 这些文件各有什么用？

| 文件 | 作用 | 类比 Keil |
|------|------|-----------|
| CMakeLists.txt | 定义"编译规则"：编译哪些文件、用什么参数 | Keil 的 .uvprojx 工程文件 |
| CMakePresets.json | 预定义编译方案（Debug/Release 等） | Keil 里的 Debug/Release 配置 |
| gcc-arm-none-eabi.cmake | 告诉 CMake 用哪个编译器 | Options -> Target -> 选择芯片 |
| STM32F103CBTx_FLASH.ld | 定义芯片的 Flash/RAM 内存布局 | Linker Settings -> Scatter File |
| startup_stm32f103xb.s | 芯片上电启动代码（汇编） | Keil 自动生成，通常不需要改 |

---

## 5. CMake 核心概念：先搞懂这几个名词

在看具体文件之前，先理解这几个 CMake 的核心概念：

### 5.1 目标（Target）

目标是 CMake 中最核心的概念。可以理解为"一个要构建的东西"：

```cmake
add_executable(my_app)           # 创建一个"可执行文件"目标（最终生成 .elf）
add_library(my_lib STATIC)       # 创建一个"静态库"目标（生成 .a 文件）
add_library(my_iface INTERFACE)  # 创建一个"接口库"目标（不生成文件，只传递信息）
```

### 5.2 INTERFACE 库的妙用

INTERFACE 库不会生成任何文件，它的作用是**把一堆编译信息打包传递给别人**：

```cmake
# 定义一个 INTERFACE 库，把源文件、头文件路径、宏定义打包
add_library(stm32cubemx INTERFACE)
target_sources(stm32cubemx INTERFACE main.c hal.c ...)
target_include_directories(stm32cubemx INTERFACE /path/to/headers)
target_compile_definitions(stm32cubemx INTERFACE USE_HAL_DRIVER)

# 另一个目标只要链接这个库，就能自动获得所有信息
target_link_libraries(my_app PRIVATE stm32cubemx)
```

### 5.3 PRIVATE / PUBLIC / INTERFACE 关键字

这三个关键字控制属性的"传递范围"：

| 关键字 | 含义 | 使用场景 |
|--------|------|----------|
| PRIVATE | 只给**当前目标**使用 | 你自己写的源文件、私有的头文件 |
| PUBLIC | 给当前目标使用，**也传递**给链接它的其他目标 | 被外部调用的头文件 |
| INTERFACE | **只传递**给链接它的其他目标，当前目标不用 | 库的头文件、宏定义 |

> 对于大多数情况，用 PRIVATE 就够了。

### 5.4 生成器表达式（Generator Expression）

`$<$<条件:值>` 是 CMake 的"条件表达式"，在构建时根据条件自动选择：

```cmake
# 如果是 Debug 模式，使用 -O0 -g3；如果是 Release 模式，使用 -Os -g0
add_compile_options(
    "$<$<CONFIG:Debug>:-O0;-g3>"
    "$<$<CONFIG:Release>:-Os;-g0>"
)
```

### 5.5 后构建命令（POST_BUILD）

在目标构建完成后自动执行额外的命令：

```cmake
add_custom_command(TARGET my_app POST_BUILD
    COMMAND arm-none-eabi-objcopy -O binary my_app.elf my_app.bin
    COMMENT "生成 .bin 文件"
)
```

---

## 6. 逐行详解：工具链文件

文件路径：`cmake/gcc-arm-none-eabi.cmake`

这个文件必须在 `project()` 被调用**之前**加载，它告诉 CMake 交叉编译的基本设置。

### 6.1 声明目标系统信息

```cmake
set(CMAKE_SYSTEM_NAME Generic)       # 通用嵌入式系统（无操作系统）
set(CMAKE_SYSTEM_PROCESSOR arm)      # 目标处理器架构
```

> **为什么需要这一步？** 因为 CMake 默认认为你在编译"电脑程序"。如果不设置这个，CMake 会尝试用电脑上的编译器（如 MSVC）来编译，而不是 ARM 交叉编译器。

### 6.2 指定编译器

```cmake
set(TOOLCHAIN_PREFIX arm-none-eabi-)

set(CMAKE_C_COMPILER        ${TOOLCHAIN_PREFIX}gcc)     # C 编译器
set(CMAKE_CXX_COMPILER      ${TOOLCHAIN_PREFIX}g++)     # C++ 编译器
set(CMAKE_ASM_COMPILER      ${TOOLCHAIN_PREFIX}gcc)     # 汇编器
set(CMAKE_OBJCOPY           ${TOOLCHAIN_PREFIX}objcopy) # 生成 .bin/.hex
set(CMAKE_SIZE              ${TOOLCHAIN_PREFIX}size)     # 显示固件大小
```

> **重要**：arm-none-eabi-gcc 必须在系统 PATH 中！否则 CMake 找不到它会报错。

### 6.3 跳过编译器能力检测

```cmake
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
```

交叉编译时，CMake 默认会尝试编译一个小程序来检测编译器。但交叉编译的程序没法在电脑上运行，所以会失败。设为 STATIC_LIBRARY 可以跳过链接步骤，只编译不链接，就不会报错了。

### 6.4 编译和链接参数

```cmake
set(TARGET_FLAGS "-mcpu=cortex-m3")   # STM32F103 用的是 Cortex-M3

# C 编译参数（使用 _INIT 后缀，工具链文件的规范写法）
set(CMAKE_C_FLAGS_INIT
    "${TARGET_FLAGS} -Wall -Wextra -Wpedantic -fdata-sections -ffunction-sections"
)

# 汇编编译参数
set(CMAKE_ASM_FLAGS_INIT
    "${TARGET_FLAGS} -x assembler-with-cpp -MMD -MP"
)

# 链接器参数
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${TARGET_FLAGS} \
    -T \"${CMAKE_SOURCE_DIR}/STM32F103CBTx_FLASH.ld\" \
    --specs=nano.specs \
    -Wl,-Map=${CMAKE_PROJECT_NAME}.map \
    -Wl,--gc-sections \
    -Wl,--start-group -lc -lm -Wl,--end-group \
    -Wl,--print-memory-usage"
)
```

### 关键参数解释

| 参数 | 作用 |
|------|------|
| -mcpu=cortex-m3 | 指定目标 CPU 架构 |
| -Wall -Wextra -Wpedantic | 开启各种编译警告（强烈建议保留） |
| -fdata-sections -ffunction-sections | 让每个函数/变量独立成"段"，配合 --gc-sections 删除未使用的代码 |
| -T xxx.ld | 指定链接脚本（定义芯片内存布局） |
| --specs=nano.specs | 使用 Newlib-nano 库（体积小，适合嵌入式） |
| -Wl,--gc-sections | 删除未被调用的函数和变量，缩小固件体积 |
| -Wl,-Map=xxx.map | 生成内存映射文件，调试时很有用 |
| -Wl,--print-memory-usage | 编译完成后打印 Flash/RAM 占用情况 |
| -Wl,--start-group -lc -lm -Wl,--end-group | 链接 C 标准库和数学库 |

> **关于 _INIT 变量**：工具链文件中应该使用 `CMAKE_C_FLAGS_INIT` 而不是 `CMAKE_C_FLAGS`。`_INIT` 后缀的变量是专门给工具链文件用的"初始值"，CMake 会把它们和用户在 CMakeLists.txt 中设置的参数自动合并，不会互相覆盖。

---

## 7. 逐行详解：CMake 预设文件

文件路径：`CMakePresets.json`

这个文件定义了"编译方案"，让你不用每次都手动输入一堆编译参数。

```json
{
    "version": 3,
    "configurePresets": [
        {
            "name": "default",
            "hidden": true,
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/${presetName}",
            "toolchainFile": "${sourceDir}/cmake/gcc-arm-none-eabi.cmake",
            "cacheVariables": {}
        },
        {
            "name": "Debug",
            "inherits": "default",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug"
            }
        },
        {
            "name": "Release",
            "inherits": "default",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Release"
            }
        }
    ]
}
```

### 字段解释

| 字段 | 作用 |
|------|------|
| "version": 3 | 预设文件格式版本，需要 CMake 3.21+ |
| "generator": "Ninja" | 使用 Ninja 作为构建系统（比 Make 更快） |
| "binaryDir" | 编译输出目录，build/Debug 或 build/Release |
| "toolchainFile" | 指定工具链文件的路径 |
| "CMAKE_BUILD_TYPE" | 编译类型：Debug 或 Release |
| "inherits": "default" | 继承 default 预设的所有配置 |
| "hidden": true | 表示这个预设不能直接使用，只能被其他预设继承 |

> **为什么用预设？** 有了它，你只需要输入 `cmake --preset Debug` 就能配置，不需要记住一堆命令行参数。而且 VS Code 的 CMake 插件也能直接识别预设。

### 编译类型说明

| 类型 | 优化级别 | 调试信息 | 用途 |
|------|----------|----------|------|
| Debug | -O0（不优化） | -g3（最详细） | 开发调试阶段 |
| Release | -Os（优化体积） | -g0（无） | 最终发布固件 |
| RelWithDebInfo | -O2（速度优化） | -g（有） | 需要性能分析时 |
| MinSizeRel | -Os（优化体积） | -g0（无） | 追求最小体积 |

---

## 8. 逐行详解：主 CMakeLists.txt

文件路径：`CMakeLists.txt`（项目根目录）

这是整个项目最核心的文件，下面逐步拆解：

### 第一步：声明项目

```cmake
cmake_minimum_required(VERSION 3.22)

project(CMake_STM_demo LANGUAGES C ASM)
```

- `cmake_minimum_required`：要求 CMake 版本不低于 3.22
- `project()`：声明项目名称和使用的语言
  - `LANGUAGES C ASM`：项目使用 C 语言和汇编语言
  - ASM 是因为 STM32 的启动文件 startup_stm32f103xb.s 是汇编写的

### 第二步：设置 C 语言标准

```cmake
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
```

- 使用 C11 标准（CubeMX 生成的代码需要至少 C11）
- `REQUIRED ON`：如果编译器不支持 C11 就报错，而不是悄悄降级
- `EXTENSIONS ON`：允许使用 GNU 扩展语法

### 第三步：设置编译参数

```cmake
add_compile_options(-Wall -Wextra -Wpedantic)
add_compile_options(-fdata-sections -ffunction-sections)

add_compile_options(
    "$<$<CONFIG:Debug>:-O0;-g3>"
    "$<$<CONFIG:Release>:-Os;-g0>"
)
```

- **警告开关**：-Wall -Wextra -Wpedantic 开启所有常见警告
- **段优化**：-fdata-sections -ffunction-sections 配合链接器的 --gc-sections 删除未使用的代码
- **调试/发布选项**：使用生成器表达式根据编译类型自动选择

### 第四步：创建目标并链接库

```cmake
add_subdirectory(cmake/stm32cubemx)

add_executable(${CMAKE_PROJECT_NAME})

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE stm32cubemx)
```

- 先 `add_subdirectory()` 加载 CubeMX 子项目（定义了 HAL 库信息）
- 再 `add_executable()` 创建可执行目标
- 最后 `target_link_libraries()` 把 HAL 库信息传递给目标

> **顺序很重要**：必须先加载子项目（定义 INTERFACE 库），再创建目标，再链接。否则 CMake 找不到 stm32cubemx 这个库。

### 第五步：添加用户自己的代码（可选）

```cmake
# 添加你自己的源文件
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Core/Src/my_driver.c
)

# 添加你自己的头文件目录
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # ${CMAKE_SOURCE_DIR}/MyDrivers/Inc
)
```

这两个区域是你**最常修改**的地方。写了新的 .c 文件就加到 target_sources，写了新的 .h 文件就加到 target_include_directories。

### 第六步：生成烧录文件

```cmake
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
        $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        ${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex
        $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        ${CMAKE_PROJECT_NAME}.hex
    COMMENT ">>> 正在生成烧录文件"
)
```

编译完成后自动把 .elf 转成 .bin 和 .hex，直接拿去烧录即可。

### 第七步：显示固件大小

```cmake
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    COMMENT ">>> 固件大小信息:"
)
```

编译完成后自动显示 Flash/RAM 占用情况，类似这样：

```
Memory region         Used Size  Region Size  %age Used
             RAM:        1584 B        20 KB      7.73%
           FLASH:        3604 B       128 KB      2.75%
```

---

## 9. 逐行详解：CubeMX 子项目

文件路径：`cmake/stm32cubemx/CMakeLists.txt`

这个文件管理 STM32 HAL 库的所有编译信息。

### 创建 INTERFACE 库

```cmake
add_library(stm32cubemx INTERFACE)
```

创建一个 INTERFACE 库，它不会编译成任何文件，只负责"打包传递信息"。

### 定义编译宏

```cmake
target_compile_definitions(stm32cubemx INTERFACE
    USE_HAL_DRIVER
    STM32F103xB
    $<$<CONFIG:Debug>:DEBUG>
)
```

- `USE_HAL_DRIVER`：让 HAL 库编译全部功能
- `STM32F103xB`：告诉 HAL 库你用的是 STM32F103 中等容量系列

### 头文件路径

```cmake
target_include_directories(stm32cubemx INTERFACE
    ${CMAKE_SOURCE_DIR}/Core/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Include
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Include
)
```

> **重要**：这里使用 `${CMAKE_SOURCE_DIR}` 表示项目根目录，这样无论你在哪个目录执行编译，路径都不会错。

### 源文件列表

```cmake
target_sources(stm32cubemx INTERFACE
    # --- 用户代码 ---
    ${CMAKE_SOURCE_DIR}/Core/Src/main.c
    ${CMAKE_SOURCE_DIR}/Core/Src/stm32f1xx_it.c
    ${CMAKE_SOURCE_DIR}/Core/Src/stm32f1xx_hal_msp.c
    ${CMAKE_SOURCE_DIR}/Core/Src/system_stm32f1xx.c
    ${CMAKE_SOURCE_DIR}/Core/Src/sysmem.c
    ${CMAKE_SOURCE_DIR}/Core/Src/syscalls.c

    # --- HAL 驱动库 ---
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c
    ...（其他 HAL 库源文件）

    # --- 启动文件 ---
    ${CMAKE_SOURCE_DIR}/startup_stm32f103xb.s
)
```

> **如果 CubeMX 重新生成代码**，可能需要更新这里的源文件列表。例如，你新开了一个外设（如 SPI），CubeMX 会生成新的 stm32f1xx_hal_spi.c，你需要把它加到这里。

### C 标准版本检查

```cmake
if(CMAKE_C_STANDARD LESS 11)
    message(FATAL_ERROR "STM32CubeMX 生成的代码需要 C11 或更高版本的 C 标准！")
endif()
```

这是一个安全检查，确保主 CMakeLists.txt 中设置的 C 标准版本足够高。

---

## 10. 一键编译：如何构建项目

### 10.1 首次配置（只需要做一次）

```bash
# 在项目根目录下执行
cmake --preset Debug
```

这条命令会：
1. 读取 CMakePresets.json 中的 Debug 预设
2. 加载工具链文件 cmake/gcc-arm-none-eabi.cmake
3. 分析所有 CMakeLists.txt 文件
4. 在 build/Debug/ 目录下生成 Ninja 构建文件

### 10.2 编译

```bash
# 编译 Debug 版本
cmake --build --preset Debug

# 编译 Release 版本
cmake --build --preset Release
```

### 10.3 输出文件在哪里？

编译成功后，`build/Debug/` 目录下会生成：

| 文件 | 用途 |
|------|------|
| CMake_STM_demo.elf | 可执行固件，用于调试器（如 ST-Link、J-Link）直接加载 |
| CMake_STM_demo.bin | 二进制烧录文件，用于 STM32CubeProgrammer、串口下载工具等 |
| CMake_STM_demo.hex | Intel HEX 烧录文件，包含地址信息，用于 ST-Link 烧录 |
| CMake_STM_demo.map | 内存映射文件，可以查看每个函数/变量占了多少空间 |

### 10.4 清理编译产物

```bash
# 直接删除 build 目录即可
rm -rf build/
```

或者用 Ninja 的清理命令：
```bash
cmake --build --preset Debug --target clean
```

### 10.5 编译流程图

```
cmake --preset Debug            cmake --build --preset Debug
       |                                  |
       v                                  v
  读取 CMakePresets.json           读取 build.ninja 构建文件
       |                                  |
       v                                  v
  加载工具链文件                    编译所有 .c 文件 -> .o 文件
       |                                  |
       v                                  v
  分析 CMakeLists.txt              链接所有 .o 文件 -> .elf
       |                                  |
       v                                  v
  生成 build/Debug/build.ninja    objcopy -> .bin 和 .hex
                                          |
                                          v
                                     size -> 打印大小
```

---

## 11. 添加你自己的代码

### 11.1 添加一个新的外设驱动

假设你新建了一个驱动文件 `Core/Src/my_spi_driver.c` 和 `Core/Inc/my_spi_driver.h`。

**步骤一**：在主 CMakeLists.txt 中添加源文件：

```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/my_spi_driver.c        # 添加你的新驱动文件
)
```

**步骤二**：在主 CMakeLists.txt 中添加头文件路径（如果头文件在新目录）：

```cmake
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/Core/Inc    # 你的头文件目录
)
```

**步骤三**：重新编译：

```bash
cmake --build --preset Debug
```

### 11.2 添加一个新的 HAL 模块

比如你在 CubeMX 中启用了 SPI1 外设，CubeMX 会生成 `stm32f1xx_hal_spi.c`。

你需要在 `cmake/stm32cubemx/CMakeLists.txt` 的源文件列表中添加：

```cmake
target_sources(stm32cubemx INTERFACE
    ...（原有文件）
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_spi.c
)
```

### 11.3 添加一个外部库

假设你有一个第三方库 `libs/my_lib/`：

```cmake
# 创建静态库目标
add_library(my_lib STATIC
    ${CMAKE_SOURCE_DIR}/libs/my_lib/src/lib.c
)

# 设置头文件搜索路径
target_include_directories(my_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/libs/my_lib/inc
)

# 链接到主目标
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE my_lib)
```

### 11.4 添加条件编译代码

在你的 C 代码中可以使用 CMake 定义的宏：

```c
// CMakeLists.txt 中定义了: target_compile_definitions(... PRIVATE USE_MY_DEBUG)
#ifdef USE_MY_DEBUG
    printf("调试信息\n");
#endif
```

---

## 12. 常见问题排查

### Q1：CMake 报错 "The C compiler is not found"

**原因**：arm-none-eabi-gcc 没有加到系统 PATH 环境变量中。

**解决**：
1. 找到 arm-none-eabi-gcc 的安装路径（如 `C:\Program Files (x86)\GNU Arm Embedded Toolchain\10 2021.10\bin`）
2. 把这个路径加到系统环境变量 PATH 中
3. 重启终端，再试一次

### Q2：CMake 报错 "CMakeLists.txt: No SOURCES given to target"

**原因**：add_executable 没有源文件，且 INTERFACE 库还没有被链接。

**解决**：确保文件顺序正确：
```cmake
add_subdirectory(cmake/stm32cubemx)   # 先加载子项目
add_executable(${CMAKE_PROJECT_NAME}) # 再创建目标
target_link_libraries(... stm32cubemx) # 再链接库
```

### Q3：链接时报错 "cannot find -lc" 或 "cannot find -lm"

**原因**：编译工具链的 sysroot 路径没有正确设置，或者工具链不完整。

**解决**：
1. 确认安装的是完整的 GNU Arm Embedded Toolchain（不是只有 gcc）
2. 尝试重新安装工具链

### Q4：编译时报错 "fatal error: stm32f1xx_hal.h: No such file or directory"

**原因**：头文件路径没有正确配置。

**解决**：检查 `cmake/stm32cubemx/CMakeLists.txt` 中的 `target_include_directories` 是否包含了正确的路径。

### Q5：链接时报错 "STM32F103CBTx_FLASH.ld:56: syntax error"

**原因**：链接脚本中 `ORIGIN()` 和 `LENGTH()` 缺少内存区域名称。

**解决**：打开链接脚本，找到类似这行：
```
_estack = ORIGIN() + LENGTH();    // 错误
```
改为：
```
_estack = ORIGIN(RAM) + LENGTH(RAM);    // 正确
```
同时检查 `} >` 是否缺少 RAM 或 FLASH：

```
// 错误：缺少目标内存区域
} >

// 正确
} > RAM
```

### Q6：如何从 Keil 迁移到 CMake？

**推荐步骤**：
1. 用 CubeMX 重新配置你的芯片（.ioc 文件），选择 CMake 作为 Toolchain/IDE
2. 生成代码后，用本教程的模板替换自动生成的 CMakeLists.txt
3. 把你的用户代码（Core/Src 和 Core/Inc）复制到新项目
4. 编译测试

### Q7：CMake 配置成功但编译时报很多警告

**解决**：这是正常的！GCC 的 -Wall -Wextra -Wpedantic 会报告很多信息。如果是 ST 官方 HAL 库的警告，可以暂时忽略。如果是你自己代码的警告，建议认真修复，它们可能隐藏着 Bug。

### Q8：如何在 VS Code 中配置开发环境？

1. 安装以下 VS Code 插件：
   - **C/C++**（Microsoft 官方）
   - **clangd**（更好的代码补全和跳转）
   - **CMake Tools**（CMake 集成）
2. 打开项目文件夹，VS Code 会自动检测到 CMakePresets.json
3. 在底部状态栏选择编译预设（如 Debug）
4. 按 Ctrl+Shift+B 编译

---

> **教程完毕！** 如果你跟着做了一遍，应该已经能成功编译 STM32 项目了。接下来就可以在这个基础上开发你自己的 STM32 应用了。祝编程愉快！
