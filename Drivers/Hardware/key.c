#include "key.h"
#include "stm32f1xx_hal.h"

// 按键状态定义
#define KEY_PRESSED           1        // 按键按下状态
#define KEY_UNPRESSED         0        // 按键释放状态

// 时间参数定义（单位：ms，基于 HAL_GetTick 的 1ms 时基）
#define KEY_TIME_DEBOUNCE     20       // 按键消抖时间：连续 20ms 电平一致才认为状态改变
#define KEY_TIME_DOUBLE       200      // 双击最大间隔时间：在 200ms 内再次按下视为双击
#define KEY_TIME_LONG         2000     // 长按最小时间：持续按下 2000ms 视为长按
#define KEY_TIME_REPEAT       100      // 长按后重复触发间隔：长按后每 100ms 触发一次重复事件

// 按键状态机状态：0=等待按下 1=等待长按 2=等待双击 3=双击已确认 4=长按重复中
#define KEY_ST_WAIT           0
#define KEY_ST_WAIT_LONG      1
#define KEY_ST_WAIT_DBL       2
#define KEY_ST_DBL_DONE       3
#define KEY_ST_LONG_REPEAT    4

uint8_t Key_Flag;

//按键长按"强制结束"请求标志。
//为什么要多一层标志而不是在 ISR 里直接调 app：
//Key_Tick() 在 TIM4 中断里跑，app 状态机在主循环里跑，
//直接在 ISR 里改 app 状态会和主循环发生竞争。ISR 只置这个标志，主循环再消费。
volatile uint8_t Key_Event_Flag;

// 读取按键电平：外部按键接地，读到 0 视为按下
static uint8_t Key_GetState(void)
{
    if (HAL_GPIO_ReadPin(KEY_PORT, KEY_PIN) == GPIO_PIN_RESET)
        return KEY_PRESSED;
    return KEY_UNPRESSED;
}

// 按键状态机初始化：IO 方向已在 CubeMX 的 MX_GPIO_Init() 中配成输入上拉
void Key_Init(void)
{
    Key_Flag = 0;
}

uint8_t Key_Check(uint8_t Flag)
{
    if (Key_Flag & Flag)
    {
        if (Key_Flag != KEY_HOLD)
        {
            Key_Flag &= ~Flag;
        }
        return 1;
    }
    return 0;
}

void Key_Clear(void)
{
    Key_Flag = 0;
}

void Key_Tick(void)
{
    static uint8_t CurrState = KEY_UNPRESSED;
    static uint8_t PrevState = KEY_UNPRESSED;
    static uint8_t s = KEY_ST_WAIT;
    static uint32_t stable = 0;      // 当前电平已稳定持续的时刻
    static uint32_t wait_start = 0;  // 当前事件窗口开始的时刻

    uint32_t now = HAL_GetTick();
    uint8_t raw = Key_GetState();

    // 消抖：电平与已确认状态不同时，必须连续稳定 KEY_TIME_DEBOUNCE 才更新状态
    if (raw == CurrState)
    {
        stable = now;
    }
    else if ((now - stable) >= KEY_TIME_DEBOUNCE)
    {
        PrevState = CurrState;
        CurrState = raw;
        stable = now;
    }

    if (CurrState == KEY_PRESSED)
    {
        Key_Flag |= KEY_HOLD;
    }
    else
    {
        Key_Flag &= ~KEY_HOLD;
    }

    if (CurrState == KEY_PRESSED && PrevState == KEY_UNPRESSED)
    {
        Key_Flag = KEY_DOWN;
    }

    if (CurrState == KEY_UNPRESSED && PrevState == KEY_PRESSED)
    {
        Key_Flag = KEY_UP;
    }

    switch (s)
    {
    case KEY_ST_WAIT:
        if (CurrState == KEY_PRESSED)
        {
            s = KEY_ST_WAIT_LONG;
            wait_start = now;
        }
        break;

    case KEY_ST_WAIT_LONG:
        if (CurrState == KEY_UNPRESSED)
        {
            s = KEY_ST_WAIT_DBL;
            wait_start = now;
        }
        else if ((now - wait_start) >= KEY_TIME_LONG)
        {
            Key_Flag |= KEY_LONG;
            s = KEY_ST_LONG_REPEAT;
            wait_start = now;
        }
        break;

    case KEY_ST_WAIT_DBL:
        if (CurrState == KEY_PRESSED)
        {
            Key_Flag |= KEY_DOUBLE;
            s = KEY_ST_DBL_DONE;
        }
        else if ((now - wait_start) >= KEY_TIME_DOUBLE)
        {
            Key_Flag |= KEY_SINGLE;
            s = KEY_ST_WAIT;
        }
        break;

    case KEY_ST_DBL_DONE:
        if (CurrState == KEY_UNPRESSED)
        {
            s = KEY_ST_WAIT;
        }
        break;

    case KEY_ST_LONG_REPEAT:
        if (CurrState == KEY_UNPRESSED)
        {
            s = KEY_ST_WAIT;
        }
        else if ((now - wait_start) >= KEY_TIME_REPEAT)
        {
            Key_Flag |= KEY_REPEAT;
            wait_start = now;
        }
        break;

    default:
        s = KEY_ST_WAIT;
        break;
    }
}
