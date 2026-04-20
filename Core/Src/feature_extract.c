/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    feature_extract.c
  * @brief   Simplified log-energy Mel-band feature extractor (98 × 40, int8).
  *
  *  *** SIMPLIFIED / APPROXIMATE IMPLEMENTATION ***
  *  This module computes an approximation to a log-mel spectrogram without
  *  an FFT library.  It uses the Goertzel algorithm to evaluate the power
  *  spectral density at 40 mel-scale centre frequencies.  This produces
  *  qualitatively similar features to a triangular-filterbank mel spectrogram
  *  but is NOT identical to the typical Python/TensorFlow pre-processing
  *  pipeline.  If the model was trained with a specific normalisation (e.g.
  *  per-sample mean subtraction, global mean/variance normalisation), add that
  *  step here to match training.
  *
  *  Algorithm per frame:
  *   1. Slide the 400-sample window forward by 160 samples (new hop).
  *   2. Apply a Hann window to the 400 samples.
  *   3. For each of the 40 mel-scale centre frequencies, run the Goertzel
  *      algorithm to obtain the normalised power.
  *   4. Apply log10(power + eps).
  *   5. Clip and quantise to int8 using the model's input Q-params
  *      (scale=0.042253252, zero_point=13).
  *   6. Store the 40-element row in the 98-frame circular feature matrix.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "feature_extract.h"
#include <math.h>
#include <string.h>

/* -------------------------------------------------------------------------*/
/* Constants                                                                 */
/* -------------------------------------------------------------------------*/

/* Mel-scale helper macros */
#define HZ_TO_MEL(f)   (2595.0f * log10f(1.0f + (f) / 700.0f))
#define MEL_TO_HZ(m)   (700.0f * (powf(10.0f, (m) / 2595.0f) - 1.0f))

/* Mel frequency range for the 40 bins */
#define MEL_F_LO    80.0f        /* Hz */
#define MEL_F_HI    8000.0f      /* Hz (Nyquist at 16 kHz) */

/* Small value to avoid log(0) */
#define LOG_EPS     1e-7f

/* Normalisation assumption for quantisation:
 *   Log-energy values are roughly in the range [LOG_NORM_MIN, LOG_NORM_MAX].
 *   Adjust these to match the training pipeline if known.
 *   The values map the log-energy range onto approximately the int8 range
 *   [-5.96, +4.82] that the model's input quantisation covers.             */
#define LOG_NORM_MIN   (-4.0f)   /* log10 power ~ -4 → maps to quant min    */
#define LOG_NORM_MAX   ( 4.0f)   /* log10 power ~ +4 → maps to quant max    */

/* -------------------------------------------------------------------------*/
/* Module state                                                              */
/* -------------------------------------------------------------------------*/

/* Sliding audio window: FEAT_FRAME_LEN samples (400) */
static int16_t  s_frame_buf[FEAT_FRAME_LEN];

/* Hann window coefficients */
static float    s_hann[FEAT_FRAME_LEN];

/* Goertzel coefficients (2*cos(2*pi*f_k/Fs)) and exact frequencies */
static float    s_goertzel_coeff[FEAT_NUM_BINS];
static float    s_mel_freqs[FEAT_NUM_BINS];   /* centre Hz of each bin */

/* Circular feature matrix: [FEAT_NUM_FRAMES][FEAT_NUM_BINS] int8 */
static int8_t   s_feat_matrix[FEAT_NUM_FRAMES][FEAT_NUM_BINS];
static uint32_t s_frame_write_idx = 0;   /* next row to write */
static uint32_t s_frame_count     = 0;   /* total frames pushed (capped at FEAT_NUM_FRAMES) */

/* -------------------------------------------------------------------------*/
/* Internal helpers                                                          */
/* -------------------------------------------------------------------------*/

/**
 * @brief  Compute Goertzel power for one frequency in the current frame.
 *
 * @param  samples  Float samples (Hann-windowed, length N = FEAT_FRAME_LEN).
 * @param  coeff    2 * cos(2*pi*k/N) where k = f * N / Fs.
 * @return Normalised power (sum-of-squares normalised by N^2).
 */
static float goertzel_power(const float *samples, float coeff)
{
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f;
    uint32_t n;

    for (n = 0; n < FEAT_FRAME_LEN; n++)
    {
        s0 = coeff * s1 - s2 + samples[n];
        s2 = s1;
        s1 = s0;
    }

    /* Power = |X(k)|^2 / N^2 */
    float power = (s1 * s1 + s2 * s2 - coeff * s1 * s2)
                  / ((float)(FEAT_FRAME_LEN * FEAT_FRAME_LEN));
    return power;
}

/**
 * @brief  Quantise a float feature value to int8 using the model's
 *         Q-params (scale=0.042253252, zero_point=13).
 */
static int8_t quantise(float f)
{
    float q = f / FEAT_QUANT_SCALE + (float)FEAT_QUANT_ZP;

    /* Saturate to int8 range */
    if (q < -128.0f) q = -128.0f;
    if (q >  127.0f) q =  127.0f;

    return (int8_t)(q + 0.5f);   /* round */
}

