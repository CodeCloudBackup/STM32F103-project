#ifndef __SERVO_H
#define __SERVO_H

#include "sys.h"
#include "timer.h"

/* ==================== 动作脚本步骤定义 ====================
 * 一步 = 六个执行器的目标角度 + 到位保持时间
 * 角度约定: 90=中位(站立), 120=前/抬起, 60=后/压下 (可按实际安装方向微调)
 * 0 表示该执行器本步不动, 维持上一步角度
 */
typedef u16 StepMs;

typedef struct {
    u8 head;     /* PB1 头   TIM3_CH4 (未重新指定,沿用空闲通道) */
    u8 tail;     /* PB7 尾   TIM4_CH2 */
    u8 leg_fl;   /* PB8 左前脚 TIM4_CH3 */
    u8 leg_fr;   /* PB6 右前脚 TIM4_CH1 */
    u8 leg_bl;   /* PB9 左后脚 TIM4_CH4 */
    u8 leg_br;   /* PB0 右后脚 TIM3_CH3 */
    StepMs t;    /* 本步保持时间 ms */
} ServoStep;

// PB0=右后脚  PB1=头  PB6=右前脚  PB7=尾巴  PB8=左前脚  PB9=左后脚

/* ==================== 四腿机械安装角度补偿 ====================
 * 各腿达到真实站立姿态所需的偏移量(叠加在逻辑角 90 之上):
 *   左前/左后: 90-3=87度站立   右前: 90-8=82度站立   右后: 90+8=98度站立
 * 补偿已在 Servo_Leg_xx() 底层自动叠加, 动作脚本仍按 90=站立 编写.
 */
#define LEG_FL_OFFSET  (+5)   /*-3 PB8 左前脚 */
#define LEG_FR_OFFSET  (-5)   /* -8 PB6 右前脚 */
#define LEG_BL_OFFSET  (-5)   /* -3 PB9 左后脚 */
#define LEG_BR_OFFSET  (+5)   /*+8 PB0 右后脚 */

void SG90_Servo_Init(void);
void SG90_Set_Angle(TIM_TypeDef* TIMx, u8 angle, const u8 ch);

/* 各舵机引脚独立控制接口(angle: 0~180度) */
void Servo_Head(u8 angle);    // PB1 头   TIM3_CH4
void Servo_Tail(u8 angle);    // PB7 尾巴 TIM4_CH2
void Servo_Leg_FL(u8 angle);  // PB8 左前脚 TIM4_CH3
void Servo_Leg_FR(u8 angle);  // PB6 右前脚 TIM4_CH1
void Servo_Leg_BL(u8 angle);  // PB9 左后脚 TIM4_CH4
void Servo_Leg_BR(u8 angle);  // PB0 右后脚 TIM3_CH3
void Servo_RunStep(const ServoStep *s);  /* 执行脚本单步 */

#endif

