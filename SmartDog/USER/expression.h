#ifndef __EXPRESSION_H
#define __EXPRESSION_H

#include "sys.h"
#include "oled.h"
#include "ble_protocol.h"

/* ==================== 表情状态(跨模块共享) ====================
 * 定义在func.c; main.c设置开机默认表情, func.c的CMD_EXPRESSION(0x31)处理置脏标记
 */
extern u8 g_current_expr_id;   /* 当前已显示的表情ID */
extern u8 g_expr_dirty;        /* 脏标记:1=有新表情待刷新 */
extern u8 g_target_expr_id;    /* 目标表情ID(ExpressionType) */
extern signed char g_expr_shift; /* 正常表情眼睛水平偏移px(转弯联动) */

/* 表情绘制函数: 只写入GRAM,不刷新,由调用方控制刷新时机 */
void expr_face_normal(void);           /* 正常 */
void expr_face_happy(void);            /* 开心 */
void expr_face_cry(void);              /* 哭泣 */
void expr_face_daze(void);             /* 发呆 */
void expr_face_angry(void);            /* 生气 */
void expr_face_sleep(void);            /* 睡觉 */

#endif
