#ifndef HMI_MONITOR_FAULT_H
#define HMI_MONITOR_FAULT_H
#include <stdint.h>
typedef struct {
    uint32_t magic,exception,pc,lr,xpsr,sp,exception_return,cfsr,hfsr,bfar,mmfar;
} HmiMonitorCrash;
/* Best-effort fault record for debugger inspection, never a live terminal
 * guarantee after CPU fault. No printf, Flash write or display call in faults. */
extern volatile HmiMonitorCrash hmi_monitor_crash;
#endif
