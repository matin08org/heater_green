#ifndef BUZZER_H
#define BUZZER_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Passive buzzer on PB13 GPIO, toggled by a dedicated TIM4 half-period IRQ.
 * TIM1 is exclusively owned by PA8 phase PWM and PA9 hardware reset.
 */

/* Internal TIM4 counter runs at 1 MHz while playing. */
#define BUZZER_COUNTER_FREQUENCY_HZ      1000000UL
#define BUZZER_MIN_FREQUENCY_HZ          20UL
#define BUZZER_MAX_FREQUENCY_HZ          10000UL

/* duration_ms == 0 means continuous playback until Buzzer_Stop(). */

typedef enum
{
    BUZZER_OK = 0U,
    BUZZER_NOT_INITIALIZED,
    BUZZER_INVALID_ARGUMENT,
    BUZZER_TIMER_ERROR
} Buzzer_Status_t;

/* Call after GPIO/clock init. TIM4 must be unowned; PB13 stays GPIO output. */
Buzzer_Status_t Buzzer_Init(void);

/* Start/restart a passive buzzer at frequency_hz for duration_ms.
 * No delay or busy-wait is used. A duration of zero plays continuously. */
Buzzer_Status_t Buzzer_Play(uint32_t frequency_hz, uint32_t duration_ms);

/* Stop the buzzer immediately. */
Buzzer_Status_t Buzzer_Stop(void);

/* Call periodically from App_Process(); stops playback when duration expires. */
void Buzzer_Process(void);

/* Returns 1 while a tone is active, otherwise 0. */
uint8_t Buzzer_IsPlaying(void);
/* Dedicated TIM4_IRQHandler hook; no work when tone is stopped. */
void Buzzer_TimerIRQ(void);

#ifdef __cplusplus
}
#endif

#endif /* BUZZER_H */