/* -------------------------------------------------------------------------*/
/* Public API                                                                */
/* -------------------------------------------------------------------------*/

void Feature_Extract_Init(void)
{
    /* --- Compute Hann window -------------------------------------------- */
    for (uint32_t n = 0; n < FEAT_FRAME_LEN; n++)
    {
        s_hann[n] = 0.5f * (1.0f - cosf(2.0f * 3.14159265f
                                         * (float)n
                                         / (float)(FEAT_FRAME_LEN - 1)));
    }

    /* --- Compute 40 mel-scale centre frequencies and Goertzel coefficients */
    float mel_lo = HZ_TO_MEL(MEL_F_LO);
    float mel_hi = HZ_TO_MEL(MEL_F_HI);

    /* 42 equally-spaced mel points; inner 40 are the bin centres */
    for (uint32_t b = 0; b < FEAT_NUM_BINS; b++)
    {
        float mel = mel_lo + (float)(b + 1)
                    * (mel_hi - mel_lo) / (float)(FEAT_NUM_BINS + 1);
        float freq = MEL_TO_HZ(mel);
        s_mel_freqs[b] = freq;

        /* Goertzel coefficient: 2 * cos(2*pi * freq / Fs) */
        float omega = 2.0f * 3.14159265f * freq / (float)SAMPLE_RATE_HZ;
        s_goertzel_coeff[b] = 2.0f * cosf(omega);
    }

    /* Clear state */
    memset(s_frame_buf,    0, sizeof(s_frame_buf));
    memset(s_feat_matrix,  0, sizeof(s_feat_matrix));
    s_frame_write_idx = 0;
    s_frame_count     = 0;
}

void Feature_Extract_PushHop(const int16_t samples[FEAT_HOP_LEN])
{
    /* --- Slide the frame window forward by FEAT_HOP_LEN samples ---------- */
    uint32_t keep = FEAT_FRAME_LEN - FEAT_HOP_LEN;   /* 400 - 160 = 240 */
    memmove(&s_frame_buf[0], &s_frame_buf[FEAT_HOP_LEN],
            keep * sizeof(int16_t));
    memcpy(&s_frame_buf[keep], samples,
           FEAT_HOP_LEN * sizeof(int16_t));

    /* --- Convert to float and apply Hann window -------------------------- */
    float windowed[FEAT_FRAME_LEN];
    for (uint32_t n = 0; n < FEAT_FRAME_LEN; n++)
    {
        windowed[n] = (float)s_frame_buf[n] * s_hann[n];
    }

    /* --- Compute log-energy for each mel bin ------------------------------ */
    int8_t row[FEAT_NUM_BINS];
    for (uint32_t b = 0; b < FEAT_NUM_BINS; b++)
    {
        float power   = goertzel_power(windowed, s_goertzel_coeff[b]);
        float log_pow = log10f(power + LOG_EPS);

        /* Clamp log-power to assumed feature range, then scale to float
         * values that the model's Q-params can represent.
         *
         * Mapping: [LOG_NORM_MIN, LOG_NORM_MAX] → roughly [-5.96, +4.82]
         *   scaled = (log_pow - LOG_NORM_MIN) / (LOG_NORM_MAX - LOG_NORM_MIN)
         *          * (FLOAT_MAX - FLOAT_MIN) + FLOAT_MIN
         * where FLOAT_MIN = (-128-13) * 0.042253252  ≈ -5.96
         *       FLOAT_MAX = ( 127-13) * 0.042253252  ≈  4.81
         *
         * NOTE: calibrate LOG_NORM_MIN / LOG_NORM_MAX to match training.     */
        float float_min = ((float)(-128 - FEAT_QUANT_ZP)) * FEAT_QUANT_SCALE;
        float float_max = ((float)( 127 - FEAT_QUANT_ZP)) * FEAT_QUANT_SCALE;

        float norm = (log_pow - LOG_NORM_MIN) / (LOG_NORM_MAX - LOG_NORM_MIN);
        float feat = norm * (float_max - float_min) + float_min;

        row[b] = quantise(feat);
    }

    /* --- Write row into circular feature matrix -------------------------- */
    memcpy(s_feat_matrix[s_frame_write_idx], row, FEAT_NUM_BINS);
    s_frame_write_idx = (s_frame_write_idx + 1) % FEAT_NUM_FRAMES;

    if (s_frame_count < FEAT_NUM_FRAMES)
    {
        s_frame_count++;
    }
}

bool Feature_Extract_IsReady(void)
{
    return (s_frame_count >= FEAT_NUM_FRAMES);
}

void Feature_Extract_GetMatrix(int8_t out[FEAT_MATRIX_BYTES])
{
    /* Linearise the circular matrix in chronological order.
     * s_frame_write_idx points to the OLDEST entry (next to be overwritten). */
    uint32_t oldest = s_frame_write_idx;

    for (uint32_t f = 0; f < FEAT_NUM_FRAMES; f++)
    {
        uint32_t src_row = (oldest + f) % FEAT_NUM_FRAMES;
        memcpy(&out[f * FEAT_NUM_BINS],
               s_feat_matrix[src_row],
               FEAT_NUM_BINS);
    }
}
