#include "servo.h"

// 四腿 TIM3 PWM 引脚(默认映射,无需重映射):
//   PA6=TIM3_CH1 右后BR   PA7=TIM3_CH2 右前FR
//   PB0=TIM3_CH3 左后BL   PB1=TIM3_CH4 左前FL
// 头舵机与尾巴已去掉, 不再初始化 PB6/PB7/PB8/PB9/TIM4
void SG90_Servo_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	// GPIOA(PA6/PA7) + GPIOB(PB0/PB1) 时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);

	// PA6(右后BR/TIM3_CH1), PA7(右前FR/TIM3_CH2) 复用推挽输出
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	// PB0(左后BL/TIM3_CH3), PB1(左前FL/TIM3_CH4) 复用推挽输出
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	// TIM3 四通道 PWM
	TIM_Int_Init(TIM3, 1999, 719);
	TIM_CH_Init(TIM3, 1);
	TIM_CH_Init(TIM3, 2);
	TIM_CH_Init(TIM3, 3);
	TIM_CH_Init(TIM3, 4);
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

/* 叠加补偿并将结果限定在 0~180 (避免u8回绕) */
static u8 Leg_Compensate(u8 angle, int offset)
{
	int a = (int)angle + offset;
	if (a < 0)   a = 0;
	if (a > 180) a = 180;
	return (u8)a;
}

/* ==================== 各舵机引脚独立控制接口 ====================
 * 引脚->定时器通道映射(均为默认映射):
 *   PA6 = TIM3_CH1  右后脚   PA7 = TIM3_CH2  右前脚
 *   PB0 = TIM3_CH3  左后脚   PB1 = TIM3_CH4  左前脚
 *   头舵机/尾巴已去掉, Servo_Head/Servo_Tail 变为空操作(不再驱动引脚)
 * angle: 0~180 度
 * 注意: 四条腿接口内的 angle 为逻辑角(90=站立中位),
 *       底层会自动叠加各腿机械安装补偿偏移(见 servo.h LEG_xxx_OFFSET),
 *       因此动作脚本无需关心补偿值.
 */
void Servo_Leg_BR(u8 angle)   { SG90_Set_Angle(TIM3, Leg_Compensate(angle, LEG_BR_OFFSET), 1); }  // PA6 右后脚 补偿+8: 90->98度站立
void Servo_Leg_FR(u8 angle)   { SG90_Set_Angle(TIM3, Leg_Compensate(angle, LEG_FR_OFFSET), 2); }  // PA7 右前脚 补偿-8: 90->82度站立
void Servo_Leg_BL(u8 angle)   { SG90_Set_Angle(TIM3, Leg_Compensate(angle, LEG_BL_OFFSET), 3); }  // PB0 左后脚 补偿-3: 90->87度站立
void Servo_Leg_FL(u8 angle)   { SG90_Set_Angle(TIM3, Leg_Compensate(angle, LEG_FL_OFFSET), 4); }  // PB1 左前脚 补偿-3: 90->87度站立
void Servo_Head(u8 angle)     { (void)angle; }  // 头舵机已去掉, 空操作
void Servo_Tail(u8 angle)     { (void)angle; }  // 尾巴已去掉, 空操作

/* ==================== 软件斜坡层 ====================
 * 六个执行器的逻辑角(90=站立)当前值与目标值:
 *   索引 [0]头 [1]尾 [2]左前FL [3]右前FR [4]左后BL [5]右后BR
 * Servo_RunStep 只更新目标角, Servo_RampTick 每 SERVO_RAMP_TICK_MS
 * 把当前角向目标角推进 SERVO_RAMP_STEP 度, 使相位切换变成连续扫动.
 * 写出时仍走 Servo_Leg_xx 以自动叠加各腿安装补偿. */
#define SERVO_RAMP_TICK_MS  15   /* 斜坡刷新周期(约66Hz) */
#define SERVO_RAMP_STEP      5   /* 默认每周期最大变化角度(度): 60°行程约180ms */

