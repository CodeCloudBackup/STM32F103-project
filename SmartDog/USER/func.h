#ifndef __FUNC_H
#define __FUNC_H

#include "sys.h"
#include "delay.h"
#include "usart.h"
#include "led.h"
#include "oled.h"
#include "servo.h"
#include "malloc.h"
#include "ble_uart.h"
#include "ble_protocol.h"


/* ==================== 动作状态机定义 ==================== */
typedef enum {
    ACTION_IDLE,          /* 空闲 */
    ACTION_LEG_RESET,     /* 四足复位中 */
    ACTION_CUSTOM,        /* 自定义动作中 */
    ACTION_SCRIPT,        /* 动作脚本执行中(步态/姿势) */
} ActionState;


// 动作
extern ActionState g_action_state;
extern uint32_t g_action_start_tick;
extern u8 g_action_param;

/* 表情绘制在expression.h(含5个expr_face_xxx及状态变量extern声明) */

/* 远端指令处理函数 */
void handle_leg_reset(void);				/* 四足复位处理 */
void handle_reboot(void);						/* 软重启指令处理*/
void handle_joystick(u8 dataX, u8 dataY);   	/* 遥杆移动指令处理*/
void handle_heading(u8 angle);		  /* 摇头指令处理 函数入参传递*/
u8 handle_action(void);   					/* 自定义动作指令处理 全局参数传递*/
void handle_action_cmd(u8 act_val);  /* CMD_ACTION动作码分发,启动对应脚本 */
/* 摇杆四个手势对应的步态接口(名字=手势,循环执行,直到新指令或5s超时停止):
 * Joy_Right=右滑前进  Joy_Left=左滑后退  Joy_Up=上推右转  Joy_Down=下拉左转 */
void Joy_Right(void);
void Joy_Left(void);
void Joy_Up(void);
void Joy_Down(void);
void handle_expression(void);				/* 表情调度:分发绘制并刷新 全局参数传递*/
void handle_expr_return(void);   /* 转弯停止1s后恢复原表情的轮询(主循环每轮调用) */
void handle_status_req(void);
void handle_sleep(void);						/* 待机:停止动作并进入睡眠表情 */
void handle_timeout_guard(u32 *last_valid_tick); /* 通信超时保护:5s停动作/10min待机 */

//////////////////////////
void Memory_Test(void);
void OLed_Test(void);
void Servo_Test(void);
void USART2_Test(void);
void Expression_Test(void);         /* 表情轮播测试 */

#endif

