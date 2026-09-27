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
		
	TIM_Int_Init(TIM3, 1999, 719);
	TIM_CH_Init(TIM3, 3);
	TIM_CH_Init(TIM3, 4);
		
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6|GPIO_Pin_7|GPIO_Pin_8|GPIO_Pin_9;       // 定时器TIM3的PWM输出通道3，TIM3_CH3
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; // 复用推挽输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure); // 初始化GPIO
	TIM_Int_Init(TIM4, 1999, 719);
	TIM_CH_Init(TIM4, 1);
	TIM_CH_Init(TIM4, 2);
	TIM_CH_Init(TIM4, 3);
	TIM_CH_Init(TIM4, 4);
}

void SG90_Set_Angle(TIM_TypeDef* TIMx, u8 angle, const u8 ch)
{
	if (angle > 180)

	
		angle = 180;
	u16 duty = 10 * (angle + 45) / 9; 
	if (ch == 1) {
		TIM_SetCompare1(TIMx, duty); // 设置通道1旋转   
	} else if (ch == 2) {
		TIM_SetCompare2(TIMx, duty); // 设置通道2旋转  
	}else if (ch == 3) {
		TIM_SetCompare3(TIMx, duty); // 设置通道3旋转     
	} else if (ch == 4) {
		TIM_SetCompare4(TIMx, duty); // 设置通道4旋转  
	}
}

/* ==================== 各舵机引脚独立控制接口 ====================
 * 引脚->定时器通道映射(均为默认映射):
 *   PB0 = TIM3_CH3  头      PB1 = TIM3_CH4  尾
 *   PB6 = TIM4_CH1  左前腿  PB7 = TIM4_CH2  右前腿
 *   PB8 = TIM4_CH3  左后腿  PB9 = TIM4_CH4  右后腿
 * angle: 0~180 度
 */
void Servo_Leg_FL(u8 angle)    { SG90_Set_Angle(TIM3, angle, 3); }  // PB0 左前腿
void Servo_Tail(u8 angle)    { SG90_Set_Angle(TIM3, angle, 4); }  // PB1 尾
void Servo_Head(u8 angle)  { SG90_Set_Angle(TIM4, angle, 1); }  // PB6 头
void Servo_Leg_BL(u8 angle)  { SG90_Set_Angle(TIM4, angle, 2); }  // PB7 左后腿
void Servo_Leg_FR(u8 angle)  { SG90_Set_Angle(TIM4, angle, 3); }  // PB8 右前腿
void Servo_Leg_BR(u8 angle)  { SG90_Set_Angle(TIM4, angle, 4); }  // PB9 右后腿

/* 执行脚本中的一步: 角度为0的轴保持不动,其余写入目标角 */
void Servo_RunStep(const ServoStep *s)
{
	if (s->head)  Servo_Head(s->head);
	if (s->tail)  Servo_Tail(s->tail);
	if (s->leg_fl) Servo_Leg_FL(s->leg_fl);
	if (s->leg_fr) Servo_Leg_FR(s->leg_fr);
	if (s->leg_bl) Servo_Leg_BL(s->leg_bl);
	if (s->leg_br) Servo_Leg_BR(s->leg_br);
}
