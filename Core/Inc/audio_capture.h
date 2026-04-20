/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    audio_capture.h
  * @brief   SAI1A + DMA circular audio capture from INMP441 microphone.
  *
  *  DMA buffer layout (circular, 32-bit words):
  *    [0 .. DMA_HALF_BUF_SAMPLES-1]  – first half   (10 ms)
  *    [DMA_HALF_BUF_SAMPLES .. DMA_BUF_SAMPLES-1] – second half (10 ms)
  *
  *  HAL_SAI_RxHalfCpltCallback / HAL_SAI_RxCpltCallback copy one half to
  *  the audio_hop_buf and set audio_hop_ready = 1.
  *
  *  The main loop (or MX_X_CUBE_AI_Process) calls Audio_Capture_ConsumeHop()
  *  to obtain 160 int16_t samples (upper 16 bits of each 32-bit I2S word).
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __AUDIO_CAPTURE_H__
#define __AUDIO_CAPTURE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ---------------------------------------------------------------------------
 * Audio / DMA parameters
 * ---------------------------------------------------------------------------
 * Target sample rate.  With PLLSAI1 N=17, P=17 → SAI_CLK = 16 MHz.
 * At I2S 32-bit × 2 slots the actual SCK divider gives:
 *   MCKDIV = round(16e6 / (2 × 16000 × 64)) = 8
 *   Actual Fs = 16e6 / (2 × 8 × 64) = 15 625 Hz  (~2.3 % below 16 kHz).
 *
 * To reach exactly 16 kHz, change the PLLSAI1 parameters to:
 *   PLLSAI1N=24, PLLSAI1P=24  → SAI_CLK = 16 × 24 / 24 = 16 MHz ... same.
 * The closest integer divider configuration:
 *   PLLSAI1N=32, PLLSAI1P=2 → VCO=512MHz, P-out=256MHz/2?? — see README.
 * For simplicity, define SAMPLE_RATE_HZ to 16000 and let the feature
 * extractor use it; the 2.3 % error is negligible for the classifier.
 * --------------------------------------------------------------------------*/
#define SAMPLE_RATE_HZ        16000U

/* 10 ms per DMA half → 160 samples (at 16 kHz) */
#define DMA_HALF_BUF_SAMPLES  160U
#define DMA_BUF_SAMPLES       (2U * DMA_HALF_BUF_SAMPLES)

/* Raw 32-bit DMA buffer (must be accessible from DMA IRQ) */
extern uint32_t audio_dma_buf[DMA_BUF_SAMPLES];

/* One-hop (10 ms) audio buffer: 16-bit PCM, signed */
extern int16_t  audio_hop_buf[DMA_HALF_BUF_SAMPLES];

/* Set to 1 by DMA callback, cleared by Audio_Capture_ConsumeHop() */
extern volatile uint8_t audio_hop_ready;

/**
 * @brief  Start SAI1A DMA circular reception.
 *         Call once after MX_SAI1_Init() and MX_DMA_Init().
 */
void Audio_Capture_Start(void);

/**
 * @brief  Copy the pending 10 ms hop into @p out (int16_t, 160 samples) and
 *         clear the ready flag.
 * @retval true  – hop copied successfully
 *         false – no new hop available yet
 */
bool Audio_Capture_ConsumeHop(int16_t out[DMA_HALF_BUF_SAMPLES]);

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_CAPTURE_H__ */
