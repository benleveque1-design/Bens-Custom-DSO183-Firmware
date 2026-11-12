/* Startup code for the STM32F103C8-class custom firmware.
 *
 * The vector table mirrors the vendor firmware's layout (see
 * analysis/BINARY_LAYOUT.md) but with the handler names of this project.  The
 * vendor handlers we do not implement yet simply spin in a trap loop, so a mistake
 * is obvious instead of silent.
 */
  .syntax unified
  .cpu cortex-m3
  .thumb

/* Linker symbols used by the reset handler (kept out of the vector table). */
  .section .linker_symbols,"a",%progbits
  .word _sidata
  .word _sdata
  .word _edata
  .word _sbss
  .word _ebss

/* The vector table: initial stack pointer, then the exception entries. */
  .section .isr_vector,"a",%progbits
  .type g_pfnVectors, %object
  .global g_pfnVectors
g_pfnVectors:
  .word _estack
  .global Default_Handler

/* Core handlers */
.word Reset_Handler
.word NMI_Handler
.word HardFault_Handler
.word MemManage_Handler
.word BusFault_Handler
.word UsageFault_Handler
.word 0
.word 0
.word 0
.word 0
.word SVC_Handler
.word DebugMon_Handler
.word 0
.word PendSV_Handler
.word SysTick_Handler

/* External interrupts - only the ones the firmware uses */
.word WWDG_IRQHandler
.word PVD_IRQHandler
.word TAMPER_IRQHandler
.word RTC_IRQHandler
.word FLASH_IRQHandler
.word RCC_IRQHandler
.word EXTI0_IRQHandler
.word EXTI1_IRQHandler
.word EXTI2_IRQHandler
.word EXTI3_IRQHandler
.word EXTI4_IRQHandler
.word DMA1_Channel1_IRQHandler
.word DMA1_Channel2_IRQHandler
.word DMA1_Channel3_IRQHandler
.word DMA1_Channel4_IRQHandler
.word DMA1_Channel5_IRQHandler
.word DMA1_Channel6_IRQHandler
.word DMA1_Channel7_IRQHandler
.word ADC1_2_IRQHandler
.word USB_HP_CAN1_TX_IRQHandler
.word USB_LP_CAN1_RX0_IRQHandler
.word CAN1_RX1_IRQHandler
.word CAN1_SCE_IRQHandler
.word EXTI9_5_IRQHandler
.word TIM1_BRK_IRQHandler
.word TIM1_UP_IRQHandler
.word TIM1_TRG_COM_IRQHandler
.word TIM1_CC_IRQHandler
.word TIM2_IRQHandler
.word TIM3_IRQHandler
.word TIM4_IRQHandler
.word I2C1_EV_IRQHandler
.word I2C1_ER_IRQHandler
.word I2C2_EV_IRQHandler
.word I2C2_ER_IRQHandler
.word SPI1_IRQHandler
.word SPI2_IRQHandler
.word USART1_IRQHandler
.word USART2_IRQHandler
.word USART3_IRQHandler
.word EXTI15_10_IRQHandler
.word RTCAlarm_IRQHandler
.word USBWakeUp_IRQHandler

  .size g_pfnVectors, .-g_pfnVectors

  .section .text.Reset_Handler
  .weak Reset_Handler
  .type Reset_Handler, %function
Reset_Handler:
  ldr   sp, =_estack

/* copy .data from flash to RAM */
  movs  r1, #0
  b     LoopCopyDataInit
CopyDataInit:
  ldr   r3, =_sidata
  ldr   r3, [r3, r1]
  str   r3, [r0, r1]
  adds  r1, r1, #4
LoopCopyDataInit:
  ldr   r0, =_sdata
  ldr   r3, =_edata
  adds  r2, r0, r1
  cmp   r2, r3
  bcc   CopyDataInit

/* zero .bss */
  ldr   r2, =_sbss
  b     LoopFillZerobss
FillZerobss:
  movs  r3, #0
  str   r3, [r2], #4
LoopFillZerobss:
  ldr   r3, = _ebss
  cmp   r2, r3
  bcc   FillZerobss

/* CMSIS system init: resets the RCC to its reset state, and - importantly -
 * sets SCB->VTOR to the flash base and SystemCoreClock to the actual running
 * clock.  The old startup skipped this and relied on BOOT0=0 aliasing flash at
 * 0x00000000 with VTOR still 0. */
  bl    SystemInit

  bl    main
  bx    lr
.size Reset_Handler, .-Reset_Handler

  .section .text.Default_Handler,"ax",%progbits
Default_Handler:
Infinite_Loop:
  b     Infinite_Loop
  .size Default_Handler, .-Default_Handler

