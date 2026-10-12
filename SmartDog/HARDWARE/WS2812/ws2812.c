#include "ws2812.h"
#include "timer.h"
#include <stdio.h>

/* ==================== WS2812(PA6/PA7 两路) 驱动实现 ====================
 * 波形由 TIM1 更新事件(2.4MHz) + DMA1_Channel2 直写 GPIOA->BSRR 硬件产生,
 * CPU 只在重绘波形表时参与, 不关中断、不做 us 级忙等,
 * 因此 TIM2 毫秒节拍与蓝牙串口中断不受影响.
 */

/* 一个bit = 3拍, 每拍一个BSRR字(低16位置位/高16位复位):
 *   bit=1:  置位 | 保持 | 复位   -> 高 2拍 = 0.83us (T1H)
 *   bit=0:  置位 | 复位 | 保持   -> 高 1拍 = 0.42us (T0H)
 * 两路数据线在同一个BSRR字里各自动作, 因此必须等长(共用 WS2812_LED_NUM)
 */
#define WS2812_DMA_CH        DMA1_Channel2
#define WS2812_DMA_IT_TC     DMA1_IT_TC2
#define WS2812_DMA_IT_GL     DMA1_IT_GL2
#define PIN_A_SET            ((u32)WS2812_PIN_A)          /* BSRR 置位 PA6 */
#define PIN_B_SET            ((u32)WS2812_PIN_B)          /* BSRR 置位 PA7 */
#define PIN_A_RST            (PIN_A_SET << 16)            /* BSRR 复位 PA6 */
#define PIN_B_RST            (PIN_B_SET << 16)            /* BSRR 复位 PA7 */
#define BOTH_RST             (PIN_A_RST | PIN_B_RST)

#define WS_BREATH_STEP_MS    20u    /* 呼吸亮度刷新间隔(也用作闪烁/流水的最小步进) */

typedef struct {
    u8        r, g, b;                    /* 基准色(用户设定,未含亮度) */
    u8        grb[WS2812_LED_NUM][3];     /* 实际送出的显存(G-R-B,已含亮度缩放) */
    Ws2812Mode mode;
    u16       period;                     /* 流水:步进间隔 / 闪烁:整周期 / 呼吸:暗亮暗全程 */
    u16       half;                       /* 闪烁半周期、呼吸渐亮时长(ms) */
    u8        inc;                        /* 呼吸每拍亮度增量 */
    u8        level;                      /* 呼吸当前亮度 0~255 */
    u8        idx;                        /* 流水当前位置 */
    u8        on;                         /* 闪烁/单灯流水的当前亮灭态 */
    u8        rising;                     /* 呼吸方向: 1=渐亮 0=渐灭 */
    u32       tick;                       /* 上一次状态推进时刻(ms) */
    u8        dirty;                      /* 1=显存有变化待刷新 */
} WsCtx;

static WsCtx  s_ctx[WS2812_STRIP_NUM];
static u32    s_wave[WS2812_WAVE_LEN];    /* BSRR波形表: 每灯24位*3拍*4字节,8灯=2304字节 */
static u8     s_brightness = 128;         /* 全局亮度(限流用) */
static volatile u8 s_busy = 0;            /* 1=DMA正在发送本帧 */
static volatile u32 s_busy_tick = 0;      /* 本帧启动时刻(看门狗用) */
static u8     s_busy_warned = 0;          /* TC中断缺失只报一次 */

/* 七彩闪烁调色板: 红 橙 黄 绿 青 蓝 紫 (仍会叠加全局亮度 s_brightness) */
static const u8 ws_rainbow[7][3] = {
    {255,   0,   0}, {255, 128,   0}, {255, 255,   0}, {  0, 255,   0},
    {  0, 255, 255}, {  0,   0, 255}, {160,   0, 255}
};

