#ifndef __EXPRESSION_H
#define __EXPRESSION_H

#include "sys.h"
#include "oled.h"
#include "usart_protocol.h"

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

/* ==================== 抽奖转盘(非阻塞) ====================
 * 收到抽奖指令后: 停下所有动作+立正(func.c), 然后由以下接口在主机循环轮询驱动:
 *   lottery_start()     - 启动转盘动画(从"谢谢惠顾"区开始顺时针轮选)
 *   lottery_poll()      - 主机循环每轮调用, 按时间推进相位/刷新, 非阻塞
 *   lottery_is_active() - 1=动画进行中(此期间不应刷新表情, 二者争用 OLED)
 * 动画结束自动向 USART3 发送结果帧 AA 55 31 01 06 37 0D 0A, 并回位表情 */
void lottery_start(void);      /* 3圈抽奖(由 CMD_LOTTERY 0x06 触发) */
void lottery_start_once(void); /* 单圈抽奖(由 CMD_EXPRESSION 0x31/0x07 触发), 转完一轮停在谢谢惠顾 */
void lottery_poll(void);
u8   lottery_is_active(void);

#endif
