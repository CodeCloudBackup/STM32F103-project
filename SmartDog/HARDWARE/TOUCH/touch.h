#ifndef __TOUCH_H
#define __TOUCH_H
#include "sys.h"

/* ==================== PA0 三线触摸开关模块(锁存模式, 轮询电平) ====================
 * 模块 OUT -> PA0; 上电默认输出关闭, 每次有效触摸使 OUT 电平翻转并保持.
 * touch_Init : PA0 下拉输入(无需 EXTI; 模块为有效高输出, 不能用上拉否则回灌微亮)
 * touch_poll : 主循环轮询, 检测到一次有效点击(已合并抖动/连击)返回 1, 否则 0(非阻塞)
 * touch_debug: =1 时每次 PA0 电平跳变都打印原始状态, 用于排查接线/模式(查完改回 0)
 */
extern u8 touch_debug;
void touch_Init(void);
u8   touch_poll(void);

#endif
