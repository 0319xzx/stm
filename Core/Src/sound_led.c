/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    sound_led.c
  * @brief   Sound-level LED indicator – PA5 (LD2) on/off per audio hop.
  *
  *  Computes the mean-absolute value (MAV) of each 10 ms PCM hop received
  *  from the SAI1A DMA pipeline and drives PA5 (Nucleo-L476RG LD2):
  *    MAV >= SOUND_LED_THRESHOLD  →  GPIO_PIN_SET   (LED on)
  *    MAV <  SOUND_LED_THRESHOLD  →  GPIO_PIN_RESET (LED off, immediately)
  *
  *  PA5 is initialised as a push-pull output in MX_GPIO_Init() (gpio.c).
  ******************************************************************************
  */
/* USER CODE END Header */

#include "sound_led.h"
#include "stm32l4xx_hal.h"

/* LED hardware mapping – PA5 = LD2 on Nucleo-L476RG */
#define SOUND_LED_GPIO_PORT   GPIOA
#define SOUND_LED_GPIO_PIN    GPIO_PIN_5

/**
 * @brief  Drive LD2 based on the mean-absolute amplitude of one audio hop.
 */
void Sound_LED_UpdateHop(const int16_t *hop, uint32_t n_samples)
{
    if (hop == NULL || n_samples == 0U)
    {
        return;
    }

    /* Compute mean-absolute value (MAV).
     * Casting to int32_t before negation avoids overflow for INT16_MIN (-32768). */
    uint32_t sum = 0U;
    for (uint32_t i = 0U; i < n_samples; i++)
    {
        int32_t s = (int32_t)hop[i];
        sum += (s < 0) ? (uint32_t)(-s) : (uint32_t)s;
    }
    uint32_t mav = sum / n_samples;

    /* Drive LED: on when sound exceeds threshold, off immediately otherwise */
    if (mav >= SOUND_LED_THRESHOLD)
    {
        HAL_GPIO_WritePin(SOUND_LED_GPIO_PORT, SOUND_LED_GPIO_PIN, GPIO_PIN_SET);
    }
    else
    {
        HAL_GPIO_WritePin(SOUND_LED_GPIO_PORT, SOUND_LED_GPIO_PIN, GPIO_PIN_RESET);
    }
}