static u8  s_cur[6];             /* 当前逻辑角 */
static u8  s_tgt[6];             /* 目标逻辑角 */
static u8  s_ramp_step = SERVO_RAMP_STEP;  /* 运行时步长: 小=慢扫抓地, 大=快扫打滑 */
static u32 s_ramp_last = 0;      /* 上次推进时刻 */

/* 运行时设置斜坡步长(度/周期): 用于非对称摆速(慢扫驱动/快扫回程). 0按1处理 */
void Servo_SetRampStep(u8 deg)
{
	s_ramp_step = deg ? deg : 1;
}

/* 单轴向目标推进一步(最多 s_ramp_step 度), 收敛后正好等于目标 */
static u8 ramp_move(u8 cur, u8 tgt)
{
	if (cur < tgt)
	{
		u8 d = tgt - cur;
		cur += (d > s_ramp_step) ? s_ramp_step : d;
	}
	else if (cur > tgt)
	{
		u8 d = cur - tgt;
		cur -= (d > s_ramp_step) ? s_ramp_step : d;
	}
	return cur;
}

/* 斜坡层复位: 当前角=目标角=站立, 立即落位且不产生扫动 */
void Servo_RampInit(void)
{
	u8 i;
	for (i = 0; i < 6; i++)
		s_cur[i] = s_tgt[i] = 90;
	Servo_Head(s_cur[0]);
	Servo_Tail(s_cur[1]);
	Servo_Leg_FL(s_cur[2]);
	Servo_Leg_FR(s_cur[3]);
	Servo_Leg_BL(s_cur[4]);
	Servo_Leg_BR(s_cur[5]);
	s_ramp_step = SERVO_RAMP_STEP;   /* 步长复位为默认 */
	s_ramp_last = g_sys_tick_ms;
}

/* 主循环每轮调用: 到节流周期就把各轴当前角向目标角推进一步并写出 */
void Servo_RampTick(void)
{
	if ((u32)(g_sys_tick_ms - s_ramp_last) < SERVO_RAMP_TICK_MS)
		return;
	s_ramp_last = g_sys_tick_ms;

	if (s_cur[0] != s_tgt[0]) { s_cur[0] = ramp_move(s_cur[0], s_tgt[0]); Servo_Head(s_cur[0]);   }
	if (s_cur[1] != s_tgt[1]) { s_cur[1] = ramp_move(s_cur[1], s_tgt[1]); Servo_Tail(s_cur[1]);   }
	if (s_cur[2] != s_tgt[2]) { s_cur[2] = ramp_move(s_cur[2], s_tgt[2]); Servo_Leg_FL(s_cur[2]); }
	if (s_cur[3] != s_tgt[3]) { s_cur[3] = ramp_move(s_cur[3], s_tgt[3]); Servo_Leg_FR(s_cur[3]); }
	if (s_cur[4] != s_tgt[4]) { s_cur[4] = ramp_move(s_cur[4], s_tgt[4]); Servo_Leg_BL(s_cur[4]); }
	if (s_cur[5] != s_tgt[5]) { s_cur[5] = ramp_move(s_cur[5], s_tgt[5]); Servo_Leg_BR(s_cur[5]); }
}

/* 执行脚本中的一步: 只更新目标角(角度为0表示本步维持上一个目标),
 * 真正落位由 Servo_RampTick 逐步逼近完成, 从而消除 60<->120 硬跳变 */
void Servo_RunStep(const ServoStep *s)
{
	if (s->head)   s_tgt[0] = s->head;
	if (s->tail)   s_tgt[1] = s->tail;
	if (s->leg_fl) s_tgt[2] = s->leg_fl;
	if (s->leg_fr) s_tgt[3] = s->leg_fr;
	if (s->leg_bl) s_tgt[4] = s->leg_bl;
	if (s->leg_br) s_tgt[5] = s->leg_br;
}
