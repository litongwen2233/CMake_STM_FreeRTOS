/*
 * FreeRTOS Kernel V11.3.1
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/*-----------------------------------------------------------
* Implementation of functions defined in portable.h for the ARM CM3 port.
*----------------------------------------------------------*/

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"

#if configMAX_SYSCALL_INTERRUPT_PRIORITY == 0
    #error configMAX_SYSCALL_INTERRUPT_PRIORITY must not be set to 0.  See http: /*www.FreeRTOS.org/RTOS-Cortex-M3-M4.html */
#endif

/* Legacy macro for backward compatibility only.  This macro used to be used to
 * replace the function that configures the clock used to generate the tick
 * interrupt (prvSetupTimerInterrupt()), but now the function is declared weak so
 * the application writer can override it by simply defining a function of the
 * same name (vApplicationSetupTickInterrupt()). */
#ifndef configOVERRIDE_DEFAULT_TICK_CONFIGURATION
    #define configOVERRIDE_DEFAULT_TICK_CONFIGURATION    0
#endif

/* Constants required to manipulate the core.  Registers first... */
#define portNVIC_SYSTICK_CTRL_REG             ( *( ( volatile uint32_t * ) 0xe000e010 ) )
#define portNVIC_SYSTICK_LOAD_REG             ( *( ( volatile uint32_t * ) 0xe000e014 ) )
#define portNVIC_SYSTICK_CURRENT_VALUE_REG    ( *( ( volatile uint32_t * ) 0xe000e018 ) )
#define portNVIC_SHPR3_REG                    ( *( ( volatile uint32_t * ) 0xe000ed20 ) )
/* ...then bits in the registers. */
#define portNVIC_SYSTICK_CLK_BIT              ( 1UL << 2UL )
#define portNVIC_SYSTICK_INT_BIT              ( 1UL << 1UL )
#define portNVIC_SYSTICK_ENABLE_BIT           ( 1UL << 0UL )
#define portNVIC_SYSTICK_COUNT_FLAG_BIT       ( 1UL << 16UL )
#define portNVIC_PENDSVCLEAR_BIT              ( 1UL << 27UL )
#define portNVIC_PEND_SYSTICK_SET_BIT         ( 1UL << 26UL )
#define portNVIC_PEND_SYSTICK_CLEAR_BIT       ( 1UL << 25UL )

#define portMIN_INTERRUPT_PRIORITY            ( 255UL )
#define portNVIC_PENDSV_PRI                   ( ( ( uint32_t ) portMIN_INTERRUPT_PRIORITY ) << 16UL )
#define portNVIC_SYSTICK_PRI                  ( ( ( uint32_t ) portMIN_INTERRUPT_PRIORITY ) << 24UL )

/* Constants required to check the validity of an interrupt priority. */
#define portFIRST_USER_INTERRUPT_NUMBER       ( 16 )
#define portNVIC_IP_REGISTERS_OFFSET_16       ( 0xE000E3F0 )
#define portAIRCR_REG                         ( *( ( volatile uint32_t * ) 0xE000ED0C ) )
#define portMAX_8_BIT_VALUE                   ( ( uint8_t ) 0xff )
#define portTOP_BIT_OF_BYTE                   ( ( uint8_t ) 0x80 )
#define portMAX_PRIGROUP_BITS                 ( ( uint8_t ) 7 )
#define portPRIORITY_GROUP_MASK               ( 0x07UL << 8UL )
#define portPRIGROUP_SHIFT                    ( 8UL )

/* Masks off all bits but the VECTACTIVE bits in the ICSR register. */
#define portVECTACTIVE_MASK                   ( 0xFFUL )

/* Constants required to set up the initial stack. */
#define portINITIAL_XPSR                      ( 0x01000000 )

/* The systick is a 24-bit counter. */
#define portMAX_24_BIT_NUMBER                 ( 0xffffffUL )

/* A fiddle factor to estimate the number of SysTick counts that would have
 * occurred while the SysTick counter is stopped during tickless idle
 * calculations. */
#define portMISSED_COUNTS_FACTOR              ( 94UL )

/* For strict compliance with the Cortex-M spec the task start address should
 * have bit-0 clear, as it is loaded into the PC on exit from an ISR. */
#define portSTART_ADDRESS_MASK                ( ( StackType_t ) 0xfffffffeUL )

/* Let the user override the default SysTick clock rate.  If defined by the
 * user, this symbol must equal the SysTick clock rate when the CLK bit is 0 in the
 * configuration register. */
#ifndef configSYSTICK_CLOCK_HZ
    #define configSYSTICK_CLOCK_HZ             ( configCPU_CLOCK_HZ )
    /* Ensure the SysTick is clocked at the same frequency as the core. */
    #define portNVIC_SYSTICK_CLK_BIT_CONFIG    ( portNVIC_SYSTICK_CLK_BIT )
#else
    /* Select the option to clock SysTick not at the same frequency as the core. */
    #define portNVIC_SYSTICK_CLK_BIT_CONFIG    ( 0 )
#endif

/*
 * Setup the timer to generate the tick interrupts.  The implementation in this
 * file is weak to allow application writers to change the timer used to
 * generate the tick interrupt.
 */
void vPortSetupTimerInterrupt( void );

/*
 * Exception handlers.
 */
void xPortPendSVHandler( void );
void xPortSysTickHandler( void );
void vPortSVCHandler( void );

/*
 * Start first task is a separate function so it can be tested in isolation.
 */
static void prvStartFirstTask( void );

/*
 * Used to catch tasks that attempt to return from their implementing function.
 */
static void prvTaskExitError( void );

/*-----------------------------------------------------------*/

/* Each task maintains its own interrupt status in the critical nesting
 * variable. */
static UBaseType_t uxCriticalNesting = 0xaaaaaaaa;

