#include "oled.h"
#include "spi.h"
#include "delay.h"
#include "oledfont.h"
#include <stdio.h> 
#include <string.h>

static u8 OLED_GRAM[128][8];	

/* ========== ?????? ========== */

// PB10 RST      复位 拉低时初始化（复位），正常时拉高
// PB11 DC       命令/数据
// PB14 NSS/CS	  片选	拉低选中
void OLED_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);//配置GPIOB时钟	
	//软件引脚
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10|GPIO_Pin_11|GPIO_Pin_14;;//首先采用软件片选
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
	GPIO_Init(GPIOB,&GPIO_InitStructure);//先执行一下软件端口的初始化
	SPI2_Init();		   	//SPI2初始化
	GPIO_SetBits(GPIOB, GPIO_Pin_14); 
	// 显示屏初始化流程
	OLED_Reset();
	// 关闭显示
  OLED_WR_Cmd(SSD1306_DISPLAY_OFF);
	// 设置时钟因子，震荡频率
  OLED_WR_Cmd(SSD1306_SET_DISPLAY_CLK_DIV);
  OLED_WR_Cmd(0x80);     // ????
    
    // ???????
    OLED_WR_Cmd(SSD1306_SET_MUX_RATIO);
    OLED_WR_Cmd(0x3F);     // 64?
    
    // ??????
    OLED_WR_Cmd(SSD1306_SET_DISPLAY_OFFSET);
    OLED_WR_Cmd(0x00);     // ???
    
    // ???????
    OLED_WR_Cmd(SSD1306_SET_DISPLAY_START_LINE);
    
    // ?????(??!???????)
    OLED_WR_Cmd(SSD1306_SET_CHARGE_PUMP);
    OLED_WR_Cmd(0x14);     // ?????
    
    // ?????????????
    OLED_WR_Cmd(SSD1306_MEMORY_ADDR_MODE);
    OLED_WR_Cmd(0x00);     // ??????
    
    // ??????(????)
    OLED_WR_Cmd(SSD1306_SET_SEGMENT_REMAP);
    
    // ??COM????(????)
    OLED_WR_Cmd(SSD1306_SET_COM_SCAN_DIR);
    
    // ??COM??????
    OLED_WR_Cmd(SSD1306_SET_COM_PINS);
    OLED_WR_Cmd(0x12);     // ??COM??
    
    // ?????
    OLED_WR_Cmd(SSD1306_SET_CONTRAST);
    OLED_WR_Cmd(0xCF);     // ???=207
    
    // ???????
    OLED_WR_Cmd(SSD1306_SET_PRECHARGE_PERIOD);
    OLED_WR_Cmd(0xF1);     // ???=15??,??=1??
    
    // ??VCOMH??
    OLED_WR_Cmd(SSD1306_SET_VCOMH_DESELECT);
    OLED_WR_Cmd(0x40);     // �0.77�VCC
    
    // ????(???)
    OLED_WR_Cmd(SSD1306_NORMAL_DISPLAY);
     OLED_WR_Cmd(SSD1306_DISPLAY_ON);
    // ????
    OLED_Clear();
    OLED_Refresh();
}

void OLED_Reset(void)
{
	OLED_RST_HIGH();
	delay_ms(100);
	OLED_RST_LOW();
	delay_ms(100);
	OLED_RST_HIGH();
	delay_ms(100);
}

void OLED_Send_Byte(u8 byte)
{
	OLED_CS_LOW();                                       // 拉低片选，D/C#在下降沿被锁存，决定本字节是命令还是数据
	while(SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE)==RESET);
	SPI_I2S_SendData(SPI2, byte);
	while(SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY)==SET);  // 等待移位结束
	OLED_CS_HIGH();                                      // 释放片选，形成每字节的帧边界
}

void OLED_WR_Cmd(u8 cmd) 
{
	OLED_DC_LOW();
	OLED_Send_Byte(cmd);
	OLED_DC_HIGH();
}

void OLED_WR_Byte(u8 dat)
{
	OLED_DC_HIGH();
	OLED_Send_Byte(dat);
	OLED_DC_HIGH();
}

