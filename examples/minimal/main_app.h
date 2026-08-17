/**
  ******************************************************************************
  * @file    main.h
  * @brief   Uygulama main header: LD3 pin tanımı.
  *          Vektör tablosu offset'i system_stm32f4xx.c içinde VECT_TAB_OFFSET (0x8000) ile ayarlanır.
  ******************************************************************************
  */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

/* LD3 pin tanımı (şu an PA5; kartına göre değiştirilebilir) */
#define LD3_Pin               GPIO_PIN_5
#define LD3_GPIO_Port         GPIOA

void Error_Handler(void);
void SystemClock_Config(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