/*
 * The number of SysTick increments that make up one tick period.
 */
#if ( configUSE_TICKLESS_IDLE == 1 )
    static uint32_t ulTimerCountsForOneTick = 0;
#endif /* configUSE_TICKLESS_IDLE */

/*
 * The maximum number of tick periods that can be suppressed is limited by the
 * 24 bit resolution of the SysTick timer.
 */
#if ( configUSE_TICKLESS_IDLE == 1 )
    static uint32_t xMaximumPossibleSuppressedTicks = 0;
#endif /* configUSE_TICKLESS_IDLE */

/*
 * Compensate for the CPU cycles that pass while the SysTick is stopped (low
 * power functionality only.
 */
#if ( configUSE_TICKLESS_IDLE == 1 )
    static uint32_t ulStoppedTimerCompensation = 0;
#endif /* configUSE_TICKLESS_IDLE */

/*
 * Used by the portASSERT_IF_INTERRUPT_PRIORITY_INVALID() macro to ensure
 * FreeRTOS API functions are not called from interrupts that have been assigned
 * a priority above configMAX_SYSCALL_INTERRUPT_PRIORITY.
 */
#if ( configASSERT_DEFINED == 1 )
    static uint8_t ucMaxSysCallPriority = 0;
    static uint32_t ulMaxPRIGROUPValue = 0;
    static const volatile uint8_t * const pcInterruptPriorityRegisters = ( uint8_t * ) portNVIC_IP_REGISTERS_OFFSET_16;
#endif /* configASSERT_DEFINED */

/*-----------------------------------------------------------*/

/*
 * See header file for description.
    初始化任务栈内存
 */
StackType_t * pxPortInitialiseStack( StackType_t * pxTopOfStack,
                                     TaskFunction_t pxCode,
                                     void * pvParameters )
{
    /* Simulate the stack frame as it would be created by a context switch
     * interrupt. */
    //发生异常时，自动加载到CPU寄存器的内容
    pxTopOfStack--;                                                      /* Offset added to account for the way the MCU uses the stack on entry/exit of interrupts. */
    *pxTopOfStack = portINITIAL_XPSR;                                    /* xPSR */
    pxTopOfStack--;
    *pxTopOfStack = ( ( StackType_t ) pxCode ) & portSTART_ADDRESS_MASK; /* PC */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) prvTaskExitError;  // 任务返回地址，返回了就要这个函数里面                  /* LR */

    /* R12, R3, R2 and R1 默认初始化为0 */
    pxTopOfStack -= 5;                                                   /* R12, R3, R2 and R1. */
    *pxTopOfStack = ( StackType_t ) pvParameters;                        /* R0 */
    
    // 异常发生时，手动加载到cpu寄存器的内容
    pxTopOfStack -= 8;                                                   /* R11, R10, R9, R8, R7, R6, R5 and R4. */

    // 返回栈顶指针，此时pxTopOfStack指向空闲栈
    return pxTopOfStack;
}
/*-----------------------------------------------------------*/

static void prvTaskExitError( void )
{
    /* A function that implements a task must not exit or attempt to return to
     * its caller as there is nothing to return to.  If a task wants to exit it
     * should instead call vTaskDelete( NULL ).
     *
     * Artificially force an assert() to be triggered if configASSERT() is
     * defined, then stop here so application writers can catch the error. */
    configASSERT( uxCriticalNesting == ~0UL );
    portDISABLE_INTERRUPTS();

    for( ; ; )
    {
    }
}
/*-----------------------------------------------------------*/
//SVC 中断服务函数，启动第一个函数
__asm void vPortSVCHandler( void )
{
/* *INDENT-OFF* */
    //8 字节对齐
    PRESERVE8
    //将 pxCurrentTCB 指针的地址加载到寄存器 r3
    ldr r3, = pxCurrentTCB   /* Restore the context. */
    //将 pxCurrentTCB 指针指向的地址（存储TCB的地址）加载到寄存器 r1
    ldr r1, [ r3 ] /* Use pxCurrentTCBConst to get the pxCurrentTCB address. */
    //将 pxCurrentTCB 指针指向的地址里的值（当前运行的TCB结构体）加载到寄存器r0
    //任务控制块的第一个成员就是栈顶指针，所以r0中存储了当前运行的任务的栈顶指针的地址
    ldr r0, [ r1 ]           /* The first item in pxCurrentTCB is the task top of stack. */
    //以寄存器 r0 为基地址，将任务栈中存储的值加载到寄存器r4 ~ r11 中
    //同时寄存器 r0 的值加载给psp
    ldmia r0 !, { r4 - r11 } /* Pop the registers that are not automatically saved on exception entry and the critical nesting count. */
    //将更新后的寄存器 r0 的值加载给psp
    msr psp, r0 /* Restore the task stack pointer. */
    isb
    //开中断
    mov r0, # 0
    msr basepri, r0
    //指示处理器在异常时 使用psp作为堆栈指针
    orr r14, # 0xd
    //异常返回，自动加载剩下的 xPSR,PC,R14,R12和R3 ~ R0 寄存器
    bx r14
/* *INDENT-ON* */
}
/*-----------------------------------------------------------*/

__asm void prvStartFirstTask( void )
{
/* *INDENT-OFF* 8字节对齐 */
    PRESERVE8

    /* Use the NVIC offset register to locate the stack. */
    /**在corteex-M中，内核外设SCB的地址范围为：0xE000ED00-0xE000ED3F
     * 0xE000ED08是SCB_VTOR这个寄存器的地址，这是向量表偏移寄存器
     * 里面放的是向量表的起始地址，向量表通常是从内部FLASH的其实地址0x00000000开始存放
     */
    ldr r0, =0xE000ED08
    ldr r0, [ r0 ]      //引用r0 相当于r0 = *r0 此时 r0 = 0x00000000
    ldr r0, [ r0 ]      //将0x00000000的内容放到r0，0x00000000中存放着MSP的初始值

    /* Set the msp back to the start of the stack. */
    msr msp, r0 //设置主指针 msp的值
    /* Globally enable interrupts. */
    //启用全局中断
    cpsie i
    cpsie f
    dsb
    isb
    /* Call SVC to start the first task. */
    //触发SVC中断，启动第一个任务
    svc 0
    nop
    nop
/* *INDENT-ON* */
}
/*-----------------------------------------------------------*/

