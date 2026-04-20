/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    feature_extract.h
  * @brief   Log-energy Mel-band feature extractor for the 98 × 40 model input.
  *
  *  NOTE – SIMPLIFIED IMPLEMENTATION
  *  This feature extractor is a minimal-viable approximation intended for
  *  hardware bring-up and demonstration.  It computes per-frame energy in
  *  40 mel-scale frequency bands using the Goertzel algorithm (equivalent to
  *  evaluating the DFT at 40 specific frequencies).  The log-energy values are
  *  then scaled and quantised to int8 with the model's input quantisation
  *  parameters (scale = 0.042253252, zero_point = 13).
  *
  *  For production deployment replace this module with a feature extractor
  *  that exactly matches the pre-processing pipeline used during model training
  *  (e.g. full log-mel spectrogram with triangular filterbank, pre-emphasis,
  *  and the same normalisation mean/variance).
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __FEATURE_EXTRACT_H__
#define __FEATURE_EXTRACT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "audio_capture.h"  /* for SAMPLE_RATE_HZ, DMA_HALF_BUF_SAMPLES */

/* ---------------------------------------------------------------------------
 * Feature-extraction parameters
 * ---------------------------------------------------------------------------*/

/* Frame length: 25 ms at 16 kHz */
#define FEAT_FRAME_LEN      400U

/* Frame hop: 10 ms at 16 kHz (= DMA_HALF_BUF_SAMPLES) */
#define FEAT_HOP_LEN        DMA_HALF_BUF_SAMPLES   /* 160 */

/* Number of time frames in the model input */
#define FEAT_NUM_FRAMES     98U

/* Number of mel frequency bins */
#define FEAT_NUM_BINS       40U

/* Total model input size in bytes (== AI_NETWORK_IN_1_SIZE_BYTES) */
#define FEAT_MATRIX_BYTES   (FEAT_NUM_FRAMES * FEAT_NUM_BINS)   /* 3920 */

/* Model input quantisation parameters (from network_generate_report.txt):
 *   QLinear(scale=0.042253252, zero_point=13, int8)                          */
#define FEAT_QUANT_SCALE    0.042253252f
#define FEAT_QUANT_ZP       13

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------*/

/**
 * @brief  Initialise the feature extractor (pre-compute Goertzel coefficients).
 *         Call once before the main loop.
 */
void Feature_Extract_Init(void);

/**
 * @brief  Push one 10 ms audio hop (160 int16_t samples) into the extractor.
 *         Internally slides the 400-sample frame window forward by 160 samples,
 *         computes the 40-bin log-energy row, and appends it to the circular
 *         98-frame feature matrix.
 * @param  samples  Pointer to FEAT_HOP_LEN int16_t samples.
 */
void Feature_Extract_PushHop(const int16_t samples[FEAT_HOP_LEN]);

/**
 * @brief  Returns true once the 98-frame feature matrix has been filled for
 *         the first time (and after each subsequent hop).
 */
bool Feature_Extract_IsReady(void);

/**
 * @brief  Copy the current 98 × 40 feature matrix into @p out as int8_t,
 *         laid out row-major: out[frame * FEAT_NUM_BINS + bin].
 *         The matrix is always copied in chronological frame order regardless
 *         of the internal circular-buffer write pointer.
 * @param  out  Destination buffer of at least FEAT_MATRIX_BYTES bytes.
 */
void Feature_Extract_GetMatrix(int8_t out[FEAT_MATRIX_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* __FEATURE_EXTRACT_H__ */
