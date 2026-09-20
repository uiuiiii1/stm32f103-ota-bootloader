#ifndef __APP_BOOTLOADER_H
#define __APP_BOOTLOADER_H

#include <stdint.h>

//升级状态机：等待 "start:len" 命令 → 接收写入 flash → 校验 → 跳转 A 程序
void App_bootloader_init(void);          //复位状态机并打印提示
void App_bootloader_work(void);          //主循环轮询调用，驱动状态机
uint8_t App_bootloader_check(void);      //校验：长度 + 读回求和 + 应用向量
uint32_t App_bootloader_recv_len(void);  //当前已写入 flash 的字节数（进度）
void App_bootloader_jump_to_app(void);   //跳转（内部调用 bootloader_jump_to_app）
uint32_t App_bootloader_expected_len(void); //start:len 声明的期望长度（进度显示用）

//按键长按"强制结束"：主机声明的长度比实际发过来的多时，不用等 60 秒超时，
//直接按"实际已收到的字节数"收尾并进入校验。
//只在 RUN 状态（正在等数据）且已收到过数据时受理；否则打印拒绝原因。
void App_bootloader_force_finish(void);

#endif
