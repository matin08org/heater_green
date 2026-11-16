#ifndef BOARD_RUNTIME_H
#define BOARD_RUNTIME_H
#include "BurnerApp.h"
#include "SensorMath.h"
extern const BurnerConfig board_burner_profile;
extern const char *const board_pending_items[];
extern const uint16_t board_pending_count;
void BoardRuntime_GetAnalog(BoardAnalogValues *values,uint32_t *sample_ms);
#endif
