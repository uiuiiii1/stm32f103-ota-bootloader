#include "Int_bootloader.h"
#include "stdlib.h"
#include "string.h"
#include "stdio.h"
//（1）解决性能最简单个方法，加内存
//（2）换一个高速协议
//（3）降低波特率

//双缓冲 ping-pong：两个接收缓冲；回调里"先重武装另一个缓冲、再把本帧搬进暂存区"。
//flash 擦除/写入全部放在主循环（bootloader_process_frame / App 状态机），ISR 只做微秒级的搬数据，
//中断处理时间恒定，任何波特率/帧间隙都不会丢字节。
//DMA 版与中断版唯一区别：HAL_UARTEx_ReceiveToIdle_IT 换成 HAL_UARTEx_ReceiveToIdle_DMA，
//且 DMA 会对一次接收过程发 HT/IDLE/TC 三种事件，HT 必须 return 忽略（见回调注释）。
static uint8_t  rx_buf[2][BOOTLOADER_USART_REC_BUFF_LEN];
static volatile uint8_t rx_active = 0;   //当前接收写入哪个缓冲(0/1)

//整帧暂存：回调搬完立即返回，主循环再慢慢写 flash（与接收完全解耦）
static uint8_t           frame_buf[BOOTLOADER_USART_REC_BUFF_LEN];
static volatile uint8_t  frame_ready = 0;  //1=有整帧待写（主循环置0）
static volatile uint16_t frame_len   = 0;

uint16_t bootloader_rec_len=0; //串口接收数据长度
uint16_t bootloader_rec_full_len=0; //串口接收完整数据长度（兼作写进度，供 main.c printf）

static uint32_t write_offset = 0;   //已写入 Flash 的字节偏移，恒为偶数（半字对齐）
static uint32_t current_erased_page = 0xFFFFFFFFU; //已处理过的页（已擦除 或 已验证为干净）

uint8_t last_byte=0; //末尾可能出现的单独字字节
uint8_t last_byte_flag=0; //是否需要写入最后一个字节（0=不需要，1=需要）

//调试：定位丢字节，由 main 循环打印（ISR 里只赋值，不 printf）
volatile uint16_t dbg_frame_cnt  = 0;   //收到的帧数
volatile uint16_t dbg_last_size  = 0;   //最近一帧的 Size
volatile uint16_t dbg_short_size = 0;   //第一个"非256"帧的长度(0=还没出现)
volatile uint16_t dbg_ore_cnt   = 0;   //ORE 溢出次数（HAL 把 ORE 当致命错误关接收，这里自愈并计数）

//串口接收=>准备接收A程序
void bootloader_init(void)
{
    //清空掉初始化串口使用之前的所有问题
    __HAL_UART_CLEAR_OREFLAG(&huart1);  // 清除串口ORE溢出标志位，避免重复触发溢出错误中断
    __HAL_UART_CLEAR_IDLEFLAG(&huart1); // 清除串口IDLE空闲标志位，清零本次帧结束标记，准备检测下一帧空闲

    rx_active = 0;
    bootloader_flash_reset(); // 清零写入状态（偏移/遗留字节/已擦除页标记）
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_buf[rx_active], BOOTLOADER_USART_REC_BUFF_LEN);
}

//空闲帧接收回调
//注意名字必须是 HAL_UARTEx_RxEventCallback（带 Ex）！HAL_UARTEx_ReceiveToIdle_DMA 在
//IDLE 事件时调用的是带 Ex 的这个，见 stm32f1xx_hal_uart.c 的 weak 默认实现。
//DMA 模式下 IDLE/TC 发生后 HAL 已停止本次接收（RxState→READY），必须在这里重新武装才能继续收下一帧。
//
//HAL 会对同一个 DMA 接收过程发三种事件回调（都走这个函数）：
//  (1) IDLE 事件：Size = 521 - CNDTR = 本段实际字节数（串口空闲）——真正的"一段数据结束"
//  (2) HT  事件：Size = 521/2 = 260（DMA 收到一半，中途通知）——DMA 仍在运行！
//  (3) TC  事件：Size = 521（缓冲区满，DMA 已停）——也算"一段结束"
//若对 HT 也切缓冲/重新武装，会与运行中的 DMA 冲突；若对 HT 累加 Size=260，会和后续 IDLE/TC 重复计数。
//修复：HT 时直接 return，DMA 继续跑，不动缓冲不累加。
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart,uint16_t Size)
{
    if (huart->Instance != USART1) return;

    //HT 事件只是 DMA 收到一半的中途通知，DMA 仍在运行：
    //此时切缓冲/重新武装会与运行中的 DMA 冲突，累加 Size 也会与后续 IDLE/TC 重复计数。
    //直接返回，让 DMA 继续接收直到 IDLE 或缓冲区满(TC)。
    if (huart->RxEventType == HAL_UART_RXEVENT_HT)
    {
        return;
    }

    //仅 IDLE / TC 走到这里：此时 DMA 已停(RxState=READY)，可安全切缓冲、重新武装、搬数据
    //刚收完的缓冲与长度
    uint8_t  done = rx_active;
    uint16_t len  = Size;

    //立刻切到另一个缓冲并重新武装 DMA 接收（微秒级）——
    //ISR 里只搬数据、不碰 flash；擦除/写入全部放到主循环，中断处理时间恒定，
    //任何波特率/帧间隙都不会丢字节。
    //DMA 重新武装后由 DMA 自主把下一段搬进另一个缓冲，CPU 不参与，比中断版更省 CPU。
    rx_active ^= 1;
    __HAL_UART_CLEAR_OREFLAG(&huart1);  //保险：清掉可能存在的溢出标志
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_buf[rx_active], BOOTLOADER_USART_REC_BUFF_LEN);

    //统计 / 进度
    bootloader_rec_len = len;
    bootloader_rec_full_len += len;
    dbg_frame_cnt++;
    dbg_last_size = len;
    if (len != 256 && dbg_short_size == 0) dbg_short_size = len;  //256=发送包大小

    //把本帧搬进暂存区，置标志；主循环 bootloader_process_frame() / App 状态机负责写 flash
    memcpy(frame_buf, rx_buf[done], len);
    frame_len  = len;
    frame_ready = 1;
}

