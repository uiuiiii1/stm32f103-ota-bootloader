#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H
#include "usart.h"
#define BOOTLOADER_USART_REC_BUFF_LEN 521 //串口接收缓冲区的长度（字节）
extern uint16_t bootloader_rec_full_len; //串口接收完整数据长度

#define APP_FLASH_BASE_ADDR   0x08004000U  //程序写入起始位置
#define APP_RESET_ADDR 0x20000000U          //重置地址
#define APP_END_ADDR   0x08010000U          //程序结束地址

//调试量（定位丢字节，由 main 打印）
extern volatile uint16_t dbg_frame_cnt;
extern volatile uint16_t dbg_last_size;
extern volatile uint16_t dbg_short_size;
extern volatile uint16_t dbg_ore_cnt;   //ORE 溢出次数（HAL 把 ORE 当致命错误关接收，此处已自愈）

//串口接收=>准备接收A程序
void bootloader_init(void);

//主循环轮询：有整帧待写则写入 flash。返回1=处理了一帧，0=无帧。
uint8_t bootloader_process_frame(void);

//帧访问接口（供 App_bootloader 状态机使用：只读/消费，不写 flash）
uint8_t  bootloader_frame_pending(void);
const uint8_t *bootloader_frame_data(void);
uint16_t bootloader_frame_size(void);
void     bootloader_frame_consume(void);

//把遗留的最后一个字节补 0xFF 写入（奇数长度收尾，传输完成后调用）
void bootloader_flash_flush(void);

//接收前/重新升级前清零写入状态（偏移/遗留字节/已擦除页标记）
void bootloader_flash_reset(void);

//把一帧数据写入 flash（擦除+编程，供 Int_bootloader / App_bootloader 复用）
void bootloader_flash_write_frame(const uint8_t *buf, uint16_t len);

//跳转到应用程序
void bootloader_jump_to_app(void);
#endif
