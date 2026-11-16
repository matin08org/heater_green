#ifndef DAMPER_STEPPER_H
#define DAMPER_STEPPER_H

#include "main.h"
#include <stdint.h>

#define DAMPER_STEPS_PER_REVOLUTION 800U

typedef enum
{
    DAMPER_END_STOP = 0,     /* Disable the PC6-connected driver at the endpoint. */
    DAMPER_END_REVERSE       /* Shuttle between Start() position and endpoint. */
} DamperStepper_EndAction_t;

/* Owns TIM3_CH3 (PC8 STEP), TIM2, PC9 DIR and PC6 EN (active LOW).
 * The other driver (PC7 EN) stays disabled. Position starts at zero; no homing.
 * Call once after SystemClock_Config() and MX_GPIO_Init(), while stopped. */
HAL_StatusTypeDef DamperStepper_Init(void);

/* Positive RPM; 800 STEP rising edges/revolution. Call only while idle.
 * Actual speed is rounded to the nearest representable timer period.
 * Unsupported speeds (including pulses shorter than 5 us) return HAL_ERROR. */
HAL_StatusTypeDef DamperStepper_SetSpeedRPM(float rpm);

/* Absolute position in pulses. Call only while idle. */
HAL_StatusTypeDef DamperStepper_SetEndpoint(int32_t position,
                                          DamperStepper_EndAction_t action);

/* Main-context call: enables motor, waits 2 ms for EN, selects DIR from
 * endpoint - current position, then starts hardware. No polling required. */
HAL_StatusTypeDef DamperStepper_Start(void);

/* Logical commanded position, sampled at completed pulses, not encoder feedback. */
int32_t DamperStepper_GetPosition(void);
uint8_t DamperStepper_IsRunning(void);
void DamperStepper_Stop(void); /* Abort, STEP LOW, disable PC6. Position needs homing after abort. */
void DamperStepper_Process(void); /* Nonblocking 2-ms driver enable setup. */

/* Called only by TIM2_IRQHandler on slave ARR overflow. */
void DamperStepper_EndpointIRQHandler(void);

#endif

