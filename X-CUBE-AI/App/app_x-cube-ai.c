
/**
  ******************************************************************************
  * @file    app_x-cube-ai.c
  * @author  X-CUBE-AI C code generator
  * @brief   AI program body
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

 /*
  * Description
  *   v1.0 - Minimum template to show how to use the Embedded Client API
  *          model. Only one input and one output is supported. All
  *          memory resources are allocated statically (AI_NETWORK_XX, defines
  *          are used).
  *          Re-target of the printf function is out-of-scope.
  *   v2.0 - add multiple IO and/or multiple heap support
  *
  *   For more information, see the embeded documentation:
  *
  *       [1] %X_CUBE_AI_DIR%/Documentation/index.html
  *
  *   X_CUBE_AI_DIR indicates the location where the X-CUBE-AI pack is installed
  *   typical : C:\Users\[user_name]\STM32Cube\Repository\STMicroelectronics\X-CUBE-AI\7.1.0
  */

#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

#if defined ( __ICCARM__ )
#elif defined ( __CC_ARM ) || ( __GNUC__ )
#endif

/* System headers */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include "app_x-cube-ai.h"
#include "main.h"
#include "ai_datatypes_defines.h"
#include "network.h"
#include "network_data.h"

/* USER CODE BEGIN includes */
#include "audio_capture.h"
#include "feature_extract.h"
/* USER CODE END includes */

/* IO buffers ----------------------------------------------------------------*/

#if !defined(AI_NETWORK_INPUTS_IN_ACTIVATIONS)
AI_ALIGNED(4) ai_i8 data_in_1[AI_NETWORK_IN_1_SIZE_BYTES];
ai_i8* data_ins[AI_NETWORK_IN_NUM] = {
data_in_1
};
#else
ai_i8* data_ins[AI_NETWORK_IN_NUM] = {
NULL
};
#endif

#if !defined(AI_NETWORK_OUTPUTS_IN_ACTIVATIONS)
AI_ALIGNED(4) ai_i8 data_out_1[AI_NETWORK_OUT_1_SIZE_BYTES];
ai_i8* data_outs[AI_NETWORK_OUT_NUM] = {
data_out_1
};
#else
ai_i8* data_outs[AI_NETWORK_OUT_NUM] = {
NULL
};
#endif

/* Activations buffers -------------------------------------------------------*/

AI_ALIGNED(32)
static uint8_t pool0[AI_NETWORK_DATA_ACTIVATION_1_SIZE];

ai_handle data_activations0[] = {pool0};

/* AI objects ----------------------------------------------------------------*/

static ai_handle network = AI_HANDLE_NULL;

static ai_buffer* ai_input;
static ai_buffer* ai_output;

static void ai_log_err(const ai_error err, const char *fct)
{
  /* USER CODE BEGIN log */
  if (fct)
    printf("TEMPLATE - Error (%s) - type=0x%02x code=0x%02x\r\n", fct,
        err.type, err.code);
  else
    printf("TEMPLATE - Error - type=0x%02x code=0x%02x\r\n", err.type, err.code);

  do {} while (1);
  /* USER CODE END log */
}

static int ai_boostrap(ai_handle *act_addr)
{
  ai_error err;

  /* Create and initialize an instance of the model */
  err = ai_network_create_and_init(&network, act_addr, NULL);
  if (err.type != AI_ERROR_NONE) {
    ai_log_err(err, "ai_network_create_and_init");
    return -1;
  }

  ai_input = ai_network_inputs_get(network, NULL);
  ai_output = ai_network_outputs_get(network, NULL);

#if defined(AI_NETWORK_INPUTS_IN_ACTIVATIONS)
  /*  In the case where "--allocate-inputs" option is used, memory buffer can be
   *  used from the activations buffer. This is not mandatory.
   */
  for (int idx=0; idx < AI_NETWORK_IN_NUM; idx++) {
	data_ins[idx] = ai_input[idx].data;
  }
#else
  for (int idx=0; idx < AI_NETWORK_IN_NUM; idx++) {
	  ai_input[idx].data = data_ins[idx];
  }
#endif

#if defined(AI_NETWORK_OUTPUTS_IN_ACTIVATIONS)
  /*  In the case where "--allocate-outputs" option is used, memory buffer can be
   *  used from the activations buffer. This is no mandatory.
   */
  for (int idx=0; idx < AI_NETWORK_OUT_NUM; idx++) {
	data_outs[idx] = ai_output[idx].data;
  }
#else
  for (int idx=0; idx < AI_NETWORK_OUT_NUM; idx++) {
	ai_output[idx].data = data_outs[idx];
  }
#endif

  return 0;
}

static int ai_run(void)
{
  ai_i32 batch;

  batch = ai_network_run(network, ai_input, ai_output);
  if (batch != 1) {
    ai_log_err(ai_network_get_error(network),
        "ai_network_run");
    return -1;
  }

  return 0;
}

/* USER CODE BEGIN 2 */

