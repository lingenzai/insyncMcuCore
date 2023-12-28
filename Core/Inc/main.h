/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
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
#include "stm32l0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "simpledef.h"
#include "accel.h"
#include <pulse.h>
#include <mcu.h>
#include <ee.h>
#include <ble.h>
#include "protocol.h"
#include "adc.h"
#include "ecg.h"
#include "wpr.h"
#include "flash.h"
#include "ovbc.h"


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

extern void main_adcConfigAllCh(void);

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define CCM_PIN10_WKUP_INTR_Pin GPIO_PIN_0
#define CCM_PIN10_WKUP_INTR_GPIO_Port GPIOA
#define CCM_PIN12_RA_IEGM_Pin GPIO_PIN_2
#define CCM_PIN12_RA_IEGM_GPIO_Port GPIOA
#define CCM_PIN13_RS_RDET_Pin GPIO_PIN_3
#define CCM_PIN13_RS_RDET_GPIO_Port GPIOA
#define CCM_PIN14_RS_IEGM_Pin GPIO_PIN_4
#define CCM_PIN14_RS_IEGM_GPIO_Port GPIOA
#define CCM_PIN15_RV_RDET_Pin GPIO_PIN_5
#define CCM_PIN15_RV_RDET_GPIO_Port GPIOA
#define CCM_PIN16_RV_IEGM_Pin GPIO_PIN_6
#define CCM_PIN16_RV_IEGM_GPIO_Port GPIOA
#define CCM_PIN17_BATT_M_Pin GPIO_PIN_7
#define CCM_PIN17_BATT_M_GPIO_Port GPIOA
#define CCM_PIN19_WPR_INT_Pin GPIO_PIN_1
#define CCM_PIN19_WPR_INT_GPIO_Port GPIOB
#define CCM_PIN19_WPR_INT_EXTI_IRQn EXTI0_1_IRQn
#define CCM_PIN20_RSL10_WKUP_Pin GPIO_PIN_2
#define CCM_PIN20_RSL10_WKUP_GPIO_Port GPIOB
#define CCM_PIN21_BOOST_ON_Pin GPIO_PIN_10
#define CCM_PIN21_BOOST_ON_GPIO_Port GPIOB
#define CCM_PIN22_ACCEL_INT_Pin GPIO_PIN_11
#define CCM_PIN22_ACCEL_INT_GPIO_Port GPIOB
#define CCM_PIN22_ACCEL_INT_EXTI_IRQn EXTI4_15_IRQn
#define CCM_PIN25_MEM_CS_Pin GPIO_PIN_12
#define CCM_PIN25_MEM_CS_GPIO_Port GPIOB
#define CCM_PIN26_SP1_CLK_Pin GPIO_PIN_13
#define CCM_PIN26_SP1_CLK_GPIO_Port GPIOB
#define CCM_PIN32_VPOS_EN_Pin GPIO_PIN_11
#define CCM_PIN32_VPOS_EN_GPIO_Port GPIOA
#define CCM_PIN33_VNEG_EN_Pin GPIO_PIN_12
#define CCM_PIN33_VNEG_EN_GPIO_Port GPIOA
#define CCM_PIN38_BLE_CS_Pin GPIO_PIN_15
#define CCM_PIN38_BLE_CS_GPIO_Port GPIOA
#define CCM_PIN42_I2C1_SCK_Pin GPIO_PIN_6
#define CCM_PIN42_I2C1_SCK_GPIO_Port GPIOB
#define CCM_PIN43_I2C1_SDA_Pin GPIO_PIN_7
#define CCM_PIN43_I2C1_SDA_GPIO_Port GPIOB
#define CCM_PIN45_VOUT_SET_Pin GPIO_PIN_8
#define CCM_PIN45_VOUT_SET_GPIO_Port GPIOB
#define CCM_PIN46_VPON_Pin GPIO_PIN_9
#define CCM_PIN46_VPON_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
