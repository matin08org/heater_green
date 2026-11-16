#ifndef COMBUSTION_CYCLE_H
#define COMBUSTION_CYCLE_H
#include "BurnerControl.h"
/* Pure supervisory model: requests only, never GPIO writes. */
BurnerFault CombustionCycle_Process(BurnerControl *burner,const BurnerInputs *inputs,uint32_t now);
#endif
