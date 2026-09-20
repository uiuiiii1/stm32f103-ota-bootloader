#ifndef __OLED_H
#define __OLED_H

#include <stdint.h>
#include "OLED_Data.h"

/*参数宏定义*********************/

/*FontSize参数取值*/
/*此参数值不仅用于判断，而且用于计算横向字符偏移，默认值为字体像素宽度*/
#define OLED_8X16				8
#define OLED_6X8				6

/*IsFilled参数数值*/
#define OLED_UNFILLED			0
#define OLED_FILLED				1

/*********************参数宏定义*/


/*函数声明*********************/

/*初始化函数*/
void OLED_Init(void);

/*更新函数*/
void OLED_Update(void);
void OLED_UpdateArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height);

/*显存控制函数*/
void OLED_Clear(void);
void OLED_ClearArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height);
void OLED_Reverse(void);
void OLED_ReverseArea(int16_t X, int16_t Y, uint8_t Width, uint8_t Height);

/*显示函数*/

// 在OLED上显示单个字符
// 参数：X-横坐标, Y-纵坐标, Char-字符, FontSize-字体大小(通常6x8/8x16)
void OLED_ShowChar(int16_t X, int16_t Y, char Char, uint8_t FontSize);
// 在OLED上显示字符串
// 参数：X-横坐标, Y-纵坐标, String-字符串指针, FontSize-字体大小
void OLED_ShowString(int16_t X, int16_t Y, char *String, uint8_t FontSize);
// 显示无符号整数
// 参数：X-横坐标, Y-纵坐标, Number-要显示的数字, Length-显示位数, FontSize-字体大小
void OLED_ShowNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);
// 显示有符号整数（可显示负数）
// 参数：X-横坐标, Y-纵坐标, Number-有符号整数, Length-显示位数, FontSize-字体大小
void OLED_ShowSignedNum(int16_t X, int16_t Y, int32_t Number, uint8_t Length, uint8_t FontSize);
// 以十六进制格式显示数字
// 参数：X-横坐标, Y-纵坐标, Number-要显示的数字, Length-十六进制位数, FontSize-字体大小
void OLED_ShowHexNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);
// 以二进制格式显示数字
// 参数：X-横坐标, Y-纵坐标, Number-要显示的数字, Length-二进制位数, FontSize-字体大小
void OLED_ShowBinNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);
// 显示浮点数
// 参数：X-横坐标, Y-纵坐标, Number-浮点数, IntLength-整数部分位数, FraLength-小数部分位数, FontSize-字体大小
void OLED_ShowFloatNum(int16_t X, int16_t Y, double Number, uint8_t IntLength, uint8_t FraLength, uint8_t FontSize);
// 显示图片（位图
// 参数：X-横坐标, Y-纵坐标, Width-图片宽度(像素), Height-图片高度(像素), Image-图片数据数组指针
void OLED_ShowImage(int16_t X, int16_t Y, uint8_t Width, uint8_t Height, const uint8_t *Image);
// 格式化输出（类似printf函数）
// 参数：X-横坐标, Y-纵坐标, FontSize-字体大小, format-格式化字符串, ...-可变参数
void OLED_Printf(int16_t X, int16_t Y, uint8_t FontSize, char *format, ...);

/*绘图函数*/
void OLED_DrawPoint(int16_t X, int16_t Y);
uint8_t OLED_GetPoint(int16_t X, int16_t Y);
void OLED_DrawLine(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1);
void OLED_DrawRectangle(int16_t X, int16_t Y, uint8_t Width, uint8_t Height, uint8_t IsFilled);
void OLED_DrawTriangle(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1, int16_t X2, int16_t Y2, uint8_t IsFilled);
void OLED_DrawCircle(int16_t X, int16_t Y, uint8_t Radius, uint8_t IsFilled);
void OLED_DrawEllipse(int16_t X, int16_t Y, uint8_t A, uint8_t B, uint8_t IsFilled);
void OLED_DrawArc(int16_t X, int16_t Y, uint8_t Radius, int16_t StartAngle, int16_t EndAngle, uint8_t IsFilled);

/*********************函数声明*/

#endif

