#ifndef BOARD_ACTUATION_H
#define BOARD_ACTUATION_H
#include "BurnerControl.h"
#include "main.h"
typedef struct {
    bool fan_calibrated;
    uint16_t fan_ccr[11]; /* measured 0,10,...100% speed; OFF=1000 */
    uint32_t travel_pulses,travel_timeout_ms;
    uint16_t hall_debounce_ms;
    uint16_t endpoint_tolerance_pulses;
    float rpm;
} BoardActuationConfig;
extern const BoardActuationConfig board_actuation_config;
typedef struct {
    bool referenced,moving,bypass_proven,process_proven,fan_sync,fan_calibrated,fault,inhibited;
    BurnerDamper destination;
    uint16_t fan_ccr;
    uint32_t moves,errors;
} BoardActuationSnapshot;
void BoardActuation_Init(void);
void BoardActuation_Service(uint32_t now);
void BoardActuation_Apply(const BurnerOutputs *out,bool permitted,uint32_t now);
/* Dummy-load bench path only. Caller owns explicit arming/timeout. Does not
 * claim RPM calibration; real Hall, sync and motion fault checks still apply. */
void BoardActuation_ApplyOutputTest(const BurnerOutputs *out,bool armed,uint16_t compare,uint32_t now);
void BoardActuation_Get(BoardActuationSnapshot *out);
/* Operator must physically identify EXHAUST; a LOW Hall alone cannot identify it. */
bool BoardActuation_ConfirmExhaust(void);
HAL_StatusTypeDef BoardActuation_ManualStep(int32_t delta);
void BoardActuation_Abort(void);
#endif
