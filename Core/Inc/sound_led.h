/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    sound_led.h
  * @brief   Sound-level LED indicator using PA5 (LD2 on Nucleo-L476RG).
  *
  *  Logic (per 10 ms audio hop):
  *    mean-absolute-amplitude >= SOUND_LED_THRESHOLD  →  LED ON
  *    mean-absolute-amplitude <  SOUND_LED_THRESHOLD  →  LED OFF  (immediately)
  *
  *  --------------------------------------------------------------------------
  *  Threshold tuning (SOUND_LED_THRESHOLD)
  *  --------------------------------------------------------------------------
  *  The threshold is the mean-absolute value (MAV) of a 10 ms window of
  *  int16_t PCM samples from the INMP441 microphone.
  *
  *  Full-scale PCM value = 32767 (INT16_MAX).
  *  Typical quiet-room noise floor ≈ 50–200 counts MAV.
  *  Normal speech at ~20–30 cm ≈ 500–3000 counts MAV.
  *  Loud sound / clap            ≈ 5000–20000 counts MAV.
  *
  *  Default: 300 – turns on with any clear sound, stays off in silence.
  *  To make the LED more sensitive  → lower the value (e.g. 100).
  *  To require louder sound         → raise the value (e.g. 1000).
  *
  *  Change the value below and rebuild; no other code changes are needed.
  *  --------------------------------------------------------------------------
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __SOUND_LED_H__
#define __SOUND_LED_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Configurable threshold
 * ---------------------------------------------------------------------------
 * Units: mean-absolute int16_t PCM counts per hop (0 – 32767).
 * See header comment above for guidance on choosing a value.
 * --------------------------------------------------------------------------*/
#define SOUND_LED_THRESHOLD   300U

/**
 * @brief  Drive LD2 (PA5) based on the amplitude of one audio hop.
 *
 *         Computes the mean-absolute value (MAV) of the supplied PCM buffer.
 *         If MAV >= SOUND_LED_THRESHOLD the LED is turned ON; otherwise it is
 *         turned OFF immediately (no hold time).
 *
 *         Call once per 10 ms audio hop, e.g. from acquire_and_process_data().
 *
 * @param  hop       Pointer to int16_t PCM samples.
 * @param  n_samples Number of samples in the hop (typically DMA_HALF_BUF_SAMPLES = 160).
 */
void Sound_LED_UpdateHop(const int16_t *hop, uint32_t n_samples);

#ifdef __cplusplus
}
#endif

#endif /* __SOUND_LED_H__ */