/* ==================== 显存 -> 波形表 ==================== */
static void ws_build_wave(void)
{
    u32 w = 0;
    u8  led, by, bit;

    for (led = 0; led < WS2812_LED_NUM; led++)
    {
        for (by = 0; by < 3; by++)
        {
            u8 a = s_ctx[WS2812_STRIP_A].grb[led][by];   /* MSB先行 */
            u8 b = s_ctx[WS2812_STRIP_B].grb[led][by];
            for (bit = 0; bit < 8; bit++)
            {
                u8 ba = (a & 0x80) ? 1 : 0;
                u8 bb = (b & 0x80) ? 1 : 0;
                a <<= 1; b <<= 1;
                s_wave[w++] = (ba ? PIN_A_SET : 0) | (bb ? PIN_B_SET : 0);  /* 拍1: 抬高 */
                s_wave[w++] = (ba ? 0 : PIN_A_RST) | (bb ? 0 : PIN_B_RST);  /* 拍2: 0码在此落下 */
                s_wave[w++] = (ba ? PIN_A_RST : 0) | (bb ? PIN_B_RST : 0);  /* 拍3: 1码在此落下 */
            }
        }
    }
}

/* ==================== 按模式重绘某一路显存 ==================== */
static void ws_render(WsCtx *c)
{
    u8  i;
    u16 f;                        /* 该灯的综合亮度因子 0~255 */

    for (i = 0; i < WS2812_LED_NUM; i++)
    {
        switch (c->mode)
        {
        case WS2812_MODE_OFF:    f = 0;                        break;
        case WS2812_MODE_FLASH:
        case WS2812_MODE_RAINBOW:
            /* 七彩与闪烁同样亮/灭交替,区别在于颜色由 Tick 逐周期更换 */
            f = c->on ? 255 : 0;              break;
        case WS2812_MODE_FLOW:
            /* 单灯模块没有"流水"可言,退化为等间隔点亮/熄灭 */
            if (WS2812_LED_NUM <= 1) f = c->on ? 255 : 0;
            else                     f = (i == c->idx) ? 255 : 0;
            break;
        case WS2812_MODE_BREATH: f = c->level;                 break;
        default:                 f = 255;                      break;   /* ON */
        }
        f = (u16)(((u32)f * s_brightness) >> 8);
        c->grb[i][0] = (u8)(((u32)c->g * f) >> 8);
        c->grb[i][1] = (u8)(((u32)c->r * f) >> 8);
        c->grb[i][2] = (u8)(((u32)c->b * f) >> 8);
    }
    c->dirty = 1;
}

/* ==================== 数据线GPIO(推挽输出+空闲拉低) ==================== */
static void ws_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    /* PA6/PA7 作普通推挽输出: 不启用 TIM3_CH1/CH2 复用, 波形由写BSRR产生 */
    GPIO_InitStructure.GPIO_Pin   = WS2812_PIN_A | WS2812_PIN_B;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIOA->BSRR = BOTH_RST;                        /* 空闲电平为低,满足RESET要求 */
}

/* ==================== 初始化 ==================== */
void WS2812_Init(void)
{
    TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
    DMA_InitTypeDef          DMA_InitStructure;
    NVIC_InitTypeDef         NVIC_InitStructure;
    u8                       i;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    ws_gpio_init();

    /* TIM1(APB2=72MHz) 2.4MHz 拍: 30计数 = 0.4167us, 3拍 = 1.25us = 800kbps */
    TIM_TimeBaseStructure.TIM_Period        = WS2812_TICK_ARR - 1;
    TIM_TimeBaseStructure.TIM_Prescaler     = 0;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure);
    TIM_ClearFlag(TIM1, TIM_FLAG_Update);
    TIM_DMACmd(TIM1, TIM_DMA_Update, ENABLE);      /* 更新事件->DMA请求 */
    TIM_Cmd(TIM1, DISABLE);                        /* 由 Trigger 逐帧启停 */

    /* DMA1_Channel2(请求源固定为 TIM1_UP): 波形表 -> GPIOA->BSRR, 按字搬运 */
    DMA_DeInit(WS2812_DMA_CH);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&GPIOA->BSRR;
    DMA_InitStructure.DMA_MemoryBaseAddr     = (u32)s_wave;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize         = WS2812_WAVE_LEN;
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Word;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(WS2812_DMA_CH, &DMA_InitStructure);
    DMA_ITConfig(WS2812_DMA_CH, DMA_IT_TC, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel                   = DMA1_Channel2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;   /* 低于舵机/串口/节拍 */
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 默认状态: 两路常灭 */
    for (i = 0; i < WS2812_STRIP_NUM; i++)
    {
        WsCtx *c = &s_ctx[i];
        u8     j;
        c->r = c->g = c->b = 0;
        c->mode   = WS2812_MODE_OFF;
        c->period = WS2812_PERIOD_BREATH;
        c->half   = WS2812_PERIOD_BREATH / 2;
        c->inc    = 1;
        c->level  = 0;
        c->idx    = 0;
        c->on     = 0;
        c->rising = 1;
        c->tick   = g_sys_tick_ms;
        c->dirty  = 0;
        for (j = 0; j < WS2812_LED_NUM; j++) { c->grb[j][0] = c->grb[j][1] = c->grb[j][2] = 0; }
    }
    ws_build_wave();

#if WS2812_BRINGUP_TEST == 1
    WS2812_BasicTest();                        /* 不返回: 卡在最简阻塞点亮自检 */
#elif WS2812_BRINGUP_TEST == 2
    /* 走正式 TIM1+DMA 路径点灯: SetMode 内部会阻塞发完首帧, 返回后由主循环 Tick 推进 */
    WS2812_SetMode(WS2812_STRIP_A, WS2812_MODE_FLOW,   0,   0, 255, WS2812_PERIOD_FLOW);
    WS2812_SetMode(WS2812_STRIP_B, WS2812_MODE_BREATH, 255,   0,   0, WS2812_PERIOD_BREATH);
#endif
}

