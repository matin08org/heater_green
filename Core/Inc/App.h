#ifndef APP_H
#define APP_H

#include "main.h"
#include "MAX6675.h"
#include "DamperStepper.h"
#include "Buzzer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    APP_OK = 0U,
    APP_ERROR_HC595,
    APP_ERROR_MAX6675,
    APP_ERROR_DAMPER,
    APP_ERROR_BUZZER
} App_Status_t;

/* Call after GPIO, DMA, SPI2, TIM6 and TIM1 peripheral initialization. */
App_Status_t App_Init(void);

/* Call continuously in while(1). Services application modules and buzzer timing. */
void App_Process(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */

