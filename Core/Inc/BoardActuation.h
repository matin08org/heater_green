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
    bool referenced,moving,bypass_proven,process_proven,fan_sync,fan_calibrated,fault,inhibited,fan_tuning;
    BurnerDamper destination;
    uint16_t fan_ccr;
    uint32_t moves,errors,inhibit_flags,fan_period_us,fan_sync_age_ms;
} BoardActuationSnapshot;
enum {
    ACT_BLOCK_FAULT=1U<<0,ACT_BLOCK_PERMISSION=1U<<1,ACT_BLOCK_REFERENCE=1U<<2,
    ACT_BLOCK_SYNC=1U<<3,ACT_BLOCK_CALIBRATION=1U<<4,ACT_BLOCK_SERVICE=1U<<5
};
const char *BoardActuation_InhibitText(uint32_t flags);
void BoardActuation_Init(void);
void BoardActuation_Service(uint32_t now);
void BoardActuation_Apply(const BurnerOutputs *out,bool permitted,uint32_t now);
/* Dummy-load bench path only. Caller owns explicit arming/timeout. Does not
 * claim RPM calibration; real Hall, sync and motion fault checks still apply. */
void BoardActuation_ApplyOutputTest(const BurnerOutputs *out,bool armed,uint16_t compare,uint32_t now);
void BoardActuation_Get(BoardActuationSnapshot *out);
/* Operator physically identifies EXHAUST; manual confirmation accepts either
 * Hall level. Automatic travel still requires Hall arrival feedback. */
bool BoardActuation_ConfirmExhaust(void);
HAL_StatusTypeDef BoardActuation_ManualStep(int32_t delta);
void BoardActuation_Abort(void);
bool BoardActuation_StartRelayTest(bool idle,uint32_t now);
void BoardActuation_StopRelayTest(void);
bool BoardActuation_RelayTestActive(void);
bool BoardActuation_TuneFan(uint16_t compare,bool idle,uint32_t now);
void BoardActuation_StopFanTuning(void);
#endif