/*
 * See header file for description.
 * 调度器启动函数
 */
BaseType_t xPortStartScheduler( void )
{
    /* ============================================================
     * 第一部分：中断优先级配置校验（仅当 configASSERT 打开时编译）
     * 目的：确认硬件实际实现了几个优先级位，并据此校准
     *      configMAX_SYSCALL_INTERRUPT_PRIORITY，防止用户配置错误
     * ============================================================ */
    #if ( configASSERT_DEFINED == 1 )
    {
        volatile uint8_t ucOriginalPriority;    /* 保存被改写的优先级寄存器原值，校验完要还原 */
        volatile uint32_t ulImplementedPrioBits = 0;/* 硬件实际实现的优先级位数（0~8） */
         /* 取"第一个用户中断"对应的优先级寄存器地址：
         * portNVIC_IP_REGISTERS_OFFSET_16 = NVIC 优先级寄存器组基址(0xE000E400)
         * portFIRST_USER_INTERRUPT_NUMBER = 第一个用户可用的外部中断号(通常是 16，即 IRQ0)
         * 思路：往这个寄存器写全 1，再读回来，看几位"粘住"了 = 硬件实现了几位
         */
        volatile uint8_t * const pucFirstUserPriorityRegister = ( uint8_t * ) ( portNVIC_IP_REGISTERS_OFFSET_16 + portFIRST_USER_INTERRUPT_NUMBER );
        volatile uint8_t ucMaxPriorityValue;/* 读回来的最大值，位模式反映实现了几位 */

        /* Determine the maximum priority from which ISR safe FreeRTOS API
         * functions can be called.  ISR safe functions are those that end in
         * "FromISR".  FreeRTOS maintains separate thread and ISR API functions to
         * ensure interrupt entry is as fast and simple as possible.
         *
         * Save the interrupt priority value that is about to be clobbered. */
          /* 保存即将被破坏的中断优先级值（校验完要还原，避免影响其他代码 */
        ucOriginalPriority = *pucFirstUserPriorityRegister;

        /* Determine the number of priority bits available.  First write to all
         * possible bits. */
        /* 先往寄存器写全 0xFF（portMAX_8_BIT_VALUE = 0xFF），把所有可能位都置 1 */
        *pucFirstUserPriorityRegister = portMAX_8_BIT_VALUE;

        /* Read the value back to see how many bits stuck. */
         /* 再读回来：硬件没实现的位会读回 0，据此判断实际实现了几位
         * 例如读回 0xF0 → 高 4 位有效 → 实现 4 个优先级位 */
        ucMaxPriorityValue = *pucFirstUserPriorityRegister;

        /* Use the same mask on the maximum system call priority. */
        /* 用同一掩码约束系统调用最大优先级：
         * 把用户配置的值与"硬件实际支持的位"相与，去掉无效位 */
        ucMaxSysCallPriority = configMAX_SYSCALL_INTERRUPT_PRIORITY & ucMaxPriorityValue;

        /* Check that the maximum system call priority is nonzero after
         * accounting for the number of priority bits supported by the
         * hardware. A priority of 0 is invalid because setting the BASEPRI
         * register to 0 unmasks all interrupts, and interrupts with priority 0
         * cannot be masked using BASEPRI.
         * See https://www.FreeRTOS.org/RTOS-Cortex-M3-M4.html */
        /* 断言 1：校准后的最大系统调用优先级不能为 0
         * 原因：BASEPRI 写 0 表示"不屏蔽任何中断"，而优先级 0 的中断
         *      本来也无法用 BASEPRI 屏蔽，所以 0 是非法值 */
        configASSERT( ucMaxSysCallPriority );

        /* Check that the bits not implemented in hardware are zero in
         * configMAX_SYSCALL_INTERRUPT_PRIORITY. */
         /* 断言 2：configMAX_SYSCALL_INTERRUPT_PRIORITY 里"硬件未实现的位"必须为 0
         * 例如硬件只实现高 4 位，那配置值必须形如 0xX0 这种低 4 位为 0 的形式 */
        configASSERT( ( configMAX_SYSCALL_INTERRUPT_PRIORITY & ( ~ucMaxPriorityValue ) ) == 0U );

        /* Calculate the maximum acceptable priority group value for the number
         * of bits read back. */
         /* 统计实现了几位：从最高位开始，只要最高位还是 1，就计数并左移
         * 例如 ucMaxPriorityValue=0xF0，循环 4 次后变 0x00，得 ulImplementedPrioBits=4 */
        while( ( ucMaxPriorityValue & portTOP_BIT_OF_BYTE ) == portTOP_BIT_OF_BYTE )
        {
            ulImplementedPrioBits++;
            ucMaxPriorityValue <<= ( uint8_t ) 0x01;
        }

        if( ulImplementedPrioBits == 8 )
        {
            /* When the hardware implements 8 priority bits, there is no way for
             * the software to configure PRIGROUP to not have sub-priorities. As
             * a result, the least significant bit is always used for sub-priority
             * and there are 128 preemption priorities and 2 sub-priorities.
             *
             * This may cause some confusion in some cases - for example, if
             * configMAX_SYSCALL_INTERRUPT_PRIORITY is set to 5, both 5 and 4
             * priority interrupts will be masked in Critical Sections as those
             * are at the same preemption priority. This may appear confusing as
             * 4 is higher (numerically lower) priority than
             * configMAX_SYSCALL_INTERRUPT_PRIORITY and therefore, should not
             * have been masked. Instead, if we set configMAX_SYSCALL_INTERRUPT_PRIORITY
             * to 4, this confusion does not happen and the behaviour remains the same.
             *
             * The following assert ensures that the sub-priority bit in the
             * configMAX_SYSCALL_INTERRUPT_PRIORITY is clear to avoid the above mentioned
             * confusion. */
            /* 特殊情况：硬件实现了完整 8 位优先级
             * 此时软件无法配出"没有子优先级"的 PRIGROUP，最低位必然用于子优先级，
             * 结果是 128 个抢占优先级 × 2 个子优先级。
             * 这会带来一个易混淆现象：若 configMAX_SYSCALL_INTERRUPT_PRIORITY=5，
             * 则优先级 5 和 4 的中断在临界区里都会一起被屏蔽（它们抢占优先级相同）。
             * 断言保证配置值的子优先级位（bit0）为 0，避免这种混淆 */
            configASSERT( ( configMAX_SYSCALL_INTERRUPT_PRIORITY & 0x1U ) == 0U );
            ulMaxPRIGROUPValue = 0;
        }
        else
        {
            /* 非 8 位情况：算出最大可用的 PRIGROUP 值
             * portMAX_PRIGROUP_BITS = 7（PRIGROUP 字段共 3 位，最大 7）
             * 例如实现 4 位 → ulMaxPRIGROUPValue = 7 - 4 = 3 */
            ulMaxPRIGROUPValue = portMAX_PRIGROUP_BITS - ulImplementedPrioBits;
        }

        /* Shift the priority group value back to its position within the AIRCR
         * register. */
        /* 把 PRIGROUP 值移到 AIRCR 寄存器中的正确位置并掩码
         * portPRIGROUP_SHIFT = 8（AIRCR 的 PRIGROUP 字段在 bit[10:8]）
         * portPRIORITY_GROUP_MASK = 0x07 << 8 */
        ulMaxPRIGROUPValue <<= portPRIGROUP_SHIFT;
        ulMaxPRIGROUPValue &= portPRIORITY_GROUP_MASK;

        /* Restore the clobbered interrupt priority register to its original
         * value. */
        /* 把当初被我们改写的那个优先级寄存器还原，不留副作用 */
        *pucFirstUserPriorityRegister = ucOriginalPriority;
    }
    #endif /* configASSERT_DEFINED */

    /* Make PendSV and SysTick the lowest priority interrupts. */
    /* ============================================================
     * 第二部分：设置 PendSV 和 SysTick 为最低优先级
     * 原因：这两个系统异常负责上下文切换和系统节拍，
     *      必须能抢占普通任务，且不能打断用户的中断服务程序，
     *      所以设为优先级最低（数值最大）
     * SHPR3 寄存器：同时管理 PendSV(bit[23:16]) 和 SysTick(bit[31:24])
     * ============================================================ */
    portNVIC_SHPR3_REG |= portNVIC_PENDSV_PRI;  /* 设 PendSV 优先级最低 */

    portNVIC_SHPR3_REG |= portNVIC_SYSTICK_PRI; /* 设 SysTick 优先级最低 */

    /* Start the timer that generates the tick ISR.  Interrupts are disabled
     * here already. */
    /* ============================================================
     * 第三部分：启动产生节拍中断的定时器
     * 通常是配置 SysTick：设重载值、清计数、使能中断和计数
     * 注意：此时全局中断还是关闭状态（调用者已关），
     *      要等 prvStartFirstTask() 里 SVC 异常返回时才真正开启
     * ============================================================ */
    vPortSetupTimerInterrupt();

    /* Initialise the critical nesting count ready for the first task. */
    /* 初始化临界区嵌套计数，为第一个任务准备好
     * 该变量记录"中断被屏蔽了多少层"，第一个任务开始时应为 0 */
    uxCriticalNesting = 0;

    /* Start the first task. */
     /* ============================================================
     * 第四部分：启动第一个任务（关键且"不返回"）
     * 内部会：
     *   1. 设置 MSP 为栈顶
     *   2. 触发 SVC 异常（svc 0）
     *   3. SVC 处理程序里把 PSP 指向第一个任务的栈，并 bx r14 异常返回
     *   4. 从此 CPU 就"住进"第一个任务里，永远在任务上下文运行
     * 所以这个函数正常永远不会返回
     * ============================================================ */
    prvStartFirstTask();

    /* Should not get here! */
    return 0;
}
/*-----------------------------------------------------------*/

