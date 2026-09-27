#include "func.h"
#include "expression.h"

 
void handle_leg_reset(void)
{
	Servo_Head(90);   // PB0 头 占空比2.5%，0度
	Servo_Tail(90);   // PB1 尾 占空比2.5%，0度
	Servo_Leg_FL(90);  // PB6 左前腿
  Servo_Leg_FR(90);  // PB7 右前腿
  Servo_Leg_BL(90);  // PB8 左后腿
  Servo_Leg_BR(90);  // PB9 右后腿
	delay_ms(1000);
	printf("leg_reset handle\r\n");
}

/* ==================== 表情调度 ====================
 * 具体绘制在expression.c,这里只做分发与刷新时机控制
 */
/* 表情状态(跨模块共享,extern声明见expression.h) */
u8 g_current_expr_id = 0;
u8 g_expr_dirty = 0;
u8 g_target_expr_id = 0;
signed char g_expr_shift = 0;   /* 正常表情水平偏移px(转弯联动用) */

void handle_expression(void)
{
	OLED_Clear();                        /* 清GRAM(内部会刷新一次空白) */
	switch(g_target_expr_id){
		case EXP_HAPPY: expr_face_happy(); break;
		case EXP_CRY:   expr_face_cry();   break;
		case EXP_DAZE:  expr_face_daze();  break;
		case EXP_ANGRY: expr_face_angry(); break;
		case EXP_SLEEP: expr_face_sleep(); break;
		case EXP_NORMAL: expr_face_normal(); break;
		default:        expr_face_daze();  break;
	}
	OLED_Refresh();                      /* 一次性把表情刷到屏上 */
}

void handle_sleep(void)
{
	printf("Sleep command\r\n");
	/* 待机:停止动作并显示睡眠表情 */
	g_action_state = ACTION_IDLE;
	if (g_current_expr_id != EXP_SLEEP)
	{
		g_target_expr_id = EXP_SLEEP;
		g_expr_dirty     = 1;
	}
}

/* ==================== 转弯表情联动 ====================
 * 收到转向指令: 表情切为正常款并向旋转方向偏移10px(右+10/左-10)
 * 停止转向(回中/5s超时/切其他动作): 偏移回正,1秒后恢复转弯前原表情
 * 非阻塞:回正计时由主循环每轮调用的handle_expr_return轮询 */
static u8      s_turn_expr_active = 0;   /* 联动模式进行中 */
static u8      s_turn_orig_expr   = 0;   /* 进入转弯前的表情ID */
static uint32_t s_expr_return_tick = 0;  /* !=0:已回正,待1s后恢复原表情 */

static void turn_expr_enter(signed char shift)
{
	if (!s_turn_expr_active)
	{
		s_turn_orig_expr   = g_current_expr_id;  /* 记下转弯前表情 */
		s_turn_expr_active = 1;
	}
	s_expr_return_tick = 0;                    /* 转弯中取消回表计时 */
	if (g_expr_shift != shift || g_target_expr_id != EXP_NORMAL)
	{
		g_expr_shift     = shift;
		g_target_expr_id = EXP_NORMAL;
		g_expr_dirty     = 1;
	}
}

static void turn_expr_exit(void)
{
	if (!s_turn_expr_active) return;
	s_turn_expr_active = 0;
	g_expr_shift       = 0;                    /* 表情回正 */
	g_target_expr_id   = EXP_NORMAL;           /* 回正后先显示居中的正常脸 */
	g_expr_dirty       = 1;
	s_expr_return_tick = g_sys_tick_ms;        /* 1s后恢复原表情 */
}

void handle_expr_return(void)
{
	if (s_expr_return_tick == 0) return;
	if (g_sys_tick_ms - s_expr_return_tick < 1000)
		return;
	s_expr_return_tick = 0;
	if (g_target_expr_id != EXP_NORMAL)
		return;   /* 等待期间用户已手动切其他表情,取消自动恢复 */
	g_target_expr_id = s_turn_orig_expr;       /* 恢复转弯前表情 */
	g_expr_shift     = 0;
	g_expr_dirty = 1;
}

/* ==================== 通信超时保护 ====================
 * 5s 未收到心跳/有效指令: 停止所有动作(只执行一次)
 * 10min 未收到心跳/有效指令: 进入待机 handle_sleep(),并刷新计时避免重复触发
 * last_valid_tick: 最近一次收到有效帧(心跳或正确指令)的时刻,由主循环维护
 */