//UART 错误回调（ORE 溢出等）。HAL F1 把 ORE 当"阻塞错误"，会关掉所有接收中断并置
//RxState=READY 且默认回调为空——若不管它，接收会永久停止。这里清标志后立即重武装，
//保证接收永不"死"；被溢出损坏的那一帧数据会少/错，由主机端校验兜底。
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) return;

    dbg_ore_cnt++;
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    __HAL_UART_CLEAR_IDLEFLAG(&huart1);
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_buf[rx_active], BOOTLOADER_USART_REC_BUFF_LEN);
}

//主循环轮询：有整帧待写则写入 flash。返回1=处理了一帧，0=无帧。
uint8_t bootloader_process_frame(void)
{
    if (!frame_ready) return 0;
    frame_ready = 0;
    bootloader_flash_write_frame(frame_buf, frame_len);
    return 1;
}

//帧访问接口（App_bootloader 状态机用）：只读/消费，不写 flash

// 查询是否有收到待处理的完整帧，1=有帧等待，0=无
uint8_t  bootloader_frame_pending(void)     { return frame_ready; }
// 获取待处理帧的数据缓冲区指针（只读，不能修改缓冲区内容）
const uint8_t *bootloader_frame_data(void)  { return frame_buf; }
// 获取待处理帧的有效字节长度
uint16_t bootloader_frame_size(void)        { return frame_len; }
// 标记当前帧已经处理完毕，清空帧就绪标志，准备接收下一帧
void     bootloader_frame_consume(void)     { frame_ready = 0; }

//按页检查/擦除并登记为已处理页（供 write_frame 和 flush 复用）
static void flash_ensure_page_erased(uint32_t addr)
{
    uint32_t page = addr & ~((uint32_t)FLASH_PAGE_SIZE - 1U);

    if (page != current_erased_page)
    {
        uint8_t need_erase = 0;
        for (uint32_t a = page; a < page + FLASH_PAGE_SIZE; a++)
            if (*(volatile uint8_t *)a != 0xFFU) { need_erase = 1; break; }
        if (need_erase)
        {
            FLASH_EraseInitTypeDef EraseInit = {0};
            uint32_t PageError = 0;
            EraseInit.TypeErase   = FLASH_TYPEERASE_PAGES;
            EraseInit.Banks       = FLASH_BANK_1;
            EraseInit.PageAddress = page;
            EraseInit.NbPages     = 1;
            HAL_FLASHEx_Erase(&EraseInit, &PageError);
        }
        current_erased_page = page;
    }
}

//传输完成后调用：若遗留了最后一个字节（总长奇数），把它补 0xFF 写入 flash，
//否则该字节永远不会写进去（write_frame 只写偶数个字节）。
void bootloader_flash_flush(void)
{
    if (!last_byte_flag) return;

    HAL_FLASH_Unlock();
    uint32_t addr = APP_FLASH_BASE_ADDR + write_offset;
    flash_ensure_page_erased(addr);
    //低字节=遗留字节，高字节=0xFF 补齐（不覆盖任何有效数据）
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr,
                      (uint16_t)((uint16_t)last_byte | 0xFF00U));
    write_offset += 2;
    last_byte      = 0;
    last_byte_flag = 0;
    HAL_FLASH_Lock();
}

//接收前清零写入状态（偏移/遗留字节/已擦除页标记）
void bootloader_flash_reset(void)
{
    write_offset = 0;
    current_erased_page = 0xFFFFFFFFU;
    last_byte = 0;
    last_byte_flag = 0;
}

