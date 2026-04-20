/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    audio_capture.c
  * @brief   SAI1A DMA circular audio capture from INMP441 microphone.
  *
  *  INMP441 I2S wiring (connected to SAI1A):
  *    WS  (Word Select / LRCLK) → PB9   (SAI1_FS_A)
  *    SCK (Bit Clock)           → PB10  (SAI1_SCK_A)
  *    SD  (Serial Data)         → PC3   (SAI1_SD_A)
  *    L/R → GND  (left channel, WS low)
  *    VDD → 3.3 V,  GND → GND
  *
  *  The INMP441 is an I2S slave that outputs 24-bit audio left-justified in a
  *  32-bit slot.  The STM32 SAI1A is configured as I2S Master Receive with
  *  DMA2 Channel 1 in circular mode.
  *
  *  Each received 32-bit DMA word contains:
  *    bits[31:8]  – 24-bit signed audio sample (MSB first)
  *    bits[7:0]   – zero-pad from INMP441
  *  We extract the upper 16 bits as an int16_t for feature extraction.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "audio_capture.h"
#include "sai.h"

/* DMA double-buffer (aligned to 4-byte boundary for DMA) */
__attribute__((aligned(4)))
uint32_t audio_dma_buf[DMA_BUF_SAMPLES];

/* Latest 10 ms hop in int16_t PCM */
int16_t audio_hop_buf[DMA_HALF_BUF_SAMPLES];

/* Flag: set by IRQ callback, cleared by consumer */
volatile uint8_t audio_hop_ready = 0;

/* -------------------------------------------------------------------------*/
/* Internal helper: convert one half of the DMA buffer to int16_t PCM       */
/* -------------------------------------------------------------------------*/
static void copy_half(const uint32_t *src)
{
    for (uint32_t i = 0; i < DMA_HALF_BUF_SAMPLES; i++)
    {
        /* Upper 16 bits of the 32-bit I2S word (24-bit left-justified) */
        audio_hop_buf[i] = (int16_t)((int32_t)src[i] >> 16);
    }
    audio_hop_ready = 1;
}

/* -------------------------------------------------------------------------*/
/* HAL SAI callbacks (override weak symbols from stm32l4xx_hal_sai.c)       */
/* -------------------------------------------------------------------------*/

/**
 * @brief  Called when the first half of the DMA buffer has been filled.
 */
void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai->Instance == SAI1_Block_A)
    {
        copy_half(&audio_dma_buf[0]);
    }
}

/**
 * @brief  Called when the second half of the DMA buffer has been filled
 *         (i.e., the full circular DMA cycle completes).
 */
void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai->Instance == SAI1_Block_A)
    {
        copy_half(&audio_dma_buf[DMA_HALF_BUF_SAMPLES]);
    }
}

/* -------------------------------------------------------------------------*/
/* Public API                                                                */
/* -------------------------------------------------------------------------*/

void Audio_Capture_Start(void)
{
    /* Start SAI1A circular DMA receive.
     * hsai_BlockA1 and hdma_sai1_a are initialised by MX_SAI1_Init().
     * The DMA is linked as hdmarx inside HAL_SAI_MspInit (sai.c).            */
    HAL_SAI_Receive_DMA(&hsai_BlockA1,
                        (uint8_t *)audio_dma_buf,
                        DMA_BUF_SAMPLES);
}

bool Audio_Capture_ConsumeHop(int16_t out[DMA_HALF_BUF_SAMPLES])
{
    if (!audio_hop_ready)
    {
        return false;
    }

    /* Copy atomically-safe: audio_hop_buf is written by ISR (copy_half),
     * but we only read it after the ready flag is set.                       */
    for (uint32_t i = 0; i < DMA_HALF_BUF_SAMPLES; i++)
    {
        out[i] = audio_hop_buf[i];
    }

    audio_hop_ready = 0;
    return true;
}