void vPortEndScheduler( void )
{
    /* Not implemented in ports where there is nothing to return to.
     * Artificially force an assert. */
    configASSERT( uxCriticalNesting == 1000UL );
}
/*-----------------------------------------------------------*/

void vPortEnterCritical( void )
{
    portDISABLE_INTERRUPTS();
    uxCriticalNesting++;

    /* This is not the interrupt safe version of the enter critical function so
     * assert() if it is being called from an interrupt context.  Only API
     * functions that end in "FromISR" can be used in an interrupt.  Only assert if
     * the critical nesting count is 1 to protect against recursive calls if the
     * assert function also uses a critical section. */
    if( uxCriticalNesting == 1 )
    {
        configASSERT( ( portNVIC_INT_CTRL_REG & portVECTACTIVE_MASK ) == 0 );
    }
}
/*-----------------------------------------------------------*/

void vPortExitCritical( void )
{
    configASSERT( uxCriticalNesting );
    uxCriticalNesting--;

    if( uxCriticalNesting == 0 )
    {
        portENABLE_INTERRUPTS();
    }
}
/*-----------------------------------------------------------*/
// 
/**
 * @brief PendSV 中断服务函数，进行任务的切换
 *        __asm 是 C/C++ 编译器提供的内联汇编（inline assembly）声明符，
 *        用来在 C 代码里直接嵌入汇编指令
 * 
 * @return __asm 
 */
