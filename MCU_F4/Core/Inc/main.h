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
#define reserve_input_Pin GPIO_PIN_3
#define reserve_input_GPIO_Port GPIOE
#define SHOCK_Pin GPIO_PIN_4
#define SHOCK_GPIO_Port GPIOE
#define DOOR_Pin GPIO_PIN_5
#define DOOR_GPIO_Port GPIOE
#define ACC_Pin GPIO_PIN_6
#define ACC_GPIO_Port GPIOE
#define ENGINE_CUTOFF_Pin GPIO_PIN_0
#define ENGINE_CUTOFF_GPIO_Port GPIOC
#define ALARM_Pin GPIO_PIN_2
#define ALARM_GPIO_Port GPIOC
#define reserve_output_Pin GPIO_PIN_3
#define reserve_output_GPIO_Port GPIOC
#define KEY_ON_Pin GPIO_PIN_3
#define KEY_ON_GPIO_Port GPIOA
#define KEY_ON_EXTI_IRQn EXTI3_IRQn
#define BUZZER_Pin GPIO_PIN_6
#define BUZZER_GPIO_Port GPIOA
#define LED_POWER_Pin GPIO_PIN_11
#define LED_POWER_GPIO_Port GPIOD
#define LED_SIM_Pin GPIO_PIN_12
#define LED_SIM_GPIO_Port GPIOD
#define LED_GPS_Pin GPIO_PIN_13
#define LED_GPS_GPIO_Port GPIOD
#define LED_GSM_Pin GPIO_PIN_14
#define LED_GSM_GPIO_Port GPIOD
#define EC200U_PWRKEY_Pin GPIO_PIN_3
#define EC200U_PWRKEY_GPIO_Port GPIOD
#define EC200U_NRST_3V3_Pin GPIO_PIN_4
#define EC200U_NRST_3V3_GPIO_Port GPIOD
#define EC200U_TX_Pin GPIO_PIN_5
#define EC200U_TX_GPIO_Port GPIOD
#define EC200U_RX_Pin GPIO_PIN_6
#define EC200U_RX_GPIO_Port GPIOD

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
