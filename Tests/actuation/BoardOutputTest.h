#ifndef BOARD_OUTPUT_TEST_H
#define BOARD_OUTPUT_TEST_H
#include "BoardActuation.h"
typedef struct {
 bool enabled,require_dummy_load_confirmation;
 uint16_t fan_on_ccr;
 uint32_t arm_timeout_ms,max_run_ms,max_total_ms;
} BoardOutputTestConfig;
extern const BoardOutputTestConfig board_output_test_config;
typedef enum {OUTPUT_TEST_NONE,OUTPUT_TEST_NOT_READY,OUTPUT_TEST_ARM_EXPIRED,
 OUTPUT_TEST_SYNC_LOST,OUTPUT_TEST_MOTION_FAULT,OUTPUT_TEST_SPI_ERROR,
 OUTPUT_TEST_TOTAL_LIMIT,OUTPUT_TEST_INVALID_REQUEST} BoardOutputTestReason;
typedef struct {
 bool armed,active,stop_requested,operator_confirmed_dummy_loads;
 BoardOutputTestReason reason;
 uint32_t elapsed_ms;
} BoardOutputTestSnapshot;
void BoardOutputTest_Init(void);
bool BoardOutputTest_Arm(bool operator_confirmed,uint32_t now);
void BoardOutputTest_Disarm(void);
/* Return true when caller must request STOP from the private scenario model. */
bool BoardOutputTest_Process(const BurnerOutputs *out,BurnerState state,uint32_t now);
void BoardOutputTest_Get(BoardOutputTestSnapshot *out);
#endif
