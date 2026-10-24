#include "sys.h"
#include "usart.h"	  
#include "led.h"
////////////////////////////////////////////////////////////////////////////////// 	 
//如果使用ucos,则包括下面的头文件即可.
#if SYSTEM_SUPPORT_OS
#include "includes.h"					//ucos 使用	  
#endif
//////////////////////////////////////////////////////////////////////////////////	 
////////////////////////////////////////////////////////////////////////////////// 	  
 

//////////////////////////////////////////////////////////////////
//加入以下代码,支持printf函数,而不需要选择use MicroLIB	  
#if 1
#pragma import(__use_no_semihosting)             
//标准库需要的支持函数                 
struct __FILE 
{ 
	int handle; 

}; 

FILE __stdout;       
//定义_sys_exit()以避免使用半主机模式    
void _sys_exit(int x) 
{ 
	x = x; 
} 
//重定义fputc函数 
int fputc(int ch, FILE *f)
{   	
	while((USART1->SR&0X40)==0);//循环发送,直到发送完毕     
	USART1->DR = (u8) ch;
	return ch;
}
#endif 

/*使用microLib的方法*/
 /* 
int fputc(int ch, FILE *f)
{
	USART_SendData(USART1, (uint8_t) ch);

	while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET) {}	
   
    return ch;
}
int GetKey (void)  { 

    while (!(USART1->SR & USART_FLAG_RXNE));

    return ((int)(USART1->DR & 0x1FF));
}
*/
 
#if EN_USART1_RX   //如果使能了接收
//串口1中断服务程序
//注意,读取USARTx->SR能避免莫名其妙的错误   	
u8 USART_RX_BUF[USART_REC_LEN];     //接收缓冲,最大USART_REC_LEN个字节.
//接收状态
//bit15，	接收完成标志
//bit14，	接收到0x0d
//bit13~0，	接收到的有效字节数目
u16 USART_RX_STA=0;       //接收状态标记	  
// PA9  TX
// PA10 RX
void uart_init(u32 bound){
  //GPIO端口设置
  GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	 
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1|RCC_APB2Periph_GPIOA, ENABLE);	//使能USART1，GPIOA时钟
  
	//USART1_TX   GPIOA.9
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9; //PA.9
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;	//复用推挽输出
  GPIO_Init(GPIOA, &GPIO_InitStructure);//初始化GPIOA.9
   
  //USART1_RX	  GPIOA.10初始化
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;//PA10
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;//浮空输入
  GPIO_Init(GPIOA, &GPIO_InitStructure);//初始化GPIOA.10  

  //Usart1 NVIC 配置
  NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=3 ;//抢占优先级3
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;		//子优先级3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);	//根据指定的参数初始化VIC寄存器
  
   //USART 初始化设置

	USART_InitStructure.USART_BaudRate = bound;//串口波特率
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;//字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;//一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;//无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;	//收发模式

  USART_Init(USART1, &USART_InitStructure); //初始化串口1
  USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);//开启串口接受中断
  USART_Cmd(USART1, ENABLE);                    //使能串口1 

}

/* USART1 接收中断已移交 usart_protocol.c 中的 USART1_IRQHandler 统一处理;
   以下为旧的行缓冲接收逻辑, 停用保留仅作参考(不再是中断向量) */
void USART1_RxLineLegacy(void)                	//串口1中断服务程序
	{
		
	uint8_t Res;

    // 1. 处理接收数据中断
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
        Res = USART_ReceiveData(USART1);

        if ((USART_RX_STA & 0x8000) == 0)  // 接收未完成
        {
            if (USART_RX_STA & 0x4000)  // 已收到 0x0D
            {
                if (Res != 0x0A)
                    USART_RX_STA = 0;  // 接收错误，重新开始
                else
                    USART_RX_STA |= 0x8000;  // 接收完成
            }
            else  // 还没收到 0x0D
            {
                if (Res == 0x0D)
                    USART_RX_STA |= 0x4000;
                else
                {
                    USART_RX_BUF[USART_RX_STA & 0x3FFF] = Res;
                    USART_RX_STA++;
                    if (USART_RX_STA > (USART_REC_LEN - 1))
                        USART_RX_STA = 0;  // 数据错误，重新开始
                }
            }
        }
    }

    // 2. 【关键】处理溢出错误中断 —— 不加这段就会卡死
    if (USART_GetFlagStatus(USART1, USART_FLAG_ORE) != RESET)
    {
        USART_ReceiveData(USART1);  // 读 DR 清除 ORE 标志
    }
} 
#endif	


