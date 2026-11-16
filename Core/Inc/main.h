/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define OK_Pin GPIO_PIN_14
#define OK_GPIO_Port GPIOC
#define OK_EXTI_IRQn EXTI15_10_IRQn
#define BACK_Pin GPIO_PIN_13
#define BACK_GPIO_Port GPIOC
#define BACK_EXTI_IRQn EXTI15_10_IRQn
#define HAL2_OPEN_Pin GPIO_PIN_0
#define HAL2_OPEN_GPIO_Port GPIOC
#define HAL2_CLOSE_Pin GPIO_PIN_1
#define HAL2_CLOSE_GPIO_Port GPIOC
#define STCP_Pin GPIO_PIN_0
#define STCP_GPIO_Port GPIOA
#define Flame_Detector_Pin GPIO_PIN_1
#define Flame_Detector_GPIO_Port GPIOA
#define ME2sig_Pin GPIO_PIN_2
#define ME2sig_GPIO_Port GPIOA
#define ME2diff_Pin GPIO_PIN_3
#define ME2diff_GPIO_Port GPIOA
#define ME2ntc_Pin GPIO_PIN_4
#define ME2ntc_GPIO_Port GPIOA
#define ntc_mixer_tmp_Pin GPIO_PIN_5
#define ntc_mixer_tmp_GPIO_Port GPIOA
#define HAL1_CLOSE_Pin GPIO_PIN_2
#define HAL1_CLOSE_GPIO_Port GPIOB
#define HAL1_OPEN_Pin GPIO_PIN_12
#define HAL1_OPEN_GPIO_Port GPIOB
#define buzzer_Pin GPIO_PIN_13
#define buzzer_GPIO_Port GPIOB
#define FLT2_Pin GPIO_PIN_14
#define FLT2_GPIO_Port GPIOB
#define FLT1_Pin GPIO_PIN_15
#define FLT1_GPIO_Port GPIOB
#define ENSTEP2_Pin GPIO_PIN_6
#define ENSTEP2_GPIO_Port GPIOC
#define ENSTEP1_Pin GPIO_PIN_7
#define ENSTEP1_GPIO_Port GPIOC
#define STEP_Pin GPIO_PIN_8
#define STEP_GPIO_Port GPIOC
#define DIR_Pin GPIO_PIN_9
#define DIR_GPIO_Port GPIOC
#define fan_ehtragh_Pin GPIO_PIN_8
#define fan_ehtragh_GPIO_Port GPIOA
#define zc_Pin GPIO_PIN_9
#define zc_GPIO_Port GPIOA
#define CE_Pin GPIO_PIN_11
#define CE_GPIO_Port GPIOA
#define CSN_Pin GPIO_PIN_15
#define CSN_GPIO_Port GPIOA
#define RESET_Pin GPIO_PIN_10
#define RESET_GPIO_Port GPIOC
#define DC_Pin GPIO_PIN_12
#define DC_GPIO_Port GPIOC
#define CS_Pin GPIO_PIN_2
#define CS_GPIO_Port GPIOD
#define UP_Pin GPIO_PIN_8
#define UP_GPIO_Port GPIOB
#define UP_EXTI_IRQn EXTI9_5_IRQn
#define DOWN_Pin GPIO_PIN_9
#define DOWN_GPIO_Port GPIOB
#define DOWN_EXTI_IRQn EXTI9_5_IRQn

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
