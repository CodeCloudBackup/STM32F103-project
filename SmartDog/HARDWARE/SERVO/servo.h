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
    u8 head;     /* PB0 头   TIM3_CH3 */
    u8 tail;     /* PB1 尾   TIM3_CH4 */
    u8 leg_fl;   /* PB6 左前腿 TIM4_CH1 */
    u8 leg_fr;   /* PB7 右前腿 TIM4_CH2 */
    u8 leg_bl;   /* PB8 左后腿 TIM4_CH3 */
    u8 leg_br;   /* PB9 右后腿 TIM4_CH4 */
    StepMs t;    /* 本步保持时间 ms */
} ServoStep;

// PB0=头  PB1=尾  PB6/PB7/PB8/PB9=四足
void SG90_Servo_Init(void);
void SG90_Set_Angle(TIM_TypeDef* TIMx, u8 angle, const u8 ch);

/* 各舵机引脚独立控制接口(angle: 0~180度) */
void Servo_Head(u8 angle);    // PB0 头   TIM3_CH3
void Servo_Tail(u8 angle);    // PB1 尾   TIM3_CH4
void Servo_Leg_FL(u8 angle);  // PB6 左前腿 TIM4_CH1
void Servo_Leg_FR(u8 angle);  // PB7 右前腿 TIM4_CH2
void Servo_Leg_BL(u8 angle);  // PB8 左后腿 TIM4_CH3
void Servo_Leg_BR(u8 angle);  // PB9 右后腿 TIM4_CH4
void Servo_RunStep(const ServoStep *s);  /* 执行脚本单步 */

#endif

