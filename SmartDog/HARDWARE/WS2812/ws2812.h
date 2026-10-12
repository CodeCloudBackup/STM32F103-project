#ifndef __WS2812_H
#define __WS2812_H

#include "sys.h"
#include "delay.h"

/* ==================== WS2812 驱动(PA6/PA7 两路) ====================
 * 硬件连线: PA6 -> 模块A 数据线, PA7 -> 模块B 数据线 (两路独立配色,同时刷新)
 *
 * 时序产生方式: TIM1 更新事件(2.4MHz,即0.4167us/拍) 触发 DMA1_Channel2
 *   把波形表依次写入 GPIOA->BSRR, 每个bit占3拍=1.25us(800kbps):
 *     "1"码: 高2拍 + 低1拍  (0.83us / 0.42us)  符合 T1H=0.8us±0.05us
 *     "0"码: 高1拍 + 低2拍  (0.42us / 0.83us)  符合 T0H=0.4us±0.05us
 *   全程硬件产生波形, 不关中断、不占用CPU, TIM2毫秒滴答与蓝牙串口不受影响
 *
 * 资源占用说明:
 *   TIM1  : 空闲定时器(舵机用TIM3/TIM4,毫秒节拍用TIM2), 只取更新事件不输出PWM
 *   DMA1  : Channel2 的请求源固定为 TIM1_UP(本项目原先未使用DMA)
 *   PA6/PA7: 虽是 TIM3_CH1/CH2 的默认脚, 但此处按普通推挽输出+BSRR直写使用,
 *            不启用TIM3这两个通道(舵机只占 TIM3_CH3/CH4), 因此不冲突
 *
 * 注意: BSRR 的低16位是置位、高16位是复位, 半字(16位)写不出复位位,
 *       所以波形表必须是 u32 且 DMA 按字(32位)搬运
 *
 * 六种模式(由 WS2812_SetMode 切换, 动画由主循环 WS2812_Tick 非阻塞推进):
 *   常灭 OFF / 常亮 ON / 流水灯 FLOW / 闪光灯 FLASH / 呼吸灯 BREATH / 七彩闪烁 RAINBOW
 *   两路可各自独立配色与独立模式(如A路呼吸+B路流水)
 */
#define WS2812_PIN_A       GPIO_Pin_6      /* PA6 模块A 数据线 */
#define WS2812_PIN_B       GPIO_Pin_7      /* PA7 模块B 数据线 */
#define WS2812_STRIP_NUM   2               /* 模块路数(PA6/PA7 各一路) */

/* 每路模块的灯珠数量: 单灯模块填1, 4灯环填4, 8灯条填8
 * 本项目模块为 8 灯珠; 两路共用同一条波形表, 故两路必须等长
 * 波形表RAM开销 = LED数 × 24位 × 3拍 × 4字节 (每只灯288字节,8只=2304字节) */
#define WS2812_LED_NUM         8

#define WS2812_BITS_PER_LED    24          /* 每灯24位: G8-R8-B8, MSB先行 */
#define WS2812_TICKS_PER_BIT   3           /* 每bit的DMA节拍数 */
#define WS2812_WAVE_LEN        (WS2812_LED_NUM * WS2812_BITS_PER_LED * WS2812_TICKS_PER_BIT)
#define WS2812_TICK_ARR        30          /* TIM1一拍=30计数=0.4167us@72MHz */

/* 模块编号 */
typedef enum {
    WS2812_STRIP_A = 0,    /* PA6 */
    WS2812_STRIP_B = 1,    /* PA7 */
} Ws2812Strip;

/* ==================== 灯效模式 ==================== */
typedef enum {
    WS2812_MODE_OFF   = 0,    /* 常灭 */
    WS2812_MODE_ON    = 1,    /* 常亮 */
    WS2812_MODE_FLOW  = 2,    /* 流水灯: 单点亮位逐格移动 */
    WS2812_MODE_FLASH = 3,    /* 闪光灯: 全亮/全灭交替 */
    WS2812_MODE_BREATH= 4,    /* 呼吸灯: 亮度三角波渐亮渐灭 */
    WS2812_MODE_RAINBOW=5,    /* 七彩闪烁: 全亮/全灭交替, 每次重新亮时换成下一种颜色 */
    WS2812_MODE_NUM
} Ws2812Mode;

