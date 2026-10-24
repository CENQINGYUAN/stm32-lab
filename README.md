# STM32 入门实践：串口命令控制舵机与 LED

大一阶段的单片机练习，两块板子、两个工程，都是 **STM32F103C8T6 + STM32CubeMX + Keil MDK**，
核心都是同一件事：**用串口接收文本命令，解析后控制外设，再把结果回传给上位机。**

不是"点亮一个灯"级别的 demo，两个工程都实现了完整的命令行协议：参数校验、越界保护、错误回显。

| 目录 | 内容 | 演示 |
|---|---|---|
| `01-servo-uart-control` | 串口输入角度，SG90 舵机转到指定角度 | [舵机演示视频](docs/servo-demo.mp4) |
| `02-led-pattern-uart` | 串口切换三种 LED 模式、调节速度、查询状态 | — |

---

## 硬件

- **MCU**：STM32F103C8T6 最小系统板（LQFP48，72 MHz，HSE 8 MHz × PLL9）
- **舵机**：SG90（50 Hz PWM，0.5–2.5 ms 对应 0°–180°）
- **LED**：4 只，接 PB0 / PB1 / PB3 / PB11
- **串口**：USART1，115200 8N1，USB-TTL 接 PA9(TX) / PA10(RX)

---

## 01 · 串口控制舵机

### 功能

上电后串口打印 `SG90 Servo Ready`，之后发送：

```
angle: 90
```

舵机转到 90°，串口回显 `Servo moved to 90 deg`。非法输入会得到明确提示：

| 输入 | 回显 |
|---|---|
| `angle: 200` | `Angle range error, only 0-180` |
| `foo` | `Command format error, please retry` |

### 实现要点

**1. PWM 参数是算出来的，不是试出来的**

```
72 MHz / (PSC+1 = 72) = 1 MHz 计数频率
1 MHz / (ARR+1 = 20000) = 50 Hz  → 周期 20 ms，正好是舵机的标准频率
```

于是 CCR 的 1 个计数 = 1 µs，0.5 ms 高电平 = `CCR 500`，2.5 ms = `CCR 2500`。
角度到 CCR 的换算写成一行：

```c
int ccr = 500 + angle * 2000 / 180;   // 0°→500, 180°→2500
__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ccr);
```

**2. 串口用中断逐字节收，不用阻塞式 `HAL_UART_Receive`**

`HAL_UART_Receive_IT()` 每次只收 1 字节，收到就在 `HAL_UART_RxCpltCallback` 里存进缓冲区、
重新挂起下一次接收。**收到 `\r` 或 `\n` 才算一条命令结束**，然后交给解析函数。

这样带来的好处是：主循环可以是空的（`while(1){}`），CPU 不浪费在轮询上，
上位机什么时候发都能立刻响应。

**3. 缓冲区边界**

`rx_buf[32]`，`rx_index >= 30` 时强制截断并复位，避免无换行输入把内存写穿。

---

## 02 · 串口控制 LED 三种模式

### 功能

```
mode:1        流水灯
mode:2        全部闪烁
mode:3        呼吸灯
speed:500     周期设为 500 ms（范围 100–2000）
status        查询当前状态 → Mode:1, Speed:500ms
```

每条指令都有对应的成功/失败回显，例如 `Error: mode 1-3 only`、`Speed changed`。

### 实现要点

**1. 三种模式用一个状态机切换，不是三段独立的 while 循环**

`current_mode` 是唯一的状态变量，`HAL_TIM_PeriodElapsedCallback`（1 ms 触发一次）
里用 `if / else if` 分派到三种行为。这样模式切换是**立即生效**的，
不需要等当前动画跑完，也不会出现"卡在某个死循环里出不来"。

**2. 时基和动画解耦：用 `tick_1ms` 计数，而不是 `HAL_Delay()`**

```c
tick_1ms++;
if (tick_1ms >= speed_ms) { tick_1ms = 0; /* 翻转 LED */ }
```

这是这个工程里我自己觉得最值得记的一点。如果改成 `HAL_Delay(500)` 放在主循环里，
效果看起来一样，但**串口命令会被延迟最多 500 ms 才响应**，
而且改速度要改延时值、三种模式要写三套循环。

用"1 ms 中断 + 计数器"之后：主循环空着、串口随时能响应、改速度只是改一个变量。

**3. 呼吸灯的现状**

模式 3 目前是**简化实现**——用 10 级台阶配合全亮/全灭模拟明暗变化，
不是真正的 PWM 占空比渐变。这个我知道做得不够，见下面"已知不足"。

---

## 已知不足（复查代码时记下的）

写这个 README 的时候重新读了一遍代码，发现几个当时没意识到的点：

1. **呼吸灯不是真呼吸。** 应该改用 TIM 的 PWM 输出 + 查表做占空比渐变，
   现在是数字开关的分级模拟，肉眼能看出台阶。
2. **中断里做了阻塞发送。** `HAL_UART_RxCpltCallback` 里直接调用了 `HAL_UART_Transmit`
   和 `sprintf`，报文一长就会拉长中断响应时间。正确做法是环形缓冲 + 主循环里发送。
3. **快速连续输入可能丢字节。** 接收回调里只有单字符缓冲，没有队列，
   上位机连续发送时可能出现字节覆盖。
4. **`atoi` 不校验数字合法性。** 输入 `angle:abc` 会被解析成 0，然后当成合法角度执行，
   应该先检查数字部分是否全为数字字符。

---

## 如何编译运行

本仓库**不包含 `Drivers/` 目录**（HAL 库和 CMSIS 是 CubeMX 生成的第三方代码，
899 个文件、约 65 MB，会盖住自己的代码），只保留手写部分和工程配置。

想完整编译，按三步：

1. 用 **STM32CubeMX** 打开对应目录下的 `*.ioc`
2. 菜单 `Project → Generate Code`（会自动重新生成 `Drivers/`）
3. 用 **Keil MDK 5** 打开 `MDK-ARM/*.uvprojx`，编译下载

> 生成代码时注意：手写的业务逻辑都放在 `/* USER CODE BEGIN */ ... /* USER CODE END */`
> 之间，CubeMX 重新生成不会覆盖这部分。

---

## 目录结构

```
.
├── README.md
├── docs/
│   └── servo-demo.mp4          舵机演示
├── 01-servo-uart-control/
│   ├── sg90_pwm.ioc            CubeMX 工程配置（引脚、时钟、外设都在这）
│   ├── Core/
│   │   ├── Inc/                外设头文件
│   │   └── Src/
│   │       ├── main.c          ← 命令解析、PWM 输出、串口回调
│   │       ├── tim.c           TIM1 PWM 初始化（PSC=71, ARR=19999）
│   │       └── usart.c         USART1 初始化（115200 8N1）
│   └── MDK-ARM/
│       └── sg90_pwm.uvprojx    Keil 工程
└── 02-led-pattern-uart/
    ├── exam4_led.ioc
    ├── Core/
    │   └── Src/
    │       └── main.c          ← 状态机、模式调度、命令解析
    └── MDK-ARM/
        └── exam4_led.uvprojx
```

---

## 接下来想做的

- 把两个工程的串口收发都改成环形缓冲，主循环里发送
- 呼吸灯改成真正的 PWM 渐变
- 命令协议从 `key: value` 升级成带校验的简单帧格式（加长度和校验和）

---

*2026 级 自动化专业 · 单片机课程练习*