/* ==================== 帧发送 ==================== */
/* 结束一帧: 停TIM1与DMA通道、数据线拉低(形成>50us复位锁存)、清busy
 * 正常完成(TC中断)与异常兜底(看门狗)共用此处,保证状态一定回零 */
static void ws_stop_frame(void)
{
    DMA_Cmd(WS2812_DMA_CH, DISABLE);
    TIM_Cmd(TIM1, DISABLE);
    TIM_SetCounter(TIM1, 0);
    TIM_ClearFlag(TIM1, TIM_FLAG_Update);
    GPIOA->BSRR = BOTH_RST;
    s_busy = 0;
}

void WS2812_Trigger(void)
{
    if (s_busy) return;                       /* 上一帧未走完,本帧由下次 Tick 重试 */
    ws_build_wave();
    DMA_SetCurrDataCounter(WS2812_DMA_CH, WS2812_WAVE_LEN);
    DMA_ClearITPendingBit(WS2812_DMA_IT_GL | WS2812_DMA_IT_TC);
    /* 强制从拍头开始,并清掉上一帧残留的更新标志:
     * 否则使能TIM1的瞬间会多发一拍DMA请求,把波形表首字提前写进BSRR,
     * 造成首颗灯多一个窄脉冲而错位(典型现象:只有第一颗灯颜色乱或不亮) */
    TIM_SetCounter(TIM1, 0);
    TIM_ClearFlag(TIM1, TIM_FLAG_Update);
    s_busy      = 1;
    s_busy_tick = g_sys_tick_ms;              /* 看门狗起点 */
    DMA_Cmd(WS2812_DMA_CH, ENABLE);
    TIM_Cmd(TIM1, ENABLE);                    /* 从此逐拍推动 DMA */
}

/* 阻塞发一帧并等它结束: 每轮最多等3ms(远大于8灯的240us),
 * 若完成中断迟迟不来就强制收尾后重发,最多4次,绝不把状态卡死 */
void WS2812_Show(void)
{
    u8  tries;
    u32 t0;

    for (tries = 0; tries < 4u; tries++)
    {
        if (!s_busy)
            WS2812_Trigger();                 /* 有在途帧时下一轮再启 */
        t0 = g_sys_tick_ms;
        while (s_busy && (g_sys_tick_ms - t0) < 3u) { }
        if (!s_busy) break;                   /* 本帧已由TC中断正常结束 */
        ws_stop_frame();                      /* 兜底:数据其实早走完,只是中断没来 */
    }
    s_ctx[WS2812_STRIP_A].dirty = 0;
    s_ctx[WS2812_STRIP_B].dirty = 0;
}

u8 WS2812_IsBusy(void)
{
    return (u8)s_busy;
}

/* DMA 传输完成: 正常收尾 */
void DMA1_Channel2_IRQHandler(void)
{
    if (DMA_GetITStatus(WS2812_DMA_IT_TC) != RESET)
    {
        DMA_ClearITPendingBit(WS2812_DMA_IT_GL | WS2812_DMA_IT_TC);
        ws_stop_frame();
    }
}