void handle_timeout_guard(u32 *last_valid_tick)
{
	u32 elapsed = g_sys_tick_ms - *last_valid_tick;

	if (elapsed >= 5000 && g_action_state != ACTION_IDLE)
	{
		g_action_state = ACTION_IDLE;   /* 停止所有动作 */
		turn_expr_exit();               /* 转弯联动表情同步回正并计时恢复 */
		printf("Timeout 5s: all actions stopped\r\n");
	}

	if (elapsed >= 10u * 60u * 1000u)
	{
		handle_sleep();                 /* 进入待机 */
		*last_valid_tick = g_sys_tick_ms; /* 重置计时,避免每轮重复触发 */
	}
}

void handle_reboot(void)
{
	printf("Reboot command\r\n");
  NVIC_SystemReset();
}

/* 摇杆指令: dataX=水平(左0~右255), dataY=垂直(后0~前255), 中心约128
 * 改用"带符号偏移+主轴判定": 先取偏离中心更远的主轴决定动作,
 * 斜向滑动不再被固定阈值误判成其他方向(修复右滑Y分量提前命中前后判定、
 * 以及X未超过固定阈值导致右转不触发的问题) */
void handle_joystick(u8 dataX, u8 datay)
{
	int dx = (int)dataX - 128;   /* >0 右, <0 左 */
	int dy = (int)datay - 128;   /* >0 前, <0 后 */
	int adx = (dx < 0) ? -dx : dx;
	int ady = (dy < 0) ? -dy : dy;

	if (adx < 25 && ady < 25)   /* 双轴都在死区内: 回中停 */
	{
		g_action_state = ACTION_IDLE;
		turn_expr_exit();             /* 停止转弯:表情回正,1s后恢复原表情 */
		return;
	}

	if (adx > ady)              /* 水平为主: 前后(按要求与垂直轴对调) */
	{
		turn_expr_exit();             /* 非转弯指令:退出联动(若激活则回正计时) */
		if (dx > 0) {Joy_Right();turn_expr_enter(10); } /* 右滑*/
		else        {Joy_Left(); turn_expr_enter(-10); }   /* 左滑 */
	} else {                    /* 垂直为主: 转向 */
		if (dy > 0) { Joy_Up();     }  /* 上推右转:脸右偏10px */
		else        { Joy_Down(); }  /* 下拉左转:脸左偏10px */
	}
}

/* ==================== 摇头指令 0x21 ====================
 * 通过PB6头舵机(SG90, TIM4_CH1 PWM)执行: 以收到角度为中心左右各摆30°,
 * 往复一次后回到中心角。运行时填充一段RAM脚本交给非阻塞引擎播放,
 * 不卡主循环,5s超时保护照常生效。角度限幅30~150(留足摆动余量) */
static ServoStep s_scr_heading[5];              /* 运行时填充的摇头脚本 */
static void Servo_PlayScript(const ServoStep *steps, u8 len, u8 loop); /* 前置声明(定义在后文脚本引擎) */

void handle_heading(u8 angle)
{
	ServoStep *sc = s_scr_heading;

	if (angle < 30)  angle = 30;                /* 限幅:保证±30°不撞机械限位 */
	if (angle > 150) angle = 150;

	sc[0].head = angle - 30;  sc[0].t = 300;    /* 左摆 */
	sc[1].head = angle + 30;  sc[1].t = 300;    /* 右摆 */
	sc[2].head = angle - 30;  sc[2].t = 300;    /* 再左摆 */
	sc[3].head = angle + 30;  sc[3].t = 300;    /* 再右摆 */
	sc[4].head = angle;       sc[4].t = 200;    /* 回中心角 */
	/* tail/四腿字段保持static零初始化(=0表示本步不动作) */

	Servo_PlayScript(s_scr_heading, 5, 0);      /* 单次执行,不循环 */
	printf("heading shake, base angle=%d via PB6\r\n", angle);
}


/* ==================== 电量 ====================
 * 电量百分比(0~100),暂固定为 0,待接入 ADC 分压采样后由采样任务更新
 * 预留接口:后续在 ADC 采样中将电压换算为百分比写入 g_battery_level
 */
u8 g_battery_level = 0;

/* 心跳应答:返回状态帧 CMD_STATUS_REPORT(0x41),含电量信息
 * 数据域(4字节): [0]电量%  [1]动作状态  [2]当前表情  [3]通信状态(1=在线)
 */