//把一帧数据写入 flash：解锁 → 按页检查/擦除 → 半字编程 → 遗留字节 → 推进偏移 → 上锁。
//可被 App_bootloader 复用（传入它自己的缓冲与长度即可）。
void bootloader_flash_write_frame(const uint8_t *buf, uint16_t len)
{
    //1，解锁flash
    HAL_FLASH_Unlock();

    //2,本帧写入起始地址（write_offset 恒偶数，半字对齐）
    uint32_t write_addr = APP_FLASH_BASE_ADDR + write_offset;

    //3,把 [上帧遗留字节(若有)] + [本帧buf] 视为一条逻辑字节流。
    //   F1 只能半字编程，按2字节配对写入；若总长为奇数，末字节留给下一帧。
    uint8_t  lb_flag = last_byte_flag;                 //快照上帧遗留标志
    uint8_t  lb_val  = last_byte;                      //快照上帧遗留字节
    uint16_t total   = (uint16_t)lb_flag + len;
    uint16_t to_write = total & (uint16_t)~1U;          //本次实际写入字节数(恒偶数)
    uint8_t  new_last = 0, new_last_flag = 0;           //留给下一帧的遗留

    if (total & 1U) //总长为奇数：末字节成为新遗留
    {
        uint16_t idx = total - 1;                      //末字节在逻辑流中的下标
        new_last = (lb_flag && idx == 0)
                   ? lb_val
                   : buf[idx - lb_flag];
        new_last_flag = 1;
    }

    //取逻辑流第 p 字节：p==0 且有遗留 => 取遗留字节；否则取 buf[p-lb_flag]
    #define STREAM_BYTE(p) ((lb_flag && ((p)==0)) ? lb_val \
                        : buf[(p) - lb_flag])

    uint16_t p = 0;
    while (p < to_write)
    {
        flash_ensure_page_erased(write_addr);

        uint16_t halfword = (uint16_t)STREAM_BYTE(p)
                          | ((uint16_t)STREAM_BYTE(p + 1) << 8);
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, write_addr, halfword);

        write_addr += 2;
        p += 2;
    }
    #undef STREAM_BYTE

    //更新遗留字节，供下一帧使用
    last_byte      = new_last;
    last_byte_flag = new_last_flag;

    //4,推进 write_offset（恒偶数）
    write_offset = write_addr - APP_FLASH_BASE_ADDR;

    //5,上锁
    HAL_FLASH_Lock();
}

void bootloader_jump_to_app(void)
{
    typedef void (*pFunc)(void);

    //1，检验
    uint32_t app_start_ptr = *(volatile uint32_t *)APP_FLASH_BASE_ADDR;
    uint32_t app_reset_ptr = *(volatile uint32_t *)(APP_FLASH_BASE_ADDR + 4);

    if((app_start_ptr & 0xffff0000)!=APP_RESET_ADDR)
    {
        printf("栈顶地址错误\n");
        return;
    }

    if(app_reset_ptr < APP_FLASH_BASE_ADDR||app_reset_ptr >= APP_END_ADDR)
    {
        printf("重置地址错误\n");
        return;
    }

    //2，注销bootloader程序
    //关中断
    __disable_irq();
    /* 顺序敏感：HAL_RCC_DeInit() 内部会调用 HAL_InitTick()，若 HAL 时基用的是某个定时器，
     * 该函数会重启该定时器并重新使能其中断。因此下面的 NVIC 清理必须放在 HAL_RCC_DeInit() 之后执行；
     * 一旦调换顺序，被重启的中断会在跳进 app 后 __enable_irq() 把积压的中断派发给 app 向量表里的
     * 弱符号 Default_Handler（B . 死循环），app 当场卡死。本工程 HAL 时基用 SysTick，
     * 下面统一清掉所有 NVIC + 停 SysTick，对 SysTick/定时器两种时基都安全。 */

    HAL_RCC_DeInit();

    /* 关闭并清除所有外部中断，防止残留 pending 位把 app 带进 Default_Handler */
    for (uint32_t i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFU;   /* disable */
        NVIC->ICPR[i] = 0xFFFFFFFFU;   /* clear pending */
    }
    SysTick->CTRL = 0;                 /* 顺手停掉 SysTick */
    SysTick->VAL = 0;                  /* 重置 SysTick 值 */
    SysTick->LOAD = 0;                 /* 重置 SysTick 装载值 */
     //注销HAL库
     HAL_DeInit();
    //修改主栈指针
    __set_MSP(app_start_ptr);
    //重定向中断向量表
    SCB->VTOR = APP_FLASH_BASE_ADDR;
    //3，跳转A程序复位中断
    pFunc jump_to_app = (pFunc)app_reset_ptr;
    jump_to_app();
}
