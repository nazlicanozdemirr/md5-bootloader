/**
  ******************************************************************************
  * @file    main.c
  * @brief   Uygulama: Yanıp sönen LED (LD2/LD3 = PA5).
  *          Bootloader 0x08000000'de, uygulama 0x08008000'de (Sector 2).
  *          VECT_TAB_OFFSET system_stm32f4xx.c içinde 0x8000 olmalı.
  ******************************************************************************
  */

#include "main.h"

static void SystemClock_Config(void);

#define LED_PORT    GPIOA
#define LED_PIN     GPIO_PIN_5
#define BLINK_MS    (100u)   /* Yanıp sönme süresi (ms) – hızlı */

/* SysTick saat değişince bozulabiliyor; görünür gecikme için basit döngü (yaklaşık 500ms) */
static void delay_ms_approx(uint32_t ms)
{
  volatile uint32_t count = 0u;
  const uint32_t total = ms * 400000u;  /* 180 MHz civarı ~500ms için */
  while (count < total)
    count++;
}

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  /* Saat değişti; SysTick'i yeni saate göre güncelle (HAL_Delay doğru çalışsın) */
  HAL_InitTick(0);

  /* LED pini PA5 çıkış */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin   = LED_PIN;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_PORT, &GPIO_InitStruct);

  /* Başlangıç: LED kapalı (Nucleo LD2 çoğunda LOW = yanar, önce HIGH veriyoruz) */
  HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);

  /* Yanıp sönen döngü */
  while (1)
  {
    HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
    delay_ms_approx(BLINK_MS);  /* HAL_Delay yerine; SysTick bozuksa da yanıp söner */
  }
}

static void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 180;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