void handle_status_req(void)
{
	u8 status[4] = {0};
	status[0] = g_battery_level;              /* 电量百分比(暂为0) */
	status[1] = (u8)g_action_state;           /* 当前动作状态 */
	status[2] = g_current_expr_id;            /* 当前表情ID */
	status[3] = 1;                            /* 通信状态:能应答即在线 */
	BLE_SendFrame(CMD_STATUS_REPORT, status, 4);
	printf("status_req handle, battery=%d%%\r\n", g_battery_level);
}

ActionState g_action_state = ACTION_IDLE;
uint32_t    g_action_start_tick = 0;
u8     g_action_param = 0;

/* ==================== 动作脚本库 ====================
 * 约定: 90=中位/站立, 120=前/抬起, 60=后/压下, 0=本步该轴不动
 * STEP(头,尾,左前,右前,左后,右后,保持ms)
 * 因实际舵机安装方向可能相反,若某动作方向反了,把对应的120/60对调即可
 */
#define STEP(h, t, fl, fr, bl, br, ms)  { h, t, fl, fr, bl, br, (StepMs)(ms) }
#define SCRIPT_LEN(a)  ((u8)(sizeof(a) / sizeof((a)[0])))

/* ---- 运动(循环步态) ---- 数组名对应摇杆手势: fwd=上推 back=下拉 left=左滑 right=右滑
 * (名字跟手势,具体物理动作见各行注释,数据与手势互换前一致) */
static const ServoStep scr_walk_fwd[] = {   /* 上推手势触发: 物理右转步态 */
    STEP(90,90,  60,120,  60,120,  300),
    STEP(90,90,  90, 90,  90, 90,  150),
};
static const ServoStep scr_walk_back[] = {  /* 下拉手势触发: 物理左转步态 */
    STEP(90,90, 120, 60, 120, 60,  300),
    STEP(90,90,  90, 90,  90, 90,  150),
};
static const ServoStep scr_turn_right[] = { /* 右滑手势触发: 物理前进对角步态 */
    STEP(90,90,  60,120, 120, 60,  300),
    STEP(90,90, 120, 60,  60,120,  300),
};
static const ServoStep scr_turn_left[] = {  /* 左滑手势触发: 物理后退步态 */
    STEP(90,90, 120, 60,  60,120, 300),
    STEP(90,90,  60,120, 120, 60,  300),
};

/* ---- 姿势(单次执行) ---- */
static const ServoStep scr_stand[] = {        /* 站立 */
    STEP(90,90, 90,90, 90,90, 500),
};
static const ServoStep scr_sit[] = {          /* 坐下:后腿前收 */
    STEP(90,90, 90,90, 30,150, 600),
};
static const ServoStep scr_lie_forward[] = {  /* 趴下:前脚向前,后脚向后 */
    STEP(120,90, 150,30, 150,30, 600),
};
static const ServoStep scr_lie_back[] = {     /* 后躺:前脚向前,后脚向前 */
    STEP(90,150, 30,150, 30,150, 600),
};

static const ServoStep scr_shake_head[] = {   /* 摇头 30~120 往复 */
    STEP(30, 0, 0,0,0,0, 300),
    STEP(120,0, 0,0,0,0, 300),
    STEP(30, 0, 0,0,0,0, 300),
    STEP(120,0, 0,0,0,0, 300),
    STEP(90, 0, 0,0,0,0, 300),
};
static const ServoStep scr_wag_tail[] = {     /* 摇尾 60~120 往复 */
    STEP(0, 40, 0,0,0,0, 150),
    STEP(0,140, 0,0,0,0, 150),
    STEP(0, 40, 0,0,0,0, 150),
    STEP(0,140, 0,0,0,0, 150),
    STEP(0, 40, 0,0,0,0, 150),
    STEP(0,140, 0,0,0,0, 150),
		STEP(0, 40, 0,0,0,0, 150),
    STEP(0,140, 0,0,0,0, 150),
    STEP(0, 40, 0,0,0,0, 150),
    STEP(0,140, 0,0,0,0, 150),
    STEP(0, 40, 0,0,0,0, 150),
    STEP(0,140, 0,0,0,0, 150),
    STEP(0, 90, 0,0,0,0, 100),
};
static const ServoStep scr_mark[] = {         /* 标记:先坐下,右后腿向后伸 */
    STEP(90,90, 90,90, 90,90, 500),        /* 站 */
    STEP(0, 0,   0, 0,   0, 30,  800),        /* 右后腿向后伸(br=60) */
    STEP(0, 0,   0, 0,   0,90,  400),        /* 收回 */
};
static const ServoStep scr_handshake[] = {    /* 握手:左前腿向前伸 */
    STEP(90,90, 90, 90, 90,90, 300),         /* 先站定 */
    STEP(0, 0, 30,  0,  0,  0, 700),         /* 左前腿前伸 */
    STEP(0, 0, 60,  0,  0,  0, 300),         /* 轻摆 */
    STEP(0, 0, 30,  0,  0,  0, 300),
    STEP(0, 0,  90,  0,  0,  0, 300),         /* 收回 */
};

