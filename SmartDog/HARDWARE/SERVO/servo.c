#include "servo.h"

// PB0 PB1
// PB6、PB7、PB8、PB9
void SG90_Servo_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);//配置PROTB时钟	
	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0|GPIO_Pin_1;       // 定时器TIM3的PWM输出通道3，TIM3_CH3
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; // 复用推挽输出
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOB, &GPIO_InitStructure); // 初始化GPIO
	
	TIM3_Int_Init(1999, 719);
	TIM3_CH_Init(3);
	TIM3_CH_Init(4);
}

void SG90_Set_Angle(u8 angle, const u8 ch)
{
	if (angle > 180)
		angle = 180;
	u16 duty = 10 * (angle + 45) / 9; 
	if (ch == 1) {
		TIM_SetCompare1(TIM3, duty); // 设置通道1旋转   
	} else if (ch == 2) {
		TIM_SetCompare2(TIM3, duty); // 设置通道2旋转  
	}else if (ch == 3) {
		TIM_SetCompare3(TIM3, duty); // 设置通道3旋转     
	} else if (ch == 4) {
		TIM_SetCompare4(TIM3, duty); // 设置通道4旋转  
	}
}
