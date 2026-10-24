#include "touch.h"                  // Device header
#include "timer.h"                  // g_sys_tick_ms: 抖动/连击合并的时间基准
#include "usart.h"                  // usart1_printf: 诊断打印(仅 touch_debug 用)

/* ==================== PA0 三线触摸开关(锁存模式: 每点一次 OUT 翻转并保持) ====================
 * 用"主循环轮询 PA0 电平变化"识别点击:
 *   - 锁存模式下一次有效触摸使 OUT 翻转并维持 -> 轮询读到电平翻转即一次点击;
 *   - 机械抖动与窗口内的多次连续点击, 用时间窗合并为 1 次.
 * 引脚模式关键(本模块第三根线只是内部点 LED/负载的驱动节点, 导通时被驱动为高):
 *   - 用内部下拉 IPD: 关断时把节点吸到 0(与 Out_PP 强拉到地一样是灭的, 不会回灌),
 *     导通时模块驱动为高、克服下拉 -> 读 1, 于是 0/1 翻转可被检测;
 *   - 切忌用上拉 IPU: 上拉会从 PA0 向外灌电流点亮 LED(灭灯微亮),
 *     且把关断节点也钳高, 与导通同为高 -> 电平不翻转 -> 点击读不到.
 */
#define TOUCH_CLICK_GAP_MS  3000u    /* 抖动/连击合并窗口(ms): 3s 内多次点击只算 1 次 */

/* 诊断开关: 1=每次 PA0 电平跳变都打印原始状态, 用于确认极性与"点击有没有让引脚动".
 * 已验证: 下拉 IPD 下灯亮=1、灯灭=0, 点击能翻转被轮询检测; 诊断完毕关闭. */
u8 touch_debug = 0;

static u8       s_prev    = 0;       /* 上次读到的电平(下拉空闲=0) */
static uint32_t s_fire_ms = 0;       /* 上次判定为一次点击的时刻 */

/**
  * 函    数：触摸开关初始化(PA0 下拉输入, 纯轮询)
  * 参    数：无
  * 返 回 值：无
  */
void touch_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

	/* PA0: 下拉输入, 空闲读 0; 模块导通驱动为高时读 1(不可用上拉, 否则回灌微亮) */
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPD;
	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	s_prev    = (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_RESET) ? 0 : 1;
	s_fire_ms = g_sys_tick_ms;                 /* 上电先锁定一段, 忽略上电建立毛刺 */
}

/**
  * 函    数：轮询一次有效点击(锁存模式下电平翻转即一次)
  * 参    数：无
  * 返 回 值：1=检测到一次新点击(已做抖动/连击合并), 0=无(非阻塞)
  */
u8 touch_poll(void)
{
	u8 cur = (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_RESET) ? 0 : 1;

	if (cur == s_prev)
		return 0;                              /* 电平未变: 无动作 */

	s_prev = cur;                              /* 记录翻转后的电平(锁存保持, 不会重复计) */

	if (touch_debug)
		usart1_printf("PA0 level -> %d\r\n", cur);  /* 诊断: 证明引脚是否随点击变化 */

	/* 距上次判定点击不足合并窗口: 视为抖动或连击, 忽略本次翻转 */
	if (g_sys_tick_ms - s_fire_ms < TOUCH_CLICK_GAP_MS)
		return 0;

	s_fire_ms = g_sys_tick_ms;
	return 1;
}
