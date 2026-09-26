#ifndef __OLED_H
#define __OLED_H

#include "sys.h"

//OLED�����ú���
/* ==================== */
#define OLED_WIDTH          128     // ????(??)
#define OLED_HEIGHT         64      // ????(??)
#define OLED_PAGE_NUM       8       // ??(HEIGHT/8)
#define OLED_BUFFER_SIZE    1024    // ????(128�64/8)
/* ========== SSD1306 ?????? ========== */
#define SSD1306_DISPLAY_OFF             0xAE
#define SSD1306_DISPLAY_ON              0xAF   //点亮屏幕
#define SSD1306_SET_CONTRAST            0x81
#define SSD1306_NORMAL_DISPLAY          0xA6
#define SSD1306_INVERSE_DISPLAY         0xA7
#define SSD1306_SET_MUX_RATIO           0xA8
#define SSD1306_SET_DISPLAY_OFFSET      0xD3
#define SSD1306_SET_DISPLAY_START_LINE  0x40
#define SSD1306_SET_SEGMENT_REMAP       0xA1
#define SSD1306_SET_COM_SCAN_DIR        0xC8
#define SSD1306_SET_COM_PINS            0xDA
#define SSD1306_SET_VCOMH_DESELECT      0xDB
#define SSD1306_SET_DISPLAY_CLK_DIV     0xD5	// 设置时钟分频因子，震荡频率
#define SSD1306_SET_PRECHARGE_PERIOD    0xD9
#define SSD1306_SET_CHARGE_PUMP         0x8D  //电荷泵使能
#define SSD1306_MEMORY_ADDR_MODE        0x20  
#define SSD1306_SET_COL_ADDR            0x21
#define SSD1306_SET_PAGE_ADDR           0x22
#define SSD1306_SET_PAGE_START          0xB0
#define SSD1306_CLOSE_CHARGE_PUMP       0x10	//关闭电荷泵
#define SSD1306_OPEN_CHARGE_PUMP        0x14	//开启电荷泵

// PB10 RST      复位
// PB11 DC       命令/数据
// PB12 NSS/CS	  片选	
#define OLED_RST_LOW()  GPIO_ResetBits(GPIOB, GPIO_Pin_10);
#define OLED_RST_HIGH() GPIO_SetBits(GPIOB, GPIO_Pin_10);
#define OLED_DC_LOW()  GPIO_ResetBits(GPIOB, GPIO_Pin_11);
#define OLED_DC_HIGH() GPIO_SetBits(GPIOB, GPIO_Pin_11);
#define OLED_CS_LOW() 	GPIO_ResetBits(GPIOB, GPIO_Pin_14);
#define OLED_CS_HIGH() 	GPIO_SetBits(GPIOB, GPIO_Pin_14);


void OLED_Init(void);
void OLED_Reset(void);
void OLED_Send_Byte(u8 byte);
void OLED_WR_Byte(u8 dat);  
void OLED_WR_Cmd(u8 cmd);
void OLED_Color_Turn(u8 flag);
void OLED_DisplayTurn(uint8_t i);
void OLED_Display_On(void);
void OLED_Display_Off(void);
void OLED_Refresh(void); 
void OLED_Clear(void);
void OLED_SetContrast(uint8_t value);
void OLED_DrawPoint(u8 x,u8 y,u8 t);
void OLED_DrawCircle(uint8_t x, uint8_t y, uint8_t r);
void OLED_ShowChar(u8 x,u8 y,u8 chr,u8 size,u8 mode);
void OLED_ShowNum(u8 x,u8 y,u32 num,u8 len,u8 size);
void OLED_ShowString(u8 x,u8 y,const u8 *p,u8 size);
void OLED_Fill(u8 x1,u8 y1,u8 x2,u8 y2,u8 dot);

#endif