/* period_ms 含义(因模式而异, 单位毫秒):
 *   FLOW  = 移动一格的间隔(如 150)
 *   FLASH = 一个完整周期, 亮/灭各占一半(如 400)
 *   BREATH= 一次完整呼吸: 暗→亮→暗(如 2000)
 *   RAINBOW= 一种颜色的亮+灭周期(如 300, 七色走一圈约 2.1s)
 *   ON/OFF= 忽略 */
#define WS2812_PERIOD_FLOW   150     /* 流水默认步进(8灯走完整圈约1.2s) */
#define WS2812_PERIOD_FLASH  400     /* 闪烁默认周期 */
#define WS2812_PERIOD_BREATH 2000    /* 呼吸默认周期 */
#define WS2812_PERIOD_RAINBOW 300    /* 七彩闪烁默认周期(每色亮150ms+灭150ms) */

/* 开机灯效自检开关(定位用, 现已接业务联动, 固定为0):
 *   0 = 不开自检, 两路保持常灭, 等上层(蓝牙/业务)调 WS2812_SetMode 点亮
 *         当前灯效由 func.c 的 handle_light() 根据动作/表情/通信状态自动接管
 *   1 = 最简位敲自检 WS2812_BasicTest(), 卡在里面不返回主循环(舵机/OLED/蓝牙不启动)
 *   2 = 走正式 TIM1+DMA 路径: A路流水 + B路呼吸, 然后返回主循环由 WS2812_Tick 继续推进 */
#define WS2812_BRINGUP_TEST  0

void WS2812_Init(void);
/* 以下 SetXXX 只写颜色缓存, 需调用 WS2812_Show()/WS2812_Trigger() 才上灯 */
void WS2812_SetPixel(Ws2812Strip strip, u8 idx, u8 r, u8 g, u8 b);
void WS2812_SetStrip(Ws2812Strip strip, u8 r, u8 g, u8 b);
void WS2812_Clear(void);                        /* 两路切为常灭并清空显存(刷新由下次Tick/Show完成) */
void WS2812_SetBrightness(u8 level);            /* 全局亮度0~255, 默认128(限流用) */

/* 模式接口: 立即生效(内部重绘并触发一帧),后续动画由 Tick 推进 */
void      WS2812_SetMode(Ws2812Strip strip, Ws2812Mode mode, u8 r, u8 g, u8 b, u16 period_ms);
Ws2812Mode WS2812_GetMode(Ws2812Strip strip);
void      WS2812_SetAllMode(Ws2812Mode mode, u8 r, u8 g, u8 b, u16 period_ms);  /* 两路同模式 */
void      WS2812_Tick(void);                    /* 主循环每轮调用: 非阻塞推进流水/闪烁/呼吸 */

void WS2812_Show(void);                         /* 阻塞刷新一帧(8灯约240us) */
void WS2812_Trigger(void);                      /* 启动一帧后立即返回 */
u8   WS2812_IsBusy(void);                       /* 1=DMA正在发本帧(只读,不做收尾) */

void WS2812_Test(void);                         /* 六种模式依次演示(阻塞,仅自检用) */

/* 最简阻塞点亮: 纯GPIO忙等位敲(DWT计时),不依赖TIM1/DMA/中断
 * 循环输出: 两路全红 -> 仅A绿 -> 仅B蓝 -> 两路白 -> 全灭 (各1s)
 * 若能点亮而正式驱动不亮 -> 问题在TIM1/DMA那条路径(首查系统时钟是否真为72MHz)
 * 若这里也不亮 -> 硬件侧(5V供电/共地/DIN接法/灯珠本身) */
void WS2812_BasicTest(void);

#endif