/* ==================== 配色接口(只改显存,由 Show/Trigger 上灯) ==================== */
void WS2812_SetPixel(Ws2812Strip strip, u8 idx, u8 r, u8 g, u8 b)
{
    WsCtx *c;
    if (strip >= WS2812_STRIP_NUM || idx >= WS2812_LED_NUM) return;
    c = &s_ctx[strip];
    c->grb[idx][0] = g;
    c->grb[idx][1] = r;
    c->grb[idx][2] = b;
    c->dirty = 1;                             /* 常亮模式下可逐灯自定义;动画模式会在下一拍被重绘 */
}

void WS2812_SetStrip(Ws2812Strip strip, u8 r, u8 g, u8 b)
{
    WsCtx *c;
    if (strip >= WS2812_STRIP_NUM) return;
    c = &s_ctx[strip];
    c->r = r; c->g = g; c->b = b;
    ws_render(c);
}

void WS2812_Clear(void)
{
    u8 i;
    for (i = 0; i < WS2812_STRIP_NUM; i++)
    {
        s_ctx[i].r = s_ctx[i].g = s_ctx[i].b = 0;
        s_ctx[i].mode  = WS2812_MODE_OFF;
        s_ctx[i].level = 0;
        s_ctx[i].on    = 0;
        ws_render(&s_ctx[i]);
    }
}

void WS2812_SetBrightness(u8 level)
{
    u8 i;
    s_brightness = level;
    for (i = 0; i < WS2812_STRIP_NUM; i++) ws_render(&s_ctx[i]);
}

/* ==================== 模式接口 ==================== */
void WS2812_SetMode(Ws2812Strip strip, Ws2812Mode mode, u8 r, u8 g, u8 b, u16 period_ms)
{
    WsCtx *c;
    u32    half;

    if (strip >= WS2812_STRIP_NUM || mode >= WS2812_MODE_NUM) return;
    c = &s_ctx[strip];
    c->r = r; c->g = g; c->b = b;
    c->mode   = mode;
    c->period = period_ms ? period_ms : 1000;
    c->tick   = g_sys_tick_ms;
    c->idx    = 0;
    c->on     = 1;

    /* 半周期: 闪烁=亮/灭各一半, 呼吸=暗→亮(或亮→暗)一段 */
    half = c->period / 2;
    if (half < WS_BREATH_STEP_MS) half = WS_BREATH_STEP_MS;
    c->half = (u16)half;
    /* 呼吸每拍增量: 用 half/20ms 拍数把 0~255 铺满, 至少为1 */
    c->inc  = (u8)((255u * WS_BREATH_STEP_MS + c->half - 1) / c->half);
    c->level  = (mode == WS2812_MODE_BREATH) ? 0 : 255;
    c->rising = 1;
    ws_render(c);
    WS2812_Show();                             /* 切换立即见效(本帧最多占用240us) */
}

void WS2812_SetAllMode(Ws2812Mode mode, u8 r, u8 g, u8 b, u16 period_ms)
{
    WS2812_SetMode(WS2812_STRIP_A, mode, r, g, b, period_ms);
    WS2812_SetMode(WS2812_STRIP_B, mode, r, g, b, period_ms);
}

Ws2812Mode WS2812_GetMode(Ws2812Strip strip)
{
    if (strip >= WS2812_STRIP_NUM) return WS2812_MODE_OFF;
    return s_ctx[strip].mode;
}

