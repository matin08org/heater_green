#if defined(FW_CUSTOM_HMI)
#include "main.h"
#include "HmiMonitorFault.h"
volatile HmiMonitorCrash hmi_monitor_crash __attribute__((section(".noinit")));
__attribute__((used,noreturn)) void HmiMonitorFault_Record(uint32_t *frame,uint32_t exception_return)
{
    __disable_irq();
    hmi_monitor_crash.magic=0;
    hmi_monitor_crash.exception=__get_IPSR();hmi_monitor_crash.sp=(uint32_t)(uintptr_t)frame;
    hmi_monitor_crash.exception_return=exception_return;
    hmi_monitor_crash.pc=hmi_monitor_crash.lr=hmi_monitor_crash.xpsr=0;
    uintptr_t address=(uintptr_t)frame;
    if((exception_return&0x10U) && address>=0x20000000U && address<=0x20040000U-8U*sizeof(uint32_t) && !(address&3U)) {
        hmi_monitor_crash.lr=frame[5];hmi_monitor_crash.pc=frame[6];hmi_monitor_crash.xpsr=frame[7];
    }
    hmi_monitor_crash.cfsr=SCB->CFSR;hmi_monitor_crash.hfsr=SCB->HFSR;
    hmi_monitor_crash.bfar=SCB->BFAR;hmi_monitor_crash.mmfar=SCB->MMFAR;
    __DMB();hmi_monitor_crash.magic=0x484D4946U;__DSB();
    while(1) {__WFI();}
}
#define FAULT_HANDLER(name) __attribute__((naked)) void name(void) { \
    __asm volatile("mov r1, lr\n" "tst lr, #4\n" "ite eq\n" "mrseq r0, msp\n" "mrsne r0, psp\n" \
                   "b HmiMonitorFault_Record\n"); }
FAULT_HANDLER(HardFault_Handler)
FAULT_HANDLER(MemManage_Handler)
FAULT_HANDLER(BusFault_Handler)
FAULT_HANDLER(UsageFault_Handler)
FAULT_HANDLER(NMI_Handler)
#endif