__asm void xPortPendSVHandler( void )
{
    // 只要触发 PendSV 中断，寄存器 xPSR、PC、R14、R12 和 R3 ~ R0，
	// 这 8 个寄存器的值会自动从 psp 指针指向的地址开始加载到任务栈中
	// 并且 psp 指针也会自动更新
    extern uxCriticalNesting;
    extern pxCurrentTCB;
    extern vTaskSwitchContext;

    /* *INDENT-OFF* */
    PRESERVE8
    // 将 psp 的值存储到寄存器 r0 中 
    mrs r0, psp
    isb
    // 将 pxCurrentTCB 的地址存储到寄存器 r3 中
    ldr r3, =pxCurrentTCB /* Get the location of the current TCB. */
    // 将寄存器 r3 存储的地址里的内容存储到寄存器 r2 中
	// 此时寄存器 r2 中存储了当前执行任务的 TCB 地址
    ldr r2, [ r3 ]
    // 以寄存器 r0 为基地址（r0 中现在存储了 psp 指针的值）
	// 将寄存器 r11 ~ r4 中存储的值存储到任务栈
	// 并且 r0 中存储的地址指针也会自动更新
    stmdb r0 !, { r4 - r11 } /* Save the remaining registers. */
    // 将寄存器 r0 中的内容存储到上一个执行任务的 TCB 地址
	// 也即上一个任务的栈顶指针 pxTopOfStack 指向寄存器 r0 现在存储的地址
    str r0, [ r2 ] /* Save the new top of stack into the first member of the TCB. */
    // 上文保存完成
	// 开始加载下文
	// 将寄存器 r3 和 r14 压入栈
    stmdb sp !, { r3, r14 }
    // 屏蔽 优先级值 高于或者等于 11 的中断
    mov r0, #configMAX_SYSCALL_INTERRUPT_PRIORITY
    msr basepri, r0
    dsb
    isb
    // 跳转到 vTaskSwitchContext 函数，得到下一个要执行任务的指针 pxCurrentTCB
    bl vTaskSwitchContext
    // 开中断
    mov r0, #0
    msr basepri, r0
    // 恢复寄存器 r3 和 r14
    ldmia sp !, { r3, r14 }
    // 将 pxCurrentTCB 加载到寄存器 r1 
    ldr r1, [ r3 ]
    // 将下一个要执行任务的栈顶指针加载到寄存器 r0 
    ldr r0, [ r1 ] /* The first item in pxCurrentTCB is the task top of stack. */
    // 以寄存器 r0 为基地址（r0 中现在存储了任务的栈顶指针）
	// 将任务栈中存储的值加载到寄存器 r4 ~ r11 中
	// 同时 r0 中的值会自动更新
    ldmia r0 !, { r4 - r11 } /* Pop the registers and the critical nesting count. */
    // 将更新后的 r0 的值加载到 psp
    msr psp, r0
    isb
    // 异常退出，自动加载剩下的 xPSR、PC、R14、R12 和 R3 ~ R0 寄存器
    bx r14
    nop
/* *INDENT-ON* */
}
/*-----------------------------------------------------------*/
// 阻塞延时，SysTick 中断服务函数
void xPortSysTickHandler( void )
{
    /* The SysTick runs at the lowest interrupt priority, so when this interrupt
     * executes all interrupts must be unmasked.  There is therefore no need to
     * save and then restore the interrupt mask value as its value is already
     * known - therefore the slightly faster vPortRaiseBASEPRI() function is used
     * in place of portSET_INTERRUPT_MASK_FROM_ISR(). */
    // 关中断
    vPortRaiseBASEPRI();
    traceISR_ENTER();
    {
        /* Increment the RTOS tick. */
        // 更新任务延时参数
        if( xTaskIncrementTick() != pdFALSE )
        {
            traceISR_EXIT_TO_SCHEDULER();

            /* A context switch is required.  Context switching is performed in
             * the PendSV interrupt.  Pend the PendSV interrupt. */
            portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT;
        }
        else
        {
            traceISR_EXIT();
        }
    }
    // 开中断
    vPortClearBASEPRIFromISR();
}
/*-----------------------------------------------------------*/