/* ==================== 动画推进(非阻塞,主循环每轮调用) ==================== */
void WS2812_Tick(void)
{
    u8   i;
    u32  now = g_sys_tick_ms;

    for (i = 0; i < WS2812_STRIP_NUM; i++)
    {
        WsCtx *c = &s_ctx[i];
        u32  elapsed = now - c->tick;
        u32  ivl;

        switch (c->mode)
        {
        case WS2812_MODE_FLOW:
            ivl = (u32)c->period;
            if (elapsed < ivl) break;
            c->tick = now;
            if (WS2812_LED_NUM <= 1) { c->on = !c->on; }         /* 单灯: 定时亮灭 */
            else { c->idx = (u8)((c->idx + 1) % WS2812_LED_NUM); }
            ws_render(c);
            break;

        case WS2812_MODE_FLASH:
            if (elapsed < c->half) break;
            c->tick = now;
            c->on = c->on ? 0 : 1;
            ws_render(c);
            break;

        case WS2812_MODE_RAINBOW:
            if (elapsed < c->half) break;
            c->tick = now;
            c->on = c->on ? 0 : 1;
            if (c->on)                              /* 每次重新亮起前换下一种颜色 */
            {
                c->idx = (u8)((c->idx + 1) % 7);
                c->r = ws_rainbow[c->idx][0];
                c->g = ws_rainbow[c->idx][1];
                c->b = ws_rainbow[c->idx][2];
            }
            ws_render(c);
            break;

        case WS2812_MODE_BREATH:
            if (elapsed < WS_BREATH_STEP_MS) break;
            c->tick = now;
            if (c->rising)
            {
                if (c->level + c->inc >= 255) { c->level = 255; c->rising = 0; }
                else c->level += c->inc;
            }
            else
            {
                if (c->level <= c->inc) { c->level = 0; c->rising = 1; }
                else c->level -= c->inc;
            }
            ws_render(c);
            break;

        default:                                 /* ON/OFF 静态,不重绘 */
            break;
        }
    }

    /* 看门狗: 一帧只占240us, busy 挂住超过2ms 就是 TC 中断没进来。
     * 不收尾的话之后每帧都被 Trigger() 挡在门外,现象就是
     * "灯停在最后一次切换的首帧颜色不动(七彩/闪烁的初始态恰好是全亮)" */
    if (s_busy && (g_sys_tick_ms - s_busy_tick) >= 2u)
    {
        ws_stop_frame();
        if (!s_busy_warned)
        {
            s_busy_warned = 1;
            printf("WS2812: DMA TC interrupt missing, watchdog reset\r\n");
        }
    }

    if ((s_ctx[WS2812_STRIP_A].dirty || s_ctx[WS2812_STRIP_B].dirty) && !WS2812_IsBusy())
    {
        s_ctx[WS2812_STRIP_A].dirty = 0;
        s_ctx[WS2812_STRIP_B].dirty = 0;
        WS2812_Trigger();                       /* 本帧仍在发则保留dirty,下轮重试 */
    }
}

/* ==================== 自检: 五种模式依次演示(阻塞,仅在调试时调用) ==================== */
/* 演示期间自己推动画(正常由主循环 WS2812_Tick 调用) */
static void ws_test_run(u32 ms)
{
    u32 start = g_sys_tick_ms;
    while (g_sys_tick_ms - start < ms)
        WS2812_Tick();
}

void WS2812_Test(void)
{
    /* 常亮: A路红 / B路绿 */
    WS2812_SetMode(WS2812_STRIP_A, WS2812_MODE_ON,    255,   0,   0, 0);
    WS2812_SetMode(WS2812_STRIP_B, WS2812_MODE_ON,      0, 255,   0, 0);
    ws_test_run(2000);

    /* 流水: A路蓝 / B路白 */
    WS2812_SetMode(WS2812_STRIP_A, WS2812_MODE_FLOW,    0,   0, 255, WS2812_PERIOD_FLOW);
    WS2812_SetMode(WS2812_STRIP_B, WS2812_MODE_FLOW,  255, 255, 255, WS2812_PERIOD_FLOW);
    ws_test_run(3000);

    /* 闪烁: 橙黄 */
    WS2812_SetAllMode(WS2812_MODE_FLASH, 255, 120, 0, WS2812_PERIOD_FLASH);
    ws_test_run(3000);

    /* 呼吸: 青绿 */
    WS2812_SetAllMode(WS2812_MODE_BREATH, 0, 255, 200, WS2812_PERIOD_BREATH);
    ws_test_run(4000);

    /* 七彩闪烁 */
    WS2812_SetAllMode(WS2812_MODE_RAINBOW, 255, 0, 0, WS2812_PERIOD_RAINBOW);
    ws_test_run(4000);
		while(1);
}

/* ==================== 最简阻塞点亮(定位用,与正式驱动互不依赖) ====================
 * 时序由 CPU 直接写 GPIOA->BSRR + DWT 周期计数器忙等产生, 不用TIM1/DMA/中断完成:
 *   72MHz 下 1周期=13.9ns; 位周期90周期(1.25us);
 *   "0"码高26周期(约0.40us), "1"码高55周期(约0.80us) —— 已含忙等循环自身开销
 * 每一位单独关中断(临界区仅1.25us), 避开 TIM2/串口中断把波形拉长
 */
