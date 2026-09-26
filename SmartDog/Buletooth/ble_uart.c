#include "ble_uart.h"

// PA2 TX
// PA3 RX
void BLE_UART_Init(u32 baudrate)
{
	GPIO_InitTypeDef GPIO_InitStructure;
  USART_InitTypeDef USART_InitStructure;

   // 1. 开启 USART2 和 GPIOA 时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

	// 2. 配置 PA2 为复用推挽输出，50MHz
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;//设置PA2引脚
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP ;//设置输出模式为复用推挽输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz ;//设置输出速度为50MHZ
	GPIO_Init(GPIOA,&GPIO_InitStructure);//初始化
	// 3. 配置 PA3 为上拉输入
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;//设置PA3引脚
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING ;//设置输出模式为上拉输入
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz ;//设置输出速度为50MHZ
	GPIO_Init(GPIOA,&GPIO_InitStructure);//初始化
	
	// USART2 
	USART_InitStructure.USART_BaudRate = baudrate;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx|USART_Mode_Tx;;  // ???
	USART_Init(USART2, &USART_InitStructure);
	USART_Cmd(USART2, ENABLE);
	
	// 开启接收中断和空闲中断
	USART_ITConfig(USART2,USART_IT_RXNE,ENABLE);   
	//USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);

  NVIC_InitTypeDef NVIC_InitStructure; //定义结构体
	NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn; //根据上面的我们所选取的USART2
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;//这里选择的是抢占 1
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0; //这里选择的是响应2
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; //使能指定的中断通道
  //初始化函数↓
  NVIC_Init(&NVIC_InitStructure);
}

/* ==================== 发送函数 ==================== */
void BLE_UART2_SendByte(u8 data)
{
	while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
  USART_SendData(USART2, data);
}