/* Every not-yet-implemented handler points at the default trap */
  .section .text.NMI_Handler,"ax",%progbits
  .weak NMI_Handler
  .thumb_set NMI_Handler,Default_Handler
  .section .text.HardFault_Handler,"ax",%progbits
  .weak HardFault_Handler
  .thumb_set HardFault_Handler,Default_Handler
  .section .text.MemManage_Handler,"ax",%progbits
  .weak MemManage_Handler
  .thumb_set MemManage_Handler,Default_Handler
  .section .text.BusFault_Handler,"ax",%progbits
  .weak BusFault_Handler
  .thumb_set BusFault_Handler,Default_Handler
  .section .text.UsageFault_Handler,"ax",%progbits
  .weak UsageFault_Handler
  .thumb_set UsageFault_Handler,Default_Handler
  .section .text.SVC_Handler,"ax",%progbits
  .weak SVC_Handler
  .thumb_set SVC_Handler,Default_Handler
  .section .text.DebugMon_Handler,"ax",%progbits
  .weak DebugMon_Handler
  .thumb_set DebugMon_Handler,Default_Handler
  .section .text.PendSV_Handler,"ax",%progbits
  .weak PendSV_Handler
  .thumb_set PendSV_Handler,Default_Handler
  .section .text.SysTick_Handler,"ax",%progbits
  .weak SysTick_Handler
  .thumb_set SysTick_Handler,Default_Handler
  .section .text.WWDG_IRQHandler,"ax",%progbits
  .weak WWDG_IRQHandler
  .thumb_set WWDG_IRQHandler,Default_Handler
  .section .text.PVD_IRQHandler,"ax",%progbits
  .weak PVD_IRQHandler
  .thumb_set PVD_IRQHandler,Default_Handler
  .section .text.TAMPER_IRQHandler,"ax",%progbits
  .weak TAMPER_IRQHandler
  .thumb_set TAMPER_IRQHandler,Default_Handler
  .section .text.RTC_IRQHandler,"ax",%progbits
  .weak RTC_IRQHandler
  .thumb_set RTC_IRQHandler,Default_Handler
  .section .text.FLASH_IRQHandler,"ax",%progbits
  .weak FLASH_IRQHandler
  .thumb_set FLASH_IRQHandler,Default_Handler
  .section .text.RCC_IRQHandler,"ax",%progbits
  .weak RCC_IRQHandler
  .thumb_set RCC_IRQHandler,Default_Handler
  .section .text.EXTI0_IRQHandler,"ax",%progbits
  .weak EXTI0_IRQHandler
  .thumb_set EXTI0_IRQHandler,Default_Handler
  .section .text.EXTI1_IRQHandler,"ax",%progbits
  .weak EXTI1_IRQHandler
  .thumb_set EXTI1_IRQHandler,Default_Handler
  .section .text.EXTI2_IRQHandler,"ax",%progbits
  .weak EXTI2_IRQHandler
  .thumb_set EXTI2_IRQHandler,Default_Handler
  .section .text.EXTI3_IRQHandler,"ax",%progbits
  .weak EXTI3_IRQHandler
  .thumb_set EXTI3_IRQHandler,Default_Handler
  .section .text.EXTI4_IRQHandler,"ax",%progbits
  .weak EXTI4_IRQHandler
  .thumb_set EXTI4_IRQHandler,Default_Handler
  .section .text.DMA1_Channel1_IRQHandler,"ax",%progbits
  .weak DMA1_Channel1_IRQHandler
  .thumb_set DMA1_Channel1_IRQHandler,Default_Handler
  .section .text.DMA1_Channel2_IRQHandler,"ax",%progbits
  .weak DMA1_Channel2_IRQHandler
  .thumb_set DMA1_Channel2_IRQHandler,Default_Handler
  .section .text.DMA1_Channel3_IRQHandler,"ax",%progbits
  .weak DMA1_Channel3_IRQHandler
  .thumb_set DMA1_Channel3_IRQHandler,Default_Handler
  .section .text.DMA1_Channel4_IRQHandler,"ax",%progbits
  .weak DMA1_Channel4_IRQHandler
  .thumb_set DMA1_Channel4_IRQHandler,Default_Handler
  .section .text.DMA1_Channel5_IRQHandler,"ax",%progbits
  .weak DMA1_Channel5_IRQHandler
  .thumb_set DMA1_Channel5_IRQHandler,Default_Handler
  .section .text.DMA1_Channel6_IRQHandler,"ax",%progbits
  .weak DMA1_Channel6_IRQHandler
  .thumb_set DMA1_Channel6_IRQHandler,Default_Handler
  .section .text.DMA1_Channel7_IRQHandler,"ax",%progbits
  .weak DMA1_Channel7_IRQHandler
  .thumb_set DMA1_Channel7_IRQHandler,Default_Handler
  .section .text.ADC1_2_IRQHandler,"ax",%progbits
  .weak ADC1_2_IRQHandler
  .thumb_set ADC1_2_IRQHandler,Default_Handler
  .section .text.USB_HP_CAN1_TX_IRQHandler,"ax",%progbits
  .weak USB_HP_CAN1_TX_IRQHandler
  .thumb_set USB_HP_CAN1_TX_IRQHandler,Default_Handler
  .section .text.USB_LP_CAN1_RX0_IRQHandler,"ax",%progbits
  .weak USB_LP_CAN1_RX0_IRQHandler
  .thumb_set USB_LP_CAN1_RX0_IRQHandler,Default_Handler
  .section .text.CAN1_RX1_IRQHandler,"ax",%progbits
  .weak CAN1_RX1_IRQHandler
  .thumb_set CAN1_RX1_IRQHandler,Default_Handler
  .section .text.CAN1_SCE_IRQHandler,"ax",%progbits
  .weak CAN1_SCE_IRQHandler
  .thumb_set CAN1_SCE_IRQHandler,Default_Handler
  .section .text.EXTI9_5_IRQHandler,"ax",%progbits
  .weak EXTI9_5_IRQHandler
  .thumb_set EXTI9_5_IRQHandler,Default_Handler
  .section .text.TIM1_BRK_IRQHandler,"ax",%progbits
  .weak TIM1_BRK_IRQHandler
  .thumb_set TIM1_BRK_IRQHandler,Default_Handler
  .section .text.TIM1_UP_IRQHandler,"ax",%progbits
  .weak TIM1_UP_IRQHandler
  .thumb_set TIM1_UP_IRQHandler,Default_Handler
  .section .text.TIM1_TRG_COM_IRQHandler,"ax",%progbits
  .weak TIM1_TRG_COM_IRQHandler
  .thumb_set TIM1_TRG_COM_IRQHandler,Default_Handler
  .section .text.TIM1_CC_IRQHandler,"ax",%progbits
  .weak TIM1_CC_IRQHandler
  .thumb_set TIM1_CC_IRQHandler,Default_Handler
  .section .text.TIM2_IRQHandler,"ax",%progbits
  .weak TIM2_IRQHandler
  .thumb_set TIM2_IRQHandler,Default_Handler
  .section .text.TIM3_IRQHandler,"ax",%progbits
  .weak TIM3_IRQHandler
  .thumb_set TIM3_IRQHandler,Default_Handler
  .section .text.TIM4_IRQHandler,"ax",%progbits
  .weak TIM4_IRQHandler
  .thumb_set TIM4_IRQHandler,Default_Handler
  .section .text.I2C1_EV_IRQHandler,"ax",%progbits
  .weak I2C1_EV_IRQHandler
  .thumb_set I2C1_EV_IRQHandler,Default_Handler
  .section .text.I2C1_ER_IRQHandler,"ax",%progbits
  .weak I2C1_ER_IRQHandler
  .thumb_set I2C1_ER_IRQHandler,Default_Handler
  .section .text.I2C2_EV_IRQHandler,"ax",%progbits
  .weak I2C2_EV_IRQHandler
  .thumb_set I2C2_EV_IRQHandler,Default_Handler
  .section .text.I2C2_ER_IRQHandler,"ax",%progbits
  .weak I2C2_ER_IRQHandler
  .thumb_set I2C2_ER_IRQHandler,Default_Handler
  .section .text.SPI1_IRQHandler,"ax",%progbits
  .weak SPI1_IRQHandler
  .thumb_set SPI1_IRQHandler,Default_Handler
  .section .text.SPI2_IRQHandler,"ax",%progbits
  .weak SPI2_IRQHandler
  .thumb_set SPI2_IRQHandler,Default_Handler
  .section .text.USART1_IRQHandler,"ax",%progbits
  .weak USART1_IRQHandler
  .thumb_set USART1_IRQHandler,Default_Handler
  .section .text.USART2_IRQHandler,"ax",%progbits
  .weak USART2_IRQHandler
  .thumb_set USART2_IRQHandler,Default_Handler
  .section .text.USART3_IRQHandler,"ax",%progbits
  .weak USART3_IRQHandler
  .thumb_set USART3_IRQHandler,Default_Handler
  .section .text.EXTI15_10_IRQHandler,"ax",%progbits
  .weak EXTI15_10_IRQHandler
  .thumb_set EXTI15_10_IRQHandler,Default_Handler
  .section .text.RTCAlarm_IRQHandler,"ax",%progbits
  .weak RTCAlarm_IRQHandler
  .thumb_set RTCAlarm_IRQHandler,Default_Handler
  .section .text.USBWakeUp_IRQHandler,"ax",%progbits
  .weak USBWakeUp_IRQHandler
  .thumb_set USBWakeUp_IRQHandler,Default_Handler