#define WS_DEMCR        (*(volatile u32 *)0xE000EDFCu)   /* CoreDebug->DEMCR */
#define WS_DWT_CTRL     (*(volatile u32 *)0xE0001000u)   /* DWT->CTRL */
#define WS_DWT_CYCCNT   (*(volatile u32 *)0xE0001004u)   /* DWT->CYCCNT */

#define WS_BB_BIT_CYC    90u    /* 位周期 1.25us */
#define WS_BB_T0H_CYC    26u    /* "0"码高电平 */
#define WS_BB_T1H_CYC    55u    /* "1"码高电平 */

static void ws_bb_send_byte(u32 set, u32 rst, u8 val)
{
    u8  i;
    u32 t, hi;

    for (i = 0; i < 8; i++)
    {
        hi  = (val & 0x80) ? WS_BB_T1H_CYC : WS_BB_T0H_CYC;
        val = (u8)(val << 1);                      /* MSB先行 */
        __disable_irq();
        t = WS_DWT_CYCCNT;
        GPIOA->BSRR = set;                         /* 抬高 */
        while (WS_DWT_CYCCNT - t < hi) { }
        GPIOA->BSRR = rst;                         /* 拉低 */
        while (WS_DWT_CYCCNT - t < WS_BB_BIT_CYC) { }
        __enable_irq();
    }
}

/* 单线阻塞发一帧: led_num 颗同色灯, 帧尾拉低>50us锁存(全0即为全灭帧) */
static void ws_bb_send(u32 set, u32 rst, u8 led_num, u8 r, u8 g, u8 b)
{
    u8 i;

    for (i = 0; i < led_num; i++)
    {
        ws_bb_send_byte(set, rst, g);              /* WS2812 数据顺序: G-R-B */
        ws_bb_send_byte(set, rst, r);
        ws_bb_send_byte(set, rst, b);
    }
    GPIOA->BSRR = rst;
    delay_us(80);                                  /* RESET码 */
}

void WS2812_BasicTest(void)
{
    u8  n  = WS2812_LED_NUM;
    u32 t0, t1;

    ws_gpio_init();                                /* 只配GPIO, 不碰TIM1/DMA */

    /* DWT周期计数器: 先开 trace 时钟(TRCENA), 再开 CYCCNT */
    WS_DEMCR      |= (1u << 24);
    WS_DWT_CYCCNT  = 0;
    WS_DWT_CTRL   |= 1u;
    t0 = WS_DWT_CYCCNT;
    delay_us(100);
    t1 = WS_DWT_CYCCNT;

    printf("WS2812 bringup: LEDs=%u, DWT %s (100us = %u cyc)\r\n",
           (unsigned)n, (t1 > t0) ? "OK" : "FAIL", (unsigned)(t1 - t0));
    printf("SWS=%u (期望2=PLL,即系统真跑在72MHz)\r\n",
           (unsigned)((RCC->CFGR >> 2) & 3u));

    if (t1 <= t0) while (1) { }                    /* DWT不计数则停住,免误判为灯坏了 */

    for (;;)
    {
        ws_bb_send(PIN_A_SET, PIN_A_RST, n, 255,   0,   0);   /* 两路全红 */
        ws_bb_send(PIN_B_SET, PIN_B_RST, n, 255,   0,   0);
        delay_ms(1000);

        ws_bb_send(PIN_A_SET, PIN_A_RST, n,   0, 255,   0);   /* 仅A(PA6)绿, B全灭 */
        ws_bb_send(PIN_B_SET, PIN_B_RST, n,   0,   0,   0);
        delay_ms(1000);

        ws_bb_send(PIN_A_SET, PIN_A_RST, n,   0,   0,   0);   /* 仅B(PA7)蓝, A全灭 */
        ws_bb_send(PIN_B_SET, PIN_B_RST, n,   0,   0, 255);
        delay_ms(1000);

        ws_bb_send(PIN_A_SET, PIN_A_RST, n, 255, 255, 255);   /* 两路白 */
        ws_bb_send(PIN_B_SET, PIN_B_RST, n, 255, 255, 255);
        delay_ms(1000);

        ws_bb_send(PIN_A_SET, PIN_A_RST, n,   0,   0,   0);   /* 全灭 */
        ws_bb_send(PIN_B_SET, PIN_B_RST, n,   0,   0,   0);
        delay_ms(1000);
    }
}