void OLED_Color_Turn(u8 flag)
{
	  if (flag)
			OLED_WR_Cmd(SSD1306_INVERSE_DISPLAY);
		else 
			OLED_WR_Cmd(SSD1306_NORMAL_DISPLAY);
}

void OLED_DisplayTurn(uint8_t i)
{
	if(i==0)
		{
			OLED_WR_Cmd(0xC0);//正常显示
			OLED_WR_Cmd(0xA0);
		}
	if(i==1)
		{
			OLED_WR_Cmd(0xC8);//旋转180度
			OLED_WR_Cmd(0xA1);
		}
}

void OLED_Display_On(void)
{
	// 0x8D  0x14  0xAF
	OLED_WR_Cmd(SSD1306_SET_CHARGE_PUMP);
	OLED_WR_Cmd(SSD1306_OPEN_CHARGE_PUMP);
	OLED_WR_Cmd(SSD1306_DISPLAY_ON);
}	

void OLED_Display_Off(void)
{
	// 0x8D  0x10  0xAE
	OLED_WR_Cmd(SSD1306_SET_CHARGE_PUMP);
	OLED_WR_Cmd(SSD1306_CLOSE_CHARGE_PUMP);
	OLED_WR_Cmd(SSD1306_DISPLAY_OFF);
}

void OLED_Refresh(void)
{
	 uint8_t i,n;
    for (i = 0; i < 8; i++) {
        OLED_WR_Cmd(0xB0 + i);
        OLED_WR_Cmd(0x00);       // ????
        OLED_WR_Cmd(0x10);       // ????
				for (n=0; n< 128; n++)
					OLED_WR_Byte(OLED_GRAM[n][i]);
    }
}	

void OLED_Clear(void) 
{
	uint8_t i,n;
	for(i=0;i<8;i++)
	{
	   for(n=0;n<128;n++)
			{
			 OLED_GRAM[n][i]=0;//清楚所有数据
			}
  }
	OLED_Refresh();//刷新
}

// 
void OLED_SetContrast(uint8_t value)
{
    OLED_WR_Cmd(SSD1306_SET_CONTRAST);
    OLED_WR_Cmd(value);
}

void OLED_DrawPoint(u8 x,u8 y,u8 t)
{
	uint8_t page, n, temp=0;
	//	if(x>127||y>63) return;如果不用汉字滚动，可以取消注释，并把显存缓存减少为128x8
	page=y/8;//判断该点的页码，这里的8指共有8页
	n=y%8;//判断该点位于所在列的第几个位，这里的8指每页的一列共有8个像素位
	temp=1<<n;//令temp的第n位 = 1，因此这个temp即为此时这一列的8bit长度的data
	if(t)  OLED_GRAM[x][page]|=temp;//因此这个n即为此时这一列的8bit长度的data
	else   OLED_GRAM[x][page]&=~temp;
}

void OLED_DrawCircle(uint8_t x, uint8_t y, uint8_t r)
{//Bresenham算法画圆
	uint8_t dx, dy; 
	int16_t delta = 0;//特别注意！！这里一定要是有符号数，因为要和0比较
    dx = 0;//理解成
    dy = r;
    while(2 * dy * dy >= r * r)//精细度调节，dy前的系数越大越精细,画大圆没区别,画小圆区别明显
    {
        OLED_DrawPoint(x + dx, y - dy,1);//把一个圆周以圆心为中心对称划分为八个区域，
        OLED_DrawPoint(x - dx, y - dy,1);//再利用对称的特性去逐个标点
        OLED_DrawPoint(x - dx, y + dy,1);//第一轮中，为圆周与坐标轴的四个交点
        OLED_DrawPoint(x + dx, y + dy,1);//接下来，把dx++这个操作理解成逐个找点的步骤
 
        OLED_DrawPoint(x + dy, y + dx,1);//也就是δx(或δy),步进值为1
        OLED_DrawPoint(x + dy, y - dx,1);
        OLED_DrawPoint(x - dy, y - dx,1);
        OLED_DrawPoint(x - dy, y + dx,1);
        
        dx++;
		
        delta = (dx * dx + dy * dy) - r*r;//勾股定理计算所画点到圆心距离和半径的大小关系
        if(delta > 0)//判断所画点和圆心的距离，不能大于半径
        {
            dy--;//如果大了，就说明dy也要跟着缩小
            if(delta > 0) dx--;//如果还大，说明dx不能再增大了
        }
    }
}


