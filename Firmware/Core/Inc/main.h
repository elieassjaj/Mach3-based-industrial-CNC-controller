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
#define E_STOP_Pin GPIO_PIN_2
#define E_STOP_GPIO_Port GPIOE
#define E_STOP_EXTI_IRQn EXTI2_IRQn
#define PHY_NRST_Pin GPIO_PIN_0
#define PHY_NRST_GPIO_Port GPIOB
#define LED_ERR_Pin GPIO_PIN_1
#define LED_ERR_GPIO_Port GPIOB
#define LED_RUN_Pin GPIO_PIN_2
#define LED_RUN_GPIO_Port GPIOB
#define DIR_X_Pin GPIO_PIN_8
#define DIR_X_GPIO_Port GPIOD
#define DIR_Y_Pin GPIO_PIN_9
#define DIR_Y_GPIO_Port GPIOD
#define DIR_Z_Pin GPIO_PIN_10
#define DIR_Z_GPIO_Port GPIOD
#define DIR_A_Pin GPIO_PIN_11
#define DIR_A_GPIO_Port GPIOD
#define DIR_B_Pin GPIO_PIN_12
#define DIR_B_GPIO_Port GPIOD
#define MOTOR_EN_Pin GPIO_PIN_15
#define MOTOR_EN_GPIO_Port GPIOD
#define STEP_X_Pin GPIO_PIN_8
#define STEP_X_GPIO_Port GPIOA
#define STEP_Y_Pin GPIO_PIN_9
#define STEP_Y_GPIO_Port GPIOA
#define STEP_Z_Pin GPIO_PIN_10
#define STEP_Z_GPIO_Port GPIOA
#define STEP_A_Pin GPIO_PIN_11
#define STEP_A_GPIO_Port GPIOA
#define STEP_B_Pin GPIO_PIN_12
#define STEP_B_GPIO_Port GPIOA
#define RELAY1_Pin GPIO_PIN_8
#define RELAY1_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