/* ---- 脚本播放游标(文件内部) ---- */
static const ServoStep *s_script     = NULL;
static u8   s_script_len = 0;
static u8   s_script_idx = 0;
static u8   s_script_loop = 0;
static uint32_t s_step_tick = 0;

/* 播放一段脚本;loop=1循环(步态),loop=0执行一遍后回空闲
 * 若已在播放同一循环脚本(摇杆持续同方向),不重置相位 */
static void Servo_PlayScript(const ServoStep *steps, u8 len, u8 loop)
{
    if (loop && g_action_state == ACTION_SCRIPT && s_script == steps && s_script_loop)
        return;
    s_script      = steps;
    s_script_len  = len;
    s_script_idx  = 0;
    s_script_loop = loop;
    Servo_RunStep(&s_script[0]);
    s_step_tick = g_sys_tick_ms;
    g_action_state = ACTION_SCRIPT;
    g_action_start_tick = g_sys_tick_ms;
}

/* CMD_ACTION(0x30) 动作码 → 姿势脚本分发 */
void handle_action_cmd(u8 act_val)
{
    switch (act_val)
    {
    case ACT_STAND:       Servo_PlayScript(scr_stand,       SCRIPT_LEN(scr_stand),       0); break;
    case ACT_SIT_DOWN:    Servo_PlayScript(scr_sit,         SCRIPT_LEN(scr_sit),         0); break;
    case ACT_LIE_FORWARD: Servo_PlayScript(scr_lie_forward, SCRIPT_LEN(scr_lie_forward), 0); break;
    case ACT_LIE_BACK:    Servo_PlayScript(scr_lie_back,    SCRIPT_LEN(scr_lie_back),    0); break;
    case ACT_SHAKE_HEAD:  Servo_PlayScript(scr_shake_head,  SCRIPT_LEN(scr_shake_head),  0); break;
    case ACT_WAG_TAIL:    Servo_PlayScript(scr_wag_tail,    SCRIPT_LEN(scr_wag_tail),    0); break;
    case ACT_MARK:        Servo_PlayScript(scr_mark,        SCRIPT_LEN(scr_mark),        0); break;
    case ACT_WAVE:        Servo_PlayScript(scr_handshake,   SCRIPT_LEN(scr_handshake),   0); break;
    default:
        printf("No program for action 0x%02X\r\n", act_val);
        break;
    }
}

/* ---- 摇杆手势→步态接口(名字=触发手势,数组名同为手势方向,注释=物理动作) ---- */
void Joy_Right(void) { Servo_PlayScript(scr_turn_right, SCRIPT_LEN(scr_turn_right), 1); }  /* 右滑→前进 */
void Joy_Left(void)  { Servo_PlayScript(scr_turn_left,  SCRIPT_LEN(scr_turn_left),  1); }  /* 左滑→后退 */
void Joy_Up(void)    { Servo_PlayScript(scr_walk_fwd,   SCRIPT_LEN(scr_walk_fwd),   1); }  /* 上推→右转 */
void Joy_Down(void)  { Servo_PlayScript(scr_walk_back,  SCRIPT_LEN(scr_walk_back),  1); }  /* 下拉→左转 */

/* ==================== 非阻塞动作执行器 ==================== */
/*
 * 每次调用只执行一小步，不阻塞主循环
 * 返回 1 表示动作仍在执行，0 表示动作完成/空闲
 */