void OLED_ShowChar(u8 x,u8 y,u8 chr,u8 size,u8 mode)
{
	uint8_t i, dot_n, temp, byte_len, chr_n;//chr_n为字符数组的下标
	uint8_t x0 = x,y0 = y;//这里用于暂存起始显示的像素点(x0,y0)
	if(size == 8) byte_len = 6;
	else byte_len = (size/8+((size%8)?1:0))*(size/2); //计算不同字体一个字符的字节长度
	chr_n = chr-' ';  //去除偏移量,这样chr_n才能代表字符数组的下标, " "的ASCII值为20
	for(i=0; i<byte_len; i++)
	{
		if(size==12)
			{temp=asc2_1206[chr_n][i];} //调用1206字体
		else if(size==16)
			{temp=asc2_1608[chr_n][i];} //调用1608字体
		else if(size==24)
			{temp=asc2_2412[chr_n][i];} //调用2412字体
		else return;
		//逐点点亮，实现跨页码
		for(dot_n=0; dot_n<8; dot_n++)//dot_n代表更新显示一个字符时,要按每列从0~7的顺序依次更新
		{
			if(temp&0x01)OLED_DrawPoint(x,y,mode);
			else OLED_DrawPoint(x,y,!mode);
			temp>>=1;//先更新低位，所以依次右移1位和0x01去比较
			y++;//继续下一个dot
		}
		x++;//更新完一列的8个dot(也就是一个字节)后,继续下一列(即下一个字节)
		if((size != 8)&&((x-x0) == size/2))//对于除0806以外的字体，其高度超过了8dot，即超过了一页
			{x=x0;y0=y0+8;}//因此继续更新下一页的内容，起始横坐标复原，起始纵坐标+8
		y=y0;
  }
}

u32 mypow(u8 m,u8 n)
{
	u32 result=1;	 
	while(n--)result*=m;    
	return result;
}		

void OLED_ShowNum(u8 x,u8 y,u32 num,u8 len,u8 size)
{
	u8 t,temp;
	u8 enshow=0;						   
	for(t=0;t<len;t++)
	{
		temp=(num/mypow(10,len-t-1))%10;
		if(enshow==0&&t<(len-1))
		{
			if(temp==0)
			{
				OLED_ShowChar(x+(size/2)*t,y,' ',size,1);
				continue;
			}else enshow=1; 
		 	 
		}
	 	OLED_ShowChar(x+(size/2)*t,y,temp+'0',size,1); 
	}
}
	
/**
  * @brief  显示字符串
  * @param  (x,y)---字符串起始位置;*str---要显示的字符串起始地址;
			size_h---选择字体 6x8/6x12/8x16/12x24,可取值:8,12,16,24
			mode---显示模式：1为正常，0为反色(关闭像素点)
			w---调整字符间距,最小为0
            注意x:0~127,y:0~63
  * @retval 无
  */
void OLED_ShowString(u8 x,u8 y,const u8 *p,u8 size)
{
	  while((*p<='~')&&(*p>=' '))//ÅÐ¶ÏÊÇ²»ÊÇ·Ç·¨×Ö·û!
    {       
        if(x>(128-(size/2))){x=0;y+=size;}
        if(y>(64-size)){y=x=0;OLED_Clear();}
        OLED_ShowChar(x,y,*p,size,1);	 
        x+=size/2;
        p++;
    } 
}

void OLED_Fill(u8 x1,u8 y1,u8 x2,u8 y2,u8 dot)
{
		u8 x,y;  
	for(x=x1;x<=x2;x++)
	{
		for(y=y1;y<=y2;y++)OLED_DrawPoint(x,y,dot);
	}													    
	OLED_Refresh();
}