/* ---------------------------------------------------------------------------
 * Decision-fusion state
 * ---------------------------------------------------------------------------
 * Model output quantisation (from network_generate_report.txt):
 *   QLinear(scale=0.003906250, zero_point=-128, int8)
 *   float_score = (q_out - (-128)) * 0.003906250
 *
 * Trigger condition (user-specified):
 *   Threshold float 0.55 → int8 q = round(0.55/0.003906250) + (-128)
 *                         = round(140.8) - 128 = 141 - 128 = 13
 *   → q_out >= 13  ≡  score >= ~0.550781
 *
 * Consecutive-frame counter: 5 frames above threshold → trigger event.
 * Cooldown: 30 frames (~300 ms) after a trigger, reset counter.
 * --------------------------------------------------------------------------*/
#define DECISION_THRESHOLD_Q   13        /* int8 output threshold             */
#define DECISION_CONSEC_REQ    5         /* consecutive frames required       */
#define DECISION_COOLDOWN_FRAMES 30      /* frames to skip after a trigger    */

#define OUT_SCALE    0.003906250f
#define OUT_ZP       (-128)

static int16_t  s_consec_count  = 0;
static int16_t  s_cooldown      = 0;
static uint32_t s_total_frames  = 0;

/*
 * acquire_and_process_data:
 *   1. Try to consume one 10 ms audio hop from the DMA double-buffer.
 *   2. Push it into the feature extractor (slides the 400-sample window).
 *   3. If the 98-frame feature matrix is ready, fill the AI input buffer.
 *   Returns  0 on success (inference should proceed)
 *           -1 if no new hop or feature matrix not yet full.
 */
int acquire_and_process_data(ai_i8 *data[])
{
    int16_t hop[FEAT_HOP_LEN];

    /* Wait for the next 10 ms audio hop from DMA callback */
    if (!Audio_Capture_ConsumeHop(hop))
    {
        return -1;   /* No new audio data yet */
    }

    /* Slide feature window and compute one new mel-energy row */
    Feature_Extract_PushHop(hop);

    /* Not enough frames yet for the first full 98-frame window */
    if (!Feature_Extract_IsReady())
    {
        return -1;
    }

    /* Fill the AI input buffer (data_ins[0] points into activations buffer) */
    Feature_Extract_GetMatrix((int8_t *)data[0]);

    return 0;
}

/*
 * post_process:
 *   Read the single int8 output byte, dequantise to float, apply 5-frame
 *   consecutive threshold logic with cooldown, and print result via UART.
 */
int post_process(ai_i8 *data[])
{
    int8_t  q_out  = ((int8_t *)data[0])[0];
    float   score  = (float)(q_out - OUT_ZP) * OUT_SCALE;

    s_total_frames++;

    int triggered = 0;

    if (s_cooldown > 0)
    {
        s_cooldown--;
        s_consec_count = 0;
    }
    else
    {
        if (q_out >= DECISION_THRESHOLD_Q)
        {
            s_consec_count++;
            if (s_consec_count >= DECISION_CONSEC_REQ)
            {
                triggered      = 1;
                s_consec_count = 0;
                s_cooldown     = DECISION_COOLDOWN_FRAMES;
            }
        }
        else
        {
            s_consec_count = 0;
        }
    }

    /* UART output: frame index, raw int8 output, float score, trigger flag */
    printf("[%5lu] q=%4d  score=%.4f  consec=%d  %s\r\n",
           (unsigned long)s_total_frames,
           (int)q_out,
           score,
           (int)s_consec_count,
           triggered ? "*** TRIGGER ***" : "");

    return 0;
}
/* USER CODE END 2 */

/* Entry points --------------------------------------------------------------*/

void MX_X_CUBE_AI_Init(void)
{
    /* USER CODE BEGIN 5 */
  printf("\r\n[AI] X-CUBE-AI network init\r\n");

  ai_boostrap(data_activations0);
    /* USER CODE END 5 */
}

void MX_X_CUBE_AI_Process(void)
{
    /* USER CODE BEGIN 6 */
    /*
     * Audio-driven inference pipeline (non-blocking, called from main loop):
     *
     *  1. acquire_and_process_data: consume one 10 ms audio hop, update the
     *     feature matrix.  Returns -1 if no data yet (skip inference this
     *     call) or 0 when the 98×40 matrix is ready.
     *  2. ai_run: run the X-CUBE-AI inference engine.
     *  3. post_process: dequantise output, apply 5-frame decision fusion,
     *     print to UART.
     *
     * The original do/while(res==0) template is replaced here to prevent the
     * error handler from firing on a "no data yet" (-1) return code.
     */
    int res;

    if (!network) return;

    res = acquire_and_process_data(data_ins);

    if (res != 0)
    {
        /* No new feature frame ready – come back next main-loop iteration */
        return;
    }

    res = ai_run();
    if (res != 0)
    {
        ai_error err = {AI_ERROR_INVALID_STATE, AI_ERROR_CODE_NETWORK};
        ai_log_err(err, "ai_run failed");
        return;
    }

    post_process(data_outs);
    /* USER CODE END 6 */
}
#ifdef __cplusplus
}
#endif
