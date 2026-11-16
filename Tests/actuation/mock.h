#ifndef ACTUATION_MOCK_H
#define ACTUATION_MOCK_H
#define __MAIN_H
#include <stdint.h>
typedef enum {HAL_OK,HAL_ERROR,HAL_BUSY,HAL_TIMEOUT} HAL_StatusTypeDef;
typedef enum {GPIO_PIN_RESET,GPIO_PIN_SET} GPIO_PinState;
typedef struct {uint32_t SR,CCR2,BDTR;} MockTimer;
extern MockTimer mock_timer;
#define TIM1 (&mock_timer)
#define TIM_BDTR_MOE (1U<<15)
#define TIM_SR_CC2IF (1U<<2)
#define TIM_SR_CC2OF (1U<<10)
#define HAL1_OPEN_GPIO_Port ((void*)1)
#define HAL1_OPEN_Pin 4096U
typedef struct {int unused;} TIM_HandleTypeDef;
typedef struct {int unused;} SPI_HandleTypeDef;
uint32_t HAL_GetTick(void);
GPIO_PinState HAL_GPIO_ReadPin(void *port,uint16_t pin);
#endif