#if ( configUSE_TICKLESS_IDLE == 1 )

    __weak void vPortSuppressTicksAndSleep( TickType_t xExpectedIdleTime )
    {
        uint32_t ulReloadValue, ulCompleteTickPeriods, ulCompletedSysTickDecrements, ulSysTickDecrementsLeft;
        TickType_t xModifiableIdleTime;

        /* Make sure the SysTick reload value does not overflow the counter. */
        if( xExpectedIdleTime > xMaximumPossibleSuppressedTicks )
        {
            xExpectedIdleTime = xMaximumPossibleSuppressedTicks;
        }

        /* Enter a critical section but don't use the taskENTER_CRITICAL()
         * method as that will mask interrupts that should exit sleep mode. */
        __disable_irq();
        __dsb( portSY_FULL_READ_WRITE );
        __isb( portSY_FULL_READ_WRITE );

        /* If a context switch is pending or a task is waiting for the scheduler
         * to be unsuspended then abandon the low power entry. */
        if( eTaskConfirmSleepModeStatus() == eAbortSleep )
        {
            /* Re-enable interrupts - see comments above the __disable_irq()
             * call above. */
            __enable_irq();
        }
        else
        {
            /* Stop the SysTick momentarily.  The time the SysTick is stopped for
             * is accounted for as best it can be, but using the tickless mode will
             * inevitably result in some tiny drift of the time maintained by the
             * kernel with respect to calendar time. */
            portNVIC_SYSTICK_CTRL_REG = ( portNVIC_SYSTICK_CLK_BIT_CONFIG | portNVIC_SYSTICK_INT_BIT );

            /* Use the SysTick current-value register to determine the number of
             * SysTick decrements remaining until the next tick interrupt.  If the
             * current-value register is zero, then there are actually
             * ulTimerCountsForOneTick decrements remaining, not zero, because the
             * SysTick requests the interrupt when decrementing from 1 to 0. */
            ulSysTickDecrementsLeft = portNVIC_SYSTICK_CURRENT_VALUE_REG;

            if( ulSysTickDecrementsLeft == 0 )
            {
                ulSysTickDecrementsLeft = ulTimerCountsForOneTick;
            }

            /* Calculate the reload value required to wait xExpectedIdleTime
             * tick periods.  -1 is used because this code normally executes part
             * way through the first tick period.  But if the SysTick IRQ is now
             * pending, then clear the IRQ, suppressing the first tick, and correct
             * the reload value to reflect that the second tick period is already
             * underway.  The expected idle time is always at least two ticks. */
            ulReloadValue = ulSysTickDecrementsLeft + ( ulTimerCountsForOneTick * ( xExpectedIdleTime - 1UL ) );

            if( ( portNVIC_INT_CTRL_REG & portNVIC_PEND_SYSTICK_SET_BIT ) != 0 )
            {
                portNVIC_INT_CTRL_REG = portNVIC_PEND_SYSTICK_CLEAR_BIT;
                ulReloadValue -= ulTimerCountsForOneTick;
            }

            if( ulReloadValue > ulStoppedTimerCompensation )
            {
                ulReloadValue -= ulStoppedTimerCompensation;
            }

            /* Set the new reload value. */
            portNVIC_SYSTICK_LOAD_REG = ulReloadValue;

            /* Clear the SysTick count flag and set the count value back to
             * zero. */
            portNVIC_SYSTICK_CURRENT_VALUE_REG = 0UL;

            /* Restart SysTick. */
            portNVIC_SYSTICK_CTRL_REG |= portNVIC_SYSTICK_ENABLE_BIT;

            /* Sleep until something happens.  configPRE_SLEEP_PROCESSING() can
             * set its parameter to 0 to indicate that its implementation contains
             * its own wait for interrupt or wait for event instruction, and so wfi
             * should not be executed again.  However, the original expected idle
             * time variable must remain unmodified, so a copy is taken. */
            xModifiableIdleTime = xExpectedIdleTime;
            configPRE_SLEEP_PROCESSING( xModifiableIdleTime );

            if( xModifiableIdleTime > 0 )
            {
                __dsb( portSY_FULL_READ_WRITE );
                __wfi();
                __isb( portSY_FULL_READ_WRITE );
            }

            configPOST_SLEEP_PROCESSING( xExpectedIdleTime );

            /* Re-enable interrupts to allow the interrupt that brought the MCU
             * out of sleep mode to execute immediately.  See comments above
             * the __disable_irq() call above. */
            __enable_irq();
            __dsb( portSY_FULL_READ_WRITE );
            __isb( portSY_FULL_READ_WRITE );

            /* Disable interrupts again because the clock is about to be stopped
             * and interrupts that execute while the clock is stopped will increase
             * any slippage between the time maintained by the RTOS and calendar
             * time. */
            __disable_irq();
            __dsb( portSY_FULL_READ_WRITE );
            __isb( portSY_FULL_READ_WRITE );

            /* Disable the SysTick clock without reading the
             * portNVIC_SYSTICK_CTRL_REG register to ensure the
             * portNVIC_SYSTICK_COUNT_FLAG_BIT is not cleared if it is set.  Again,
             * the time the SysTick is stopped for is accounted for as best it can
             * be, but using the tickless mode will inevitably result in some tiny
             * drift of the time maintained by the kernel with respect to calendar
             * time*/
            portNVIC_SYSTICK_CTRL_REG = ( portNVIC_SYSTICK_CLK_BIT_CONFIG | portNVIC_SYSTICK_INT_BIT );

            /* Determine whether the SysTick has already counted to zero. */
            if( ( portNVIC_SYSTICK_CTRL_REG & portNVIC_SYSTICK_COUNT_FLAG_BIT ) != 0 )
            {
                uint32_t ulCalculatedLoadValue;

                /* The tick interrupt ended the sleep (or is now pending), and
                 * a new tick period has started.  Reset portNVIC_SYSTICK_LOAD_REG
                 * with whatever remains of the new tick period. */
                ulCalculatedLoadValue = ( ulTimerCountsForOneTick - 1UL ) - ( ulReloadValue - portNVIC_SYSTICK_CURRENT_VALUE_REG );

                /* Don't allow a tiny value, or values that have somehow
                 * underflowed because the post sleep hook did something
                 * that took too long or because the SysTick current-value register
                 * is zero. */
                if( ( ulCalculatedLoadValue <= ulStoppedTimerCompensation ) || ( ulCalculatedLoadValue > ulTimerCountsForOneTick ) )
                {
                    ulCalculatedLoadValue = ( ulTimerCountsForOneTick - 1UL );
                }

                portNVIC_SYSTICK_LOAD_REG = ulCalculatedLoadValue;

                /* As the pending tick will be processed as soon as this
                 * function exits, the tick value maintained by the tick is stepped
                 * forward by one less than the time spent waiting. */
                ulCompleteTickPeriods = xExpectedIdleTime - 1UL;
            }
            else
            {
                /* Something other than the tick interrupt ended the sleep. */

                /* Use the SysTick current-value register to determine the
                 * number of SysTick decrements remaining until the expected idle
                 * time would have ended. */
                ulSysTickDecrementsLeft = portNVIC_SYSTICK_CURRENT_VALUE_REG;
                #if ( portNVIC_SYSTICK_CLK_BIT_CONFIG != portNVIC_SYSTICK_CLK_BIT )
                {
                    /* If the SysTick is not using the core clock, the current-
                     * value register might still be zero here.  In that case, the
                     * SysTick didn't load from the reload register, and there are
                     * ulReloadValue decrements remaining in the expected idle
                     * time, not zero. */
                    if( ulSysTickDecrementsLeft == 0 )
                    {
                        ulSysTickDecrementsLeft = ulReloadValue;
                    }
                }
                #endif /* portNVIC_SYSTICK_CLK_BIT_CONFIG */

                /* Work out how long the sleep lasted rounded to complete tick
                 * periods (not the ulReload value which accounted for part
                 * ticks). */
                ulCompletedSysTickDecrements = ( xExpectedIdleTime * ulTimerCountsForOneTick ) - ulSysTickDecrementsLeft;

                /* How many complete tick periods passed while the processor
                 * was waiting? */
                ulCompleteTickPeriods = ulCompletedSysTickDecrements / ulTimerCountsForOneTick;

                /* The reload value is set to whatever fraction of a single tick
                 * period remains. */
                portNVIC_SYSTICK_LOAD_REG = ( ( ulCompleteTickPeriods + 1UL ) * ulTimerCountsForOneTick ) - ulCompletedSysTickDecrements;
            }

            /* Restart SysTick so it runs from portNVIC_SYSTICK_LOAD_REG again,
             * then set portNVIC_SYSTICK_LOAD_REG back to its standard value.  If
             * the SysTick is not using the core clock, temporarily configure it to
             * use the core clock.  This configuration forces the SysTick to load
             * from portNVIC_SYSTICK_LOAD_REG immediately instead of at the next
             * cycle of the other clock.  Then portNVIC_SYSTICK_LOAD_REG is ready
             * to receive the standard value immediately. */
            portNVIC_SYSTICK_CURRENT_VALUE_REG = 0UL;
            portNVIC_SYSTICK_CTRL_REG = portNVIC_SYSTICK_CLK_BIT | portNVIC_SYSTICK_INT_BIT | portNVIC_SYSTICK_ENABLE_BIT;
            #if ( portNVIC_SYSTICK_CLK_BIT_CONFIG == portNVIC_SYSTICK_CLK_BIT )
            {
                portNVIC_SYSTICK_LOAD_REG = ulTimerCountsForOneTick - 1UL;
            }
            #else
            {
                /* The temporary usage of the core clock has served its purpose,
                 * as described above.  Resume usage of the other clock. */
                portNVIC_SYSTICK_CTRL_REG = portNVIC_SYSTICK_CLK_BIT | portNVIC_SYSTICK_INT_BIT;

                if( ( portNVIC_SYSTICK_CTRL_REG & portNVIC_SYSTICK_COUNT_FLAG_BIT ) != 0 )
                {
                    /* The partial tick period already ended.  Be sure the SysTick
                     * counts it only once. */
                    portNVIC_SYSTICK_CURRENT_VALUE_REG = 0;
                }

                portNVIC_SYSTICK_LOAD_REG = ulTimerCountsForOneTick - 1UL;
                portNVIC_SYSTICK_CTRL_REG = portNVIC_SYSTICK_CLK_BIT_CONFIG | portNVIC_SYSTICK_INT_BIT | portNVIC_SYSTICK_ENABLE_BIT;
            }
            #endif /* portNVIC_SYSTICK_CLK_BIT_CONFIG */

            /* Step the tick to account for any tick periods that elapsed. */
            vTaskStepTick( ulCompleteTickPeriods );

            /* Exit with interrupts enabled. */
            __enable_irq();
        }
    }

