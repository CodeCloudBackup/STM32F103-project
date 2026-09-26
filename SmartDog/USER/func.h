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
} ActionState;


// 动作
extern ActionState g_action_state;
extern uint32_t g_action_start_tick;
extern u8 g_action_param;

// 表情
extern u8 g_expr_dirty ;
extern u8 g_current_expr_id;
extern u8 g_target_expr_id;

/* 远端指令处理函数 */
void handle_heartbeat(void);			  /* 心跳指令处理 */
void handle_leg_reset(void);				/* 四足复位处理 */
void handle_reboot(void);						/* 软重启指令处理*/
void handle_joystick(u8 dataX, u8 dataY);   	/* 遥杆移动指令处理*/
void handle_heading(u8 angle);		  /* 摇头指令处理 函数入参传递*/
u8 handle_action(void);   					/* 自定义动作指令处理 全局参数传递*/
void handle_expression(void);				/* 表情指令处理 全局参数传递*/
void handle_status_req(void);

//////////////////////////
void Memory_Test(void);
void OLed_Test(void);
void Servo_Test(void);
void USART2_Test(void);

#endif

