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
    u8 head;     /* 头舵机已去掉(字段保留, 脚本中值被忽略) */
    u8 tail;     /* 尾巴已去掉(字段保留, 脚本中值被忽略) */
    u8 leg_fl;   /* PB1 左前脚 TIM3_CH4 */
    u8 leg_fr;   /* PA7 右前脚 TIM3_CH2 */
    u8 leg_bl;   /* PB0 左后脚 TIM3_CH3 */
    u8 leg_br;   /* PA6 右后脚 TIM3_CH1 */
    StepMs t;    /* 本步保持时间 ms */
} ServoStep;

// 四腿: PA6=右后  PA7=右前  PB0=左后  PB1=左前 (头/尾已去掉)

/* ==================== 四腿机械安装角度补偿 ====================
 * 各腿达到真实站立姿态所需的偏移量(叠加在逻辑角 90 之上):
 *   左前/左后: 90-3=87度站立   右前: 90-8=82度站立   右后: 90+8=98度站立
 * 补偿已在 Servo_Leg_xx() 底层自动叠加, 动作脚本仍按 90=站立 编写.
 */
#define LEG_FL_OFFSET  (+5)   /*-3 PB1 左前脚 */
#define LEG_FR_OFFSET  (-5)   /* -8 PA7 右前脚 */
#define LEG_BL_OFFSET  (-5)   /* -3 PB0 左后脚 */
#define LEG_BR_OFFSET  (+5)   /*+8 PA6 右后脚 */

void SG90_Servo_Init(void);
void SG90_Set_Angle(TIM_TypeDef* TIMx, u8 angle, const u8 ch);

/* 各舵机引脚独立控制接口(angle: 0~180度) */
void Servo_Head(u8 angle);    // 头舵机已去掉(空操作)
void Servo_Tail(u8 angle);    // 尾巴已去掉(空操作)
void Servo_Leg_FL(u8 angle);  // PB1 左前脚 TIM3_CH4
void Servo_Leg_FR(u8 angle);  // PA7 右前脚 TIM3_CH2
void Servo_Leg_BL(u8 angle);  // PB0 左后脚 TIM3_CH3
void Servo_Leg_BR(u8 angle);  // PA6 右后脚 TIM3_CH1
void Servo_RunStep(const ServoStep *s);  /* 设定脚本单步的目标角(不直接落位) */

/* ==================== 软件斜坡层 ====================
 * 脚本只写"目标角", 由 Servo_RampTick 按时间把当前角逐步逼近目标角,
 * 把 60<->120 的相位切换从瞬时冲击变成连续扫动, 消除前进顿挫.
 * 需在主循环每轮调用一次 Servo_RampTick()(内部自带时间节流). */
void Servo_RampInit(void);    /* 斜坡层复位: 当前角=目标角=站立90, 直接落位 */
void Servo_RampTick(void);    /* 主循环每轮调用, 推进各轴向目标角靠近一步 */
void Servo_SetRampStep(u8 deg); /* 运行时改斜坡步长: 小=慢扫抓地(驱动), 大=快扫打滑(回程) */

#endif