#endif /* #if configUSE_TICKLESS_IDLE */

/*-----------------------------------------------------------*/

/*
 * Setup the SysTick timer to generate the tick interrupts at the required
 * frequency.
 */
#if ( configOVERRIDE_DEFAULT_TICK_CONFIGURATION == 0 )
    /* __weak 表示这是"弱符号"：如果用户在别处定义了一个同名强符号函数，
    * 链接时会用用户的版本覆盖它。方便用户替换成自定义时基（比如用 TIM 代替 SysTick） */
    __weak void vPortSetupTimerInterrupt( void )
    {
        /* Calculate the constants required to configure the tick interrupt. */
        /* ============================================================
        * 第零部分：为 tickless idle（无节拍低功耗）预计算常量
        * 只有开启 configUSE_TICKLESS_IDLE 时才需要
        * ============================================================ */
        #if ( configUSE_TICKLESS_IDLE == 1 )
        {
            /* 一个 tick 对应多少个 SysTick 计数：
            * configSYSTICK_CLOCK_HZ = SysTick 时钟频率（默认等于 CPU 频率）
            * configTICK_RATE_HZ     = 系统节拍频率（如 1000Hz）
            * 例：72MHz / 1000 = 72000 个计数 = 1 个 tick */
            ulTimerCountsForOneTick = ( configSYSTICK_CLOCK_HZ / configTICK_RATE_HZ );
            /* 一次最多能"偷睡"多少个 tick：
            * SysTick 是 24 位计数器，最大值 0xFFFFFF（portMAX_24_BIT_NUMBER）
            * 用它除以"一个 tick 的计数"，得到单次最大可屏蔽的 tick 数
            * 超过这个数就需要分段补偿，所以记录上限 */
            xMaximumPossibleSuppressedTicks = portMAX_24_BIT_NUMBER / ulTimerCountsForOneTick;
            /* 停止/重启定时器的补偿系数：
            * 从低功耗模式恢复后，读到的计数可能有偏差（漏计），
            * 这项预计算用于后续补偿这些"漏掉"的计数 */
            ulStoppedTimerCompensation = portMISSED_COUNTS_FACTOR / ( configCPU_CLOCK_HZ / configSYSTICK_CLOCK_HZ );
        }
        #endif /* configUSE_TICKLESS_IDLE */

         /* ============================================================
        * 第一部分：先停掉并清空 SysTick
        * 改配置前必须使能位清零，否则新值可能不生效
        * ============================================================ */

        /* Stop and clear the SysTick. */
         /* 写 0 到 SysTick 控制寄存器：关使能、关中断、清时钟源选择
        * 相当于"先刹车" */
        portNVIC_SYSTICK_CTRL_REG = 0UL;
        /* 清当前计数值寄存器（VAL）：
        * 写任何值都会清零它，并同时清掉 COUNTFLAG 标志
        * 避免残留的旧计数值影响新的重载设置 */
        portNVIC_SYSTICK_CURRENT_VALUE_REG = 0UL;

        /* ============================================================
        * 第二部分：配置 SysTick 按目标频率中断
        * ============================================================ */

        /* Configure SysTick to interrupt at the requested rate. */
        /* 设重载值（LOAD）：
        * SysTick 是从 LOAD 递减到 0 时产生一次中断，
        * 所以计数次数 = (时钟频率 / 目标节拍频率)，
        * 但"从 LOAD 减到 0"共 LOAD+1 次，故要减 1
        * 例：72MHz/1000Hz = 72000 次 → LOAD = 71999 */
        portNVIC_SYSTICK_LOAD_REG = ( configSYSTICK_CLOCK_HZ / configTICK_RATE_HZ ) - 1UL;
        /* 设控制寄存器（CTRL），三个位或起来一次性写入：
        * - portNVIC_SYSTICK_CLK_BIT_CONFIG：选择时钟源
        *     （=0 用 SysTick 外部时钟，=1 用内核时钟/不分频，由宏决定）
        * - portNVIC_SYSTICK_INT_BIT：使能倒计时到 0 时产生 SysTick 异常
        * - portNVIC_SYSTICK_ENABLE_BIT：启动计数器
        * 这一步做完，SysTick 就开始跑了；但因为当前全局中断处于关闭状态
        * （调用者是 xPortStartScheduler，进来时中断是关的），
        * 要等 prvStartFirstTask() 里 SVC 返回才真正开启中断响应 */
        portNVIC_SYSTICK_CTRL_REG = ( portNVIC_SYSTICK_CLK_BIT_CONFIG | portNVIC_SYSTICK_INT_BIT | portNVIC_SYSTICK_ENABLE_BIT );
    }

