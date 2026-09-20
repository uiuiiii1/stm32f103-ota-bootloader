# STM32F103 串口 OTA Bootloader

基于 STM32F103C8T6 的串口 OTA bootloader：上位机通过 USART1 发送 `start:<长度>` + 固件裸数据，bootloader 写入 app 分区，校验通过后跳转。支持长按按键 2 秒强制结束。

---

## 📌 版本导航

| 版本 | 位置 | 说明 |
|---|---|---|
| **中断双缓冲版**（当前） | **仓库根目录**（`./`） | RX 中断 + IDLE 空闲检测，双缓冲 2×521B |
| **DMA 版**（待补充） | **`dma-version/`** 子目录 | 使用 DMA 接收 |

**下载提示**：
- 要**中断双缓冲版**：直接下载仓库根目录的源码（`Core/`、`Drivers/`、`interface/`、`template.ioc`、`.mxproject`）
- 要 **DMA 版**：只下载 `dma-version/` 子目录里的文件
- 两个版本的源码**独立**，不要混用

---

## 硬件

| 项 | 值 |
|---|---|
| MCU | STM32F103C8T6 (LQFP48)，64KB Flash / 20KB SRAM |
| 时钟 | HSE 8MHz × PLL9 = 72MHz，APB1=36MHz |
| 串口 | USART1，9600 baud，8N1，RX 中断 + IDLE 空闲检测，双缓冲 2×521B |
| 按键 | PB12，内部上拉，长按 2000ms = KEY_TIME_LONG |
| 定时 | TIM4 1ms tick（Prescaler=72-1, Period=1000-1），驱动按键扫描 |

## 分区布局

| 区域 | 地址 | 大小 |
|---|---|---|
| Bootloader | 0x08000000 – 0x08004000 | 16KB |
| App | 0x08004000 – 0x08010000 | 48KB |

常量定义在 `interface/Int_bootloader.h`：`APP_FLASH_BASE_ADDR` / `APP_END_ADDR` / `APP_RESET_ADDR=0x20000000`。

链接区在 `MDK-ARM/template.uvprojx` 硬锁 16KB——这是刻意的护栏：bootloader 一旦超过 16KB 会直接链接失败，不会静默压进 app 区。

当前体积：Code=13420 + RO-data=312 = 13732 字节 (13.41KB)，余量 2752B。

App 固件必须把向量表放在 0x08004000（scatter 起始 + SCB->VTOR），且镜像大小 ≤ 48KB。

## 协议

1. 上位机发送: `"start:<十进制字节数>"`（全小写，大小写敏感）
2. 随后发送固件裸数据（无帧头/校验位，按 UART 空闲线分帧）
3. 60 秒内无新数据 → 回 INIT（须重发 start）
4. 接收完 → 校验 → 通过则跳转 app，失败则回 INIT

**分帧机制**：没有协议头，用 `HAL_UARTEx_ReceiveToIdle_IT` 靠空闲线判定帧边界。所以上位机两次数据之间要有静默间隔（建议 ≥1 字节时间以上），否则会被合并成一帧。

`parse_start`（App_bootloader.c:48）容错：命令不必在帧首（前面允许杂字节）、数字之后多余的 `\r\n` 一律丢弃不写入 flash。长度上限保护 `v > 0xC000` 拒绝。

## 校验链（App_bootloader_check）

1. **长度**：`received_len == expected_len`；强制结束模式下跳过此项，改用实际接收长度
2. **读回求和**：从 APP_FLASH_BASE_ADDR 逐字节读回累加，与写入时累计的 `data_sum` 比对（能抓 ORE 丢字节/写错位）
3. **向量表**：SP 高 16 位必须 == 0x20000000；PC 必须落在 [0x08004000, 0x08010000)

## 强制结束

- `tim.c:125-141`：TIM4 ISR 里扫按键，长按满 2s 置 `s_force_armed`，只置一次 `Key_Event_Flag`
- `main.c:121-125`：主循环消费标志 → `App_bootloader_force_finish()`（ISR 置标志 / 主循环消费，避免和状态机竞争）
- `App_bootloader.c:297-302`：仅在 RUN 状态且 `received_len > 0` 时受理；否则打印拒绝原因
- `App_bootloader.c:156`：RUN 态遇标志 → 落盘（`bootloader_flash_flush`，奇数字节补 0xFF）→ 转 CHECK_DATA

## 已知问题

1. 校验只有读回求和，没有 CRC——理论上可被补偿性错误骗过；且求和是 O(n) 逐字节读 flash，48KB 会占不少时间
2. 只有一个 app 槽，没有 A/B 与回滚——刷坏只能 SWD 重烧 bootloader
3. 调试 printf 和 OTA 数据共用 USART1（usart.c:161-163 `fputc` → `HAL_UART_Transmit`）。main.c:129-136 每 1s 打印一条（约 47ms 阻塞 @9600），上位机会看到日志和数据混流。RX 走中断双缓冲所以不丢数据，但要干净分离需换串口或升级中静音
4. 9600 baud 很慢，48KB 要约 40s；波特率可以调高
5. ORE 自愈：HAL 把 ORE 当致命错误会关接收，Int_bootloader.c:86-89 已清标志并重新挂接收，`dbg_ore_cnt` 计数
6. `bootloader_process_frame()` 是遗留接口，现已被帧访问器（`bootloader_frame_pending/data/size/consume`）取代，但仍是导出符号
7. 源码 UTF-8 编码，注释中英文混杂
8. 单槽 flash 写入逐帧擦写，断电不保证一致，无"有效镜像"标记

## 构建

Keil MDK-ARM5，目标 STM32F103C8，MicroLIB，-O3，链接区 16KB。

```
"D:\Keil5\UV4\UV4.exe" -r template.uvprojx -o build.log
```

退出码 0=干净 / 1=有警告 / 2=有错误 / 3=致命。

## 串口烧录约束（重要）

bootloader 用 `HAL_UARTEx_ReceiveToIdle_IT` 双缓冲接收，单块缓冲 521B。上位机发送固件时需要注意速率，避免 ORE（Overrun Error）导致丢字节。

| 参数 | 理论上限 | 本工程实测极限 |
|---|---|---|
| 单次发送块大小 | ≤ 521 字节（单缓冲块大小） | ≤ 256 字节 |
| 块间延时 | ≥ 1 字节静默（靠 UART 空闲线分帧） | ≥ 100 ms |
| 波特率 | 可调高 | 9600 |

**原因**：接收中断双缓冲在 9600 baud 下处理速度受 CPU 与中断响应影响，连续高速发送时可能来不及清空接收缓冲区，触发 ORE。Int_bootloader.c:86-89 有 ORE 自愈（清标志 + 重新挂接收），但连续丢字节会导致校验求和不匹配。本工程在 9600 baud 下实测稳定上限为每发 ≤256 字节后等 ≥100ms，非协议硬限制。波特率调高后可相应放宽块间延时。
