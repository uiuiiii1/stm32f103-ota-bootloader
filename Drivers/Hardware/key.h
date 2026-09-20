#ifndef __KEY_H
#define __KEY_H

#include <stdint.h>

// 按键引脚：PB12 内部上拉，外部按键接地
#define KEY_PORT              GPIOB
#define KEY_PIN               GPIO_PIN_12

// 按键事件标志位掩码
#define KEY_HOLD              0x01    // 按键保持按住（电平状态，持续有效）
#define KEY_DOWN              0x02    // 按键按下（边沿触发）
#define KEY_UP                0x04    // 按键释放（边沿触发）
#define KEY_SINGLE            0x08    // 单击事件
#define KEY_DOUBLE            0x10    // 双击事件
#define KEY_LONG              0x20    // 长按事件
#define KEY_REPEAT            0x40    // 长按连续重复触发

extern uint8_t Key_Flag;                // 按键事件标志变量
extern volatile uint8_t Key_Event_Flag; //按键长按"强制结束"请求标志：TIM4 中断里置 1，主循环里读取并清零

void Key_Init(void);                    // 按键状态机初始化
uint8_t Key_Check(uint8_t Flag);        // 查询是否存在指定按键事件
void Key_Clear(void);                   // 清空按键事件标志
void Key_Tick(void);                    // 按键扫描函数，主循环中周期调用（内部用 HAL_GetTick 计时）

#endif /* __KEY_H */