u8 handle_action(void)
{
    switch (g_action_state)
    {
    case ACTION_IDLE:
        return 0;

    case ACTION_SCRIPT:
        /* 当前步保持时间到,推进到下一步 */
        if (g_sys_tick_ms - s_step_tick >= s_script[s_script_idx].t)
        {
            s_script_idx++;
            if (s_script_idx >= s_script_len)
            {
                if (s_script_loop)
                {
                    s_script_idx = 0;             /* 步态循环 */
                }
                else
                {
                    g_action_state = ACTION_IDLE;  /* 姿势完成,保持末态 */
                    return 0;
                }
            }
            Servo_RunStep(&s_script[s_script_idx]);
            s_step_tick = g_sys_tick_ms;
        }
        return 1;

    case ACTION_LEG_RESET:
    case ACTION_CUSTOM:
    default:
        g_action_state = ACTION_IDLE;
        return 0;
    }
}

/* 表情轮播测试: 依次展示6个表情,每个3秒 */
void Expression_Test(void)
{
	u8 ids[6] = {EXP_NORMAL, EXP_HAPPY, EXP_CRY, EXP_DAZE, EXP_ANGRY, EXP_SLEEP};
	u8 i;
	for(i = 0; i < 6; i++)
	{
		g_target_expr_id = ids[i];
		handle_expression();
		delay_ms(3000);
	}
}
	

 // 硬件功能验证函数
 void Servo_Test(void)
 {
	Servo_Head(0);   // PB0 头 占空比2.5%，0度
	Servo_Tail(0);   // PB1 尾 占空比2.5%，0度
	Servo_Leg_FL(0);  // PB6 左前腿
    Servo_Leg_FR(0);  // PB7 右前腿
    Servo_Leg_BL(0);  // PB8 左后腿
    Servo_Leg_BR(0);  // PB9 右后腿
	delay_ms(1000);
	  LED0 = !LED0;
	Servo_Head(60); // PB0 头 占空比7.5%，90度
	Servo_Tail(60); // PB1 尾 占空比7.5%，90度
	Servo_Leg_FL(60);  // PB6 左前腿
    Servo_Leg_FR(60);  // PB7 右前腿
    Servo_Leg_BL(60);  // PB8 左后腿
    Servo_Leg_BR(60);  // PB9 右后腿
	delay_ms(1000); 
	 LED0 = !LED0;
 }
 void OLed_Test(void)
{
	//画四个圈
	OLED_DrawCircle(23,40,18);
	OLED_Refresh();
	delay_ms(700);
	OLED_DrawCircle(50,40,18);
	 OLED_Refresh();
	delay_ms(700);
	OLED_DrawCircle(77,40,18);
	OLED_Refresh();
	delay_ms(700);
	OLED_DrawCircle(104,40,18);
	OLED_Refresh();
	delay_ms(700);
}

void Memory_Test(void)
{
		u8 *p=0;
		u8 i=10;
		p=mymalloc(SRAMIN,2048);//2
		if(p!=NULL)sprintf((char*)p,"Memory Malloc Test%03d",i);
		printf("%s\r\n",p);
		myfree(SRAMIN,p);
	  p=0;
}

void USART2_Test(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    // 1. 开启 USART2 和 GPIOA 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

		// 2. 配置 PA2 为复用推挽输出，50MHz
	  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;//设置PA2引脚
	  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP ;//设置输出模式为复用推挽输出
	  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz ;//设置输出速度为50MHZ
	  GPIO_Init(GPIOA,&GPIO_InitStructure);//初始化
	  // 3. 配置 PA3 为上拉输入
	  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;//设置PA3引脚
	  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU ;//设置输出模式为上拉输入
	  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz ;//设置输出速度为50MHZ
	  GPIO_Init(GPIOA,&GPIO_InitStructure);//初始化
		// 3. 配置 PA5 为推挽输出
		GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;//设置PA4引脚 -- //ESP8266使能引脚
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP ;//设置输出模式为推挽输出
		GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz ;//设置输出速度为50MHZ
		GPIO_Init(GPIOA,&GPIO_InitStructure);//初始化
		
    // USART2 配置
    USART_InitStructure.USART_BaudRate = 9600;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx;  // 仅发送
    USART_Init(USART2, &USART_InitStructure);
    USART_Cmd(USART2, ENABLE);

    // 发送测试
    while (1)
    {
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
        USART_SendData(USART2, 'A');
        // 如果 PA2 能持续输出 'A'（用示波器或逻辑分析仪测），说明硬件正常
    }
}