#endif /* configOVERRIDE_DEFAULT_TICK_CONFIGURATION */
/*-----------------------------------------------------------*/

__asm uint32_t vPortGetIPSR( void )
{
/* *INDENT-OFF* */
    PRESERVE8

    mrs r0, ipsr
    bx r14
/* *INDENT-ON* */
}
/*-----------------------------------------------------------*/

#if ( configASSERT_DEFINED == 1 )

    void vPortValidateInterruptPriority( void )
    {
        uint32_t ulCurrentInterrupt;
        uint8_t ucCurrentPriority;

        /* Obtain the number of the currently executing interrupt. */
        ulCurrentInterrupt = vPortGetIPSR();

        /* Is the interrupt number a user defined interrupt? */
        if( ulCurrentInterrupt >= portFIRST_USER_INTERRUPT_NUMBER )
        {
            /* Look up the interrupt's priority. */
            ucCurrentPriority = pcInterruptPriorityRegisters[ ulCurrentInterrupt ];

            /* The following assertion will fail if a service routine (ISR) for
             * an interrupt that has been assigned a priority above
             * configMAX_SYSCALL_INTERRUPT_PRIORITY calls an ISR safe FreeRTOS API
             * function.  ISR safe FreeRTOS API functions must *only* be called
             * from interrupts that have been assigned a priority at or below
             * configMAX_SYSCALL_INTERRUPT_PRIORITY.
             *
             * Numerically low interrupt priority numbers represent logically high
             * interrupt priorities, therefore the priority of the interrupt must
             * be set to a value equal to or numerically *higher* than
             * configMAX_SYSCALL_INTERRUPT_PRIORITY.
             *
             * Interrupts that  use the FreeRTOS API must not be left at their
             * default priority of  zero as that is the highest possible priority,
             * which is guaranteed to be above configMAX_SYSCALL_INTERRUPT_PRIORITY,
             * and  therefore also guaranteed to be invalid.
             *
             * FreeRTOS maintains separate thread and ISR API functions to ensure
             * interrupt entry is as fast and simple as possible.
             *
             * The following links provide detailed information:
             * https://www.FreeRTOS.org/RTOS-Cortex-M3-M4.html
             * https://www.freertos.org/Why-FreeRTOS/FAQs */
            configASSERT( ucCurrentPriority >= ucMaxSysCallPriority );
        }

        /* Priority grouping:  The interrupt controller (NVIC) allows the bits
         * that define each interrupt's priority to be split between bits that
         * define the interrupt's pre-emption priority bits and bits that define
         * the interrupt's sub-priority.  For simplicity all bits must be defined
         * to be pre-emption priority bits.  The following assertion will fail if
         * this is not the case (if some bits represent a sub-priority).
         *
         * If the application only uses CMSIS libraries for interrupt
         * configuration then the correct setting can be achieved on all Cortex-M
         * devices by calling NVIC_SetPriorityGrouping( 0 ); before starting the
         * scheduler.  Note however that some vendor specific peripheral libraries
         * assume a non-zero priority group setting, in which cases using a value
         * of zero will result in unpredictable behaviour. */
        configASSERT( ( portAIRCR_REG & portPRIORITY_GROUP_MASK ) <= ulMaxPRIGROUPValue );
    }

#endif /* configASSERT_DEFINED */
