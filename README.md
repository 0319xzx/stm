# STM32L476 – SAI1A INMP441 + X-CUBE-AI 音频推理示例

本工程实现了一个端到端的音频采集 → 特征提取 → AI 推理流水线，运行于 STM32L476RG（Nucleo-L476RG 开发板）。

---

## 声音指示灯（LD2 / PA5）

板载 LD2（绿色 LED，位于 PA5）用作实时声音指示灯：

| 状态 | LED |
|---|---|
| 当前 10 ms 音频 hop 的平均绝对幅值 ≥ 阈值 | **亮** |
| 平均绝对幅值 < 阈值 | **立即灭**（无保持时间） |

### 阈值调整

阈值定义在 `Core/Inc/sound_led.h`：

```c
#define SOUND_LED_THRESHOLD   300U   /* int16_t PCM 均值绝对值，0–32767 */
```

| 典型幅值参考（MAV，平均绝对值） | 说明 |
|---|---|
| 50 – 200 | 静音 / 环境底噪 |
| 500 – 3000 | 正常说话（距麦克风 20–30 cm） |
| 5000 – 20000 | 大声拍掌 / 喊叫 |

- **降低阈值**（如改为 `100`）→ 对更轻微的声音响应  
- **提高阈值**（如改为 `1000`）→ 仅对较大声音响应

修改后重新编译烧录即可，无需其他改动。

---



| INMP441 引脚 | STM32 引脚 | 说明 |
|---|---|---|
| VDD | 3.3 V | 电源（3.3 V） |
| GND | GND | 地 |
| SD  | PC3  | SAI1_SD_A（I2S 数据） |
| SCK | PB10 | SAI1_SCK_A（I2S 位时钟） |
| WS  | PB9  | SAI1_FS_A（帧同步 / Word Select） |
| L/R | GND  | 左声道（WS 低电平时输出） |

串口调试输出（虚拟 COM 口，经 ST-LINK USB 转 USB）：
- **引脚**：PA2（USART2_TX）、PA3（USART2_RX）（Nucleo 板已连接至 ST-LINK）
- **波特率**：115200，8-N-1

---

## 期望串口输出

系统启动后输出初始化信息，之后每处理一帧（约 10 ms 周期）输出一行：

```
[AI] X-CUBE-AI network init

[    1] q= -85  score=0.1680  consec=0
[    2] q= -60  score=0.2656  consec=0
...
[   98] q=  20  score=0.5781  consec=1
[   99] q=  18  score=0.5703  consec=2
[  100] q=  22  score=0.5859  consec=3
[  101] q=  16  score=0.5625  consec=4
[  102] q=  19  score=0.5742  consec=5  *** TRIGGER ***
```

字段说明：

| 字段 | 含义 |
|---|---|
| `[xxxxx]` | 累计推理帧编号 |
| `q=` | 模型原始 int8 输出值 |
| `score=` | 反量化浮点得分（0.0–1.0） |
| `consec=` | 当前连续超阈值帧数 |
| `*** TRIGGER ***` | 触发事件（连续 5 帧 q ≥ 13 ≈ score ≥ 0.55） |

---

## 触发逻辑说明

- **阈值**：模型 int8 输出 `q ≥ 13`（对应 float score ≈ 0.55078）
- **连续判决**：连续 5 帧均超过阈值才触发一次事件
- **冷却（去抖）**：触发后强制等待 30 帧（≈ 300 ms）再允许下一次触发，连续计数器清零

量化换算：
```
score = (q_out − (−128)) × 0.003906250
      = (q_out + 128)    × 0.003906250
当 q_out = 13：score = 141 × 0.003906250 ≈ 0.5508
```

---

## 实际采样率说明

当前 PLLSAI1 配置（`sai.c` > `HAL_SAI_MspInit`）：
- HSI = 16 MHz，PLLSAI1N = 17，PLLSAI1P = 17
- SAI 输入时钟 = 16 × 17 / 17 = **16 MHz**

I2S 帧长度 = 32 bit × 2 槽 = 64 bit/帧。STM32 SAI MCKDIV 计算：

```
MCKDIV = round(SAI_CLK / (2 × 目标Fs × 帧长))
       = round(16 000 000 / (2 × 16 000 × 64))
       = round(7.8125) = 8
实际 Fs = 16 000 000 / (2 × 8 × 64) = 15 625 Hz
```

实际采样率为 **15 625 Hz**，比目标低 ~2.3 %。这对分类精度影响较小，
但若需精确 16 kHz，建议在 CubeMX 中将 PLLSAI1 配置改为：

| 参数 | 推荐值 | 说明 |
|---|---|---|
| PLLSAI1N | 43 | VCO = 16 × 43 = 688 MHz |
| PLLSAI1P | 43 | SAI_CLK = 688/43 = 16 MHz（同上，无帮助） |

若需更精确，使用外部晶振（MCLK 12.288 MHz → 256×Fs）或 PLLSAI2。

代码配置宏（`audio_capture.h`）：
```c
#define SAMPLE_RATE_HZ  16000U   /* 修改此宏以匹配实际配置 */
```

---

## 工程文件变动说明

### 新增文件
| 文件 | 说明 |
|---|---|
| `Core/Inc/usart.h` | USART2 初始化声明 |
| `Core/Src/usart.c` | USART2 初始化（PA2/PA3，115200 baud），printf 重定向 |
| `Core/Inc/audio_capture.h` | SAI1A DMA 循环采集 API |
| `Core/Src/audio_capture.c` | DMA 半完成/完成回调，音频 hop 缓冲 |
| `Core/Inc/sound_led.h` | 声音指示灯 API 及阈值宏（SOUND_LED_THRESHOLD） |
| `Core/Src/sound_led.c` | 均值绝对幅值计算与 PA5 LED 驱动 |
| `Core/Inc/feature_extract.h` | 特征提取 API（98×40 int8） |
| `Core/Src/feature_extract.c` | Goertzel 算法近似 log-mel 特征（见注意事项） |

### 修改文件
| 文件 | 修改内容 |
|---|---|
| `Core/Src/gpio.c` | 新增 GPIOA 时钟使能、PA5 推挽输出初始化（LD2 指示灯） |
| `Core/Src/main.c` | 新增 USART2 初始化、feature_extract 初始化、启动 SAI DMA |
| `X-CUBE-AI/App/app_x-cube-ai.c` | 实现 acquire_and_process_data / post_process；重写 MX_X_CUBE_AI_Process 为非阻塞模式；每个 hop 调用 Sound_LED_UpdateHop() |
| `MDK-ARM/test.uvprojx` | 新增 usart.c / audio_capture.c / feature_extract.c / sound_led.c / stm32l4xx_hal_uart.c / stm32l4xx_hal_uart_ex.c |

---

## ⚠ 特征提取注意事项

`feature_extract.c` 中的实现是**简化版本**，使用 Goertzel 算法在 40 个 mel 刻度频率点上计算能量，并非与模型训练时完全一致的 log-mel 谱。

如果模型是用 Python 的 `librosa.feature.melspectrogram` 或 TensorFlow `tf.signal.linear_to_mel_weight_matrix` 训练的，需要：
1. 确认训练时的归一化方式（均值/方差或 min-max）
2. 在 `feature_extract.c` 中调整 `LOG_NORM_MIN` / `LOG_NORM_MAX` 常量以匹配训练分布

---

## 编译方法（Keil MDK）

1. 打开 `MDK-ARM/test.uvprojx`
2. 确认 **Device** 已配置为 `STM32L476RGTx`
3. 点击 **Build** (F7)
4. 下载并运行，打开串口终端（115200 baud）观察输出
