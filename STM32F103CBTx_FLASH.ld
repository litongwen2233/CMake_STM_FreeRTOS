/*
******************************************************************************
**

**  File        : LinkerScript.ld
**
**  Author		: STM32CubeMX
**
**  Abstract    : Linker script for STM32F103CBTx series
**                128Kbytes FLASH and 20Kbytes RAM
**
**                Set heap size, stack size and stack location according
**                to application requirements.
**
**                Set memory bank area and size if external memory is used.
**
**  Target      : STMicroelectronics STM32
**
**  Distribution: The file is distributed “as is,” without any warranty
**                of any kind.
**
*****************************************************************************
** @attention
**
** <h2><center>&copy; COPYRIGHT(c) 2019 STMicroelectronics</center></h2>
**
** Redistribution and use in source and binary forms, with or without modification,
** are permitted provided that the following conditions are met:
**   1. Redistributions of source code must retain the above copyright notice,
**      this list of conditions and the following disclaimer.
**   2. Redistributions in binary form must reproduce the above copyright notice,
**      this list of conditions and the following disclaimer in the documentation
**      and/or other materials provided with the distribution.
**   3. Neither the name of STMicroelectronics nor the names of its contributors
**      may be used to endorse or promote products derived from this software
**      without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
** OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
*****************************************************************************
*/

/* Entry Point */
ENTRY(Reset_Handler)

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM) + LENGTH(RAM);    /* end of RAM */
/* Generate a link error if heap and stack don't fit into RAM */
_Min_Heap_Size = 0x200;      /* required amount of heap  */
_Min_Stack_Size = 0x400; /* required amount of stack */

/* Specify the memory areas */
/* 用于定义目标芯片的物理内存区域 包括器名称 起始地址 长度 访问权限 */
/*语法结构
**MEMORY
**{
**    NAME1 [(attr)] : ORIGIN = origin, LENGTH = len
**    NAME2 [(attr)] : ORIGIN = origin, LENGTH = len
**}
**NAME：内存区域的名字，可自定义。
**attr：可选的属性标志，如 r（只读）、w（可读写）、x（可执行）、a（可分配的）。
**ORIGIN：区域的起始地址，可简写为 org 或 o。
**LENGTH：区域的长度，可简写为 len 或 l。
*/
MEMORY
{
RAM (xrw)      : ORIGIN = 0x20000000, LENGTH = 20K
FLASH (rx)      : ORIGIN = 0x8000000, LENGTH = 128K
}

/* Define output sections */
/* SECTIONS 命令是链接脚本的核心，它定义了输出文件中的段（Section）以及这些段在内存中的具体布局和内容 */
/*
** SECTIONS
** {
**     .段名 : [AT(LMA地址)]
**     {
**         [输入文件(.输入段名)]
**         [符号赋值]
**     } > 内存区域名
** }
** .（位置计数器，Location Counter）：一个特殊的链接器变量，始终指向当前的输出地址。可以通过对其赋值来调整后续段的放置位置（如 . = 0x10000;） 
** *(.text)：* 是通配符，表示所有输入文件；*(.text) 表示将所有输入文件中的 .text 段合并到当前输出段中
** KEEP()：告知链接器，即使该段看起来没有被引用，也必须保留在输出文件中，通常用于中断向量表等关键段
** ALIGN(4)：将当前位置计数器对齐到指定的字节边界（如 4 字节对齐），确保内存访问的正确性
** AT>： 指定加载地址（LMA）位于哪个内存区域。这对于 .data 段非常重要，因为它的初始值存储在 Flash 中（LMA），但运行时需要复制到 RAM 中（VMA）
** >： 指定段的运行地址（VMA）位于哪个内存区域
** 链接器在处理各段的放置顺序时，会按照 SECTIONS 命令中段定义的先后顺序进行
*/
SECTIONS
{
  /* The startup code goes first into FLASH */
  /* 中断向量表，必须放在Flash起始地址 */
  .isr_vector :
  {
    . = ALIGN(4);         /* 将当前位置计数器（.）的值，向上对齐到下一个 4 字节（32 位）的整数倍地址 */
    KEEP(*(.isr_vector)) /*强制保留中断向量表 Startup code */
    . = ALIGN(4);
  } >FLASH

  /* The program code and other data goes into FLASH */
  /* 程序代码段 存放所有 C/C++ 函数编译后的机器指令*/
  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >FLASH

  /* Constant data goes into FLASH */
  /*只读数据段*/
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >FLASH

  /* 异常处理与栈回溯信息 */
  .ARM.extab   : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  /*preinit_array、.init_array、.fini_array：构造与析构函数数组*/
  .preinit_array     :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH
  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH
  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH

  /* used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections goes into RAM, load LMA copy after code */
  /*.data：已初始化全局变量*/
  .data : 
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */

    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */
  } > RAM AT> FLASH

  
  /* Uninitialized data section */
  /*.bss：未初始化/零初始化全局变量*/
  . = ALIGN(4);
  .bss :
  {
    /* This is used by the startup in order to initialize the .bss secion */
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
  } > RAM

  /* User_heap_stack section, used to check that there is enough RAM left */
  /*堆和栈空间*/
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } > RAM

  

  /* Remove information from the standard libraries */
  /*丢弃标准库调试信息 */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

}