//u8 USART2_RX_BUF[USART2_MAX_RECV_LEN];     //接收缓冲,最大USART_REC_LEN个字节.
//u16 USART2_RX_LEN = 0;
//u8 USART2_RX_FLAG = 0;
// PA2  TX
// PA3 RX


/* ==================== USART3 初始化: PB10=TX, PB11=RX ====================
 * 与 USART1(uart_init) 并列, 供 usart_protocol.c 的 USART_Protocol_Init 调用
 * 此处仅做硬件配置(GPIO/USART/NVIC/使能RXNE); 接收中断服务 USART3_IRQHandler
 * 定义在 USER/usart_protocol.c
 */
void USART3_Init(u32 bound) {
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);	//使能GPIOB时钟

	//USART3_TX  GPIOB.10
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;	//复用推挽输出
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	//USART3_RX  GPIOB.11
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;	//浮空输入
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	//USART3 NVIC 配置
	NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=1 ;//抢占优先级1
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;		//子优先级0
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	//USART 初始化设置
	USART_InitStructure.USART_BaudRate = bound;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

	USART_Init(USART3, &USART_InitStructure);
	USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);	//开启接收中断
	USART_Cmd(USART3, ENABLE);
}


void USART2_Init(u32 bound) {
//GPIO端口设置
	LED0=0;
  GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);	//使能USART2，GPIOA时钟
  
	//USART1_TX   GPIOA.2
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2; //PA2
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;	//复用推挽输出
  GPIO_Init(GPIOA, &GPIO_InitStructure);//初始化GPIOA.2
   
  //USART1_RX	  GPIOA.10初始化
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;//PA3
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;//浮空输入
  GPIO_Init(GPIOA, &GPIO_InitStructure);//初始化GPIOA.3  

  //Usart1 NVIC 配置
  NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=2 ;//抢占优先级3
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;		//子优先级3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);	//根据指定的参数初始化VIC寄存器
  
   //USART 初始化设置

	USART_InitStructure.USART_BaudRate = bound;//串口波特率
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;//字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;//一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;//无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;	//收发模式

  USART_Init(USART2, &USART_InitStructure); //初始化串口1
	USART_ClearFlag(USART2, USART_FLAG_CTS);
  USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);//开启串口接受中断
  USART_Cmd(USART2, ENABLE);                    //使能串口1 
}

/* ==================== 分串口格式化打印(便于区分输出口) ====================
 * 不依赖全局 printf/fputc: 直接阻塞发送到一个指定 USART 的 TX 引脚
 */
void usart1_printf(const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	u16 i, len;

	va_start(ap, fmt);
	len = vsprintf(buf, fmt, ap);
	va_end(ap);
	for (i = 0; i < len; i++)
	{
		while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
		USART_SendData(USART1, buf[i]);
	}
	while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
}

void usart3_printf(const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	u16 i, len;

	va_start(ap, fmt);
	len = vsprintf(buf, fmt, ap);
	va_end(ap);
	for (i = 0; i < len; i++)
	{
		while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET);
		USART_SendData(USART3, buf[i]);
	}
	while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET);
}


/*
void USART2_SendByte(u8 data) 
{
   while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
	 USART_SendData(USART2, data);
}

void USART2_SendString(char *str)
{
	while(*str)
	{
		USART2_SendByte(*str++);
	}
}

void u2_printf(char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    USART2_SendString(buf);
}

void USART2_IRQHandler(void)                	//串口1中断服务程序
{
	u8 res;
	// 优先处理 ORE 溢出错误，否则中断风暴卡死
	 if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) != RESET)
   {
     USART_ReceiveData(USART2);  // 读 DR 清除 ORE
		 return; // 处理完错误直接退出，避免后续逻辑误判
   }
	if(USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)  //接收中断(接收到的数据必须是0x0d 0x0a结尾)
	{
		res=USART_ReceiveData(USART2);	//读取接收到的数据
		if(USART2_RX_LEN < USART2_MAX_RECV_LEN - 1)//接收到了0x0d
		{
				USART2_RX_BUF[USART2_RX_LEN++]=res;
		}
	}
	
	if(USART_GetITStatus(USART2, USART_IT_IDLE) != RESET)
	{
		USART2_RX_FLAG = 1;  // 置位接收完成标志
		USART_ReceiveData(USART2);  // 读 DR 清除 IDLE 标志
    USART2_RX_BUF[USART2_RX_LEN] = '\0';
	}
	 if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) != RESET)
   {
     USART_ReceiveData(USART2);  // 读 DR 清除 ORE
		 return; // 处理完错误直接退出，避免后续逻辑误判
   }

}
		 
*/
