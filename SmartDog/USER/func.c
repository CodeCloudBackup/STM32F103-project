#include "func.h"
#include "expression.h"

 
void handle_leg_reset(void)
{
	Servo_Head(90);   // PB1 头 占空比2.5%，0度
	Servo_Tail(90);   // PB7 尾巴 占空比2.5%，0度
	/* 四腿写逻辑角90(站立), 底层自动叠加安装补偿: 左腿->87, 右前->82, 右后->98 */
	Servo_Leg_FL(90);  // PB8 左前脚
  Servo_Leg_FR(90);  // PB6 右前脚
  Servo_Leg_BL(90);  // PB9 左后脚
  Servo_Leg_BR(90);  // PB0 右后脚
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

	if (adx > ady)              /* 水平为主: 左转/右转(带表情联动偏移) */
	{
		if (dx > 0) {Joy_Right(); turn_expr_enter(10); } /* 右滑→右转:脸右偏10px */
		else        {Joy_Left();  turn_expr_enter(-10); }  /* 左滑→左转:脸左偏10px */
	} else {                    /* 垂直为主: 前进/后退 */
		turn_expr_exit();             /* 非转弯指令:退出联动(若激活则回正计时) */
		if (dy > 0) { Joy_Up();   }  /* 上推→前进 */
		else        { Joy_Down(); }  /* 下拉→后退 */
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
 * 四腿角度为逻辑角: 底层 Servo_Leg_xx() 会自动叠加各腿安装补偿
 * (左±3/右±8, 见 servo.h LEG_xxx_OFFSET), 故所有姿态/步态角度均已含补偿
 * 因实际舵机安装方向可能相反,若某动作方向反了,把对应的120/60对调即可
 */
#define STEP(h, t, fl, fr, bl, br, ms)  { h, t, fl, fr, bl, br, (StepMs)(ms) }
#define SCRIPT_LEN(a)  ((u8)(sizeof(a) / sizeof((a)[0])))

/* ---- 运动(循环步态) ----
 * ==================== 四足运动原理 ====================
 * 每条腿只有 1 个自由度（髋部前/后摆）, 无膝关节抬升。
 * 前进/后退靠“向后的腿扒地推身体”产生推力, 循环交替相位实现连续运动。
 * 角度约定(原始逻辑角, 底层自动叠加各腿安装补偿):
 *   120=数值大于90的方向, 60=数值小于90的方向
 *   实际物理方向由下方 L_FWD/L_BACK/R_FWD/R_BACK 按左右镜像关系给出
 *   STEP 字段顺序: (头, 尾, 左前FL, 右前FR, 左后BL, 右后BR, 保持ms)
 *
 * 【关键: 左右舵机安装方向镜像相反】
 *   实测同一原始角度值, 左腿向前摆时右腿向后摆(安装方向相反),
 *   故脚本里必须按腿区分写值: 左腿"前摆"=120, 右腿"前摆"=60(取反)。
 *   站立中位90不受影响(镜像中心即90), 各腿补偿仍由底层叠加。
 *
 * 【前进/后退 = 对角步态(trot)】
 *   将“左前+右后”(一对对角)与“右前+左后”(另一对对角)分组,
 *   两组始终反向摆动: 一组扒地时另一组前摆复位。对角支撑形成稳定三角,
 *   身体不侧翻。后退只需把两组的扒地/前摆方向整体取反。
 *
 * 【左转/右转 = 同侧差速(scrub turn)】
 *   改为按“左侧两腿 / 右侧两腿”分组, 两侧物理反向摆动:
 *   一侧前推、另一侧后扒, 两侧力矩方向相反 -> 机身原地旋转。
 *   右转: 左腿前推 + 右腿后扒; 左转: 右腿前推 + 左腿后扒。
 *   (跟前进的区别就在于分组方式: 对角=直走, 同侧=转向)
 */
#define LEG_FWD   120   /* 逻辑角: 数值大于90 */
#define LEG_BACK   60   /* 逻辑角: 数值小于90 */
#define LEG_MID    90   /* 中位/站立 */

/* 按安装镜像关系区分左右腿的"物理方向"写值 */
#define L_FWD    LEG_FWD    /* 左腿(FL/BL): 向前摆 -> 120 */
#define L_BACK   LEG_BACK   /* 左腿(FL/BL): 向后摆 -> 60  */
#define R_FWD    LEG_BACK   /* 右腿(FR/BR): 向前摆 -> 60  (安装反向,数值取反) */
#define R_BACK   LEG_FWD    /* 右腿(FR/BR): 向后摆 -> 120 (安装反向,数值取反) */

static const ServoStep scr_walk_forward[] = {   /* 前进: 对角步态 */
    STEP(90,90, L_BACK, R_FWD,  L_FWD,  R_BACK, 260),  /* 相1: 对角FL+BR物理后扒推动, FR+BL物理前摆复位 */
    STEP(90,90, L_FWD,  R_BACK, L_BACK, R_FWD,  260),  /* 相2: 两组对角互换, 循环推进 */
};
static const ServoStep scr_walk_backward[] = {  /* 后退: 对角步态方向取反 */
    STEP(90,90, L_FWD,  R_BACK, L_BACK, R_FWD,  260),  /* 相1: 对角FL+BR物理前摆, FR+BL物理后扒 -> 往后推 */
    STEP(90,90, L_BACK, R_FWD,  L_FWD,  R_BACK, 260),  /* 相2: 两组对角互换 */
};
static const ServoStep scr_turn_right[] = {     /* 右转: 同侧差速原地转 */
    STEP(90,90, L_FWD,  R_BACK, L_FWD,  R_BACK, 300),  /* 相1: 左腿物理前推 + 右腿物理后扒 -> 机身右转 */
    STEP(90,90, L_BACK, R_FWD,  L_BACK, R_FWD,  300),  /* 相2: 两侧回摆复位, 持续旋转力矩 */
};
static const ServoStep scr_turn_left[] = {      /* 左转: 同侧差速原地转 */
    STEP(90,90, L_BACK, R_FWD,  L_BACK, R_FWD,  300),  /* 相1: 右腿物理前推 + 左腿物理后扒 -> 机身左转 */
    STEP(90,90, L_FWD,  R_BACK, L_FWD,  R_BACK, 300),  /* 相2: 两侧回摆复位 */
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
static uint32_t s_script_deadline = 0;  /* 循环步态的停止时刻(0=不限时:摇杆持续指令/单次姿势) */

#define PRESET_MOVE_MS  20000u          /* 预定运动动作时长 20s */

/* 播放一段脚本;loop=1循环(步态),loop=0执行一遍后回空闲
 * 若已在播放同一循环脚本(摇杆持续同方向),不重置相位 */
static void Servo_PlayScript(const ServoStep *steps, u8 len, u8 loop)
{
    if (loop && g_action_state == ACTION_SCRIPT && s_script == steps && s_script_loop)
    {
        s_script_deadline = 0;          /* 新指令接管:取消预定动作定时,改为持续执行 */
        return;
    }
    s_script      = steps;
    s_script_len  = len;
    s_script_idx  = 0;
    s_script_loop = loop;
    s_script_deadline = 0;
    Servo_RunStep(&s_script[0]);
    s_step_tick = g_sys_tick_ms;
    g_action_state = ACTION_SCRIPT;
    g_action_start_tick = g_sys_tick_ms;
}

/* 定时播放循环步态(预定动作): duration_ms 到点后自动切回站立脚本
 * 与摇杆步态共用同一套脚本/同一状态机,只在停止时刻上区分 */
static void Servo_PlayScriptTimed(const ServoStep *steps, u8 len, uint32_t duration_ms)
{
    Servo_PlayScript(steps, len, 1);
    s_script_deadline = g_sys_tick_ms + duration_ms;  /* 回绕安全:判定时用差值 */
}

/* CMD_ACTION(0x30) 动作码 → 姿势/步态脚本分发 */
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
    /* ---- 预定运动动作: 循环步态跑完20s后自动收腿站立 ---- */
    case ACT_MOVE_FORWARD: Servo_PlayScriptTimed(scr_walk_forward,  SCRIPT_LEN(scr_walk_forward),  PRESET_MOVE_MS);
                        break;
    case ACT_MOVE_BACK:    Servo_PlayScriptTimed(scr_walk_backward, SCRIPT_LEN(scr_walk_backward), PRESET_MOVE_MS);
                        break;
    case ACT_TURN_LEFT:    Servo_PlayScriptTimed(scr_turn_left,     SCRIPT_LEN(scr_turn_left),     PRESET_MOVE_MS);
                        break;
    case ACT_TURN_RIGHT:   Servo_PlayScriptTimed(scr_turn_right,    SCRIPT_LEN(scr_turn_right),    PRESET_MOVE_MS);
                        break;
    default:
        printf("No program for action 0x%02X\r\n", act_val);
        break;
    }
}

/* ---- 摇杆手势→步态接口(手势方向 = 物理运动方向) ---- */
void Joy_Up(void)    { Servo_PlayScript(scr_walk_forward,  SCRIPT_LEN(scr_walk_forward),  1); }  /* 上推→前进 */
void Joy_Down(void)  { Servo_PlayScript(scr_walk_backward, SCRIPT_LEN(scr_walk_backward), 1); }  /* 下拉→后退 */
void Joy_Left(void)  { Servo_PlayScript(scr_turn_left,     SCRIPT_LEN(scr_turn_left),     1); }  /* 左滑→左转 */
void Joy_Right(void) { Servo_PlayScript(scr_turn_right,    SCRIPT_LEN(scr_turn_right),    1); }  /* 右滑→右转 */

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
        /* 预定动作定时到: 停止循环步态,收腿站立(单次执行姿势脚本不带deadline,不受此处影响) */
        if (s_script_deadline && (int32_t)(g_sys_tick_ms - s_script_deadline) >= 0)
        {
            s_script_deadline = 0;
            Servo_PlayScript(scr_stand, SCRIPT_LEN(scr_stand), 0);
            printf("preset action done, stand up\r\n");
            return 1;
        }
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

/* ==================== 灯效联动(不动蓝牙协议,本地推断) ====================
 * 不加新定时器/中断: 每轮主循环算一次"目标灯效", 只在发生变化时才调
 * WS2812_SetMode(内部阻塞发完首帧,8灯约240us), 之后的流水/呼吸/闪烁仍由
 * 主循环 WS2812_Tick() 非阻塞推进, 不影响步态相位与心跳应答
 *
 * 判定优先级: 睡觉表情 > 通信报警 > 步态运动 > 其余表情 > 其余时间
 *   睡觉排在报警前面: 10min无通信会自动进睡眠表情,若不排前面就会红闪一整夜
 *   "运动"只指四足步态(摇杆前进/后退/左转/右转 及 20s 预定运动动作);
 *   坐下/趴下/摇头/摇尾/标记/握手 这类单次姿势不算,走表情色
 * 颜色为"基准色", 实际输出还要再乘全局亮度 s_brightness(默认128,约一半),
 *   所以"暗绿/暗蓝"直接用小数值的基准色即可
 */
enum {
	LT_MOVE = 0,     /* 步态运动: 蓝色闪烁 */
	LT_ALARM,        /* 通信中断报警: 红色闪烁 */
	LT_SLEEP,        /* 表情-睡觉: 暗绿呼吸 */
	LT_ANGRY,        /* 表情-生气: 橙色快速流水 */
	LT_CRY,          /* 表情-哭泣: 暗蓝常亮 */
	LT_HAPPY,        /* 表情-开心: 七彩闪烁 */
	LT_IDLE          /* 其余时间: 蓝色常亮 */
};

static u8 s_light_last      = 0xFF;  /* 0xFF=上电后还没设过灯效(保证首次必上灯) */
static u8 s_light_seen_comm = 0;     /* 本次上电后是否建立过通信(避免开机就报警) */

static void light_apply(u8 st)
{
	switch (st)
	{
	case LT_MOVE:                                /* 运动: 蓝色闪烁(亮125ms+灭125ms) */
		WS2812_SetAllMode(WS2812_MODE_FLASH,   0,   0, 255, 250);
		break;
	case LT_ALARM:                               /* 通信中断: 红色闪烁 */
		WS2812_SetAllMode(WS2812_MODE_FLASH, 255,   0,   0, WS2812_PERIOD_FLASH);
		break;
	case LT_SLEEP:                               /* 睡觉: 暗绿呼吸 */
		WS2812_SetAllMode(WS2812_MODE_BREATH,  0,  90,   0, WS2812_PERIOD_BREATH);
		break;
	case LT_ANGRY:                               /* 生气: 橙色快速流水(8灯一圈约0.48s) */
		WS2812_SetAllMode(WS2812_MODE_FLOW,  255, 120,   0, 60);
		break;
	case LT_CRY:                                 /* 哭泣: 暗蓝常亮 */
		WS2812_SetAllMode(WS2812_MODE_ON,      0,   0,  90, 0);
		break;
	case LT_HAPPY:                               /* 开心: 七彩闪烁(七色走一圈约2.1s) */
		WS2812_SetAllMode(WS2812_MODE_RAINBOW, 255,   0,   0, WS2812_PERIOD_RAINBOW);
		break;
	case LT_IDLE:                                /* 其余: 蓝色常亮 */
	default:
		WS2812_SetAllMode(WS2812_MODE_ON,      0,   0, 255, 0);
		break;
	}
}

/* comm_elapsed_ms: 距最近一次收到有效帧(心跳/正确指令)的时长,由主循环传入 */
void handle_light(u32 comm_elapsed_ms)
{
	u8 want;

	if (comm_elapsed_ms < 5000) s_light_seen_comm = 1;   /* 通信建立过,之后才允许报警 */

	if (g_current_expr_id == EXP_SLEEP)
		want = LT_SLEEP;                                /* 睡觉: 暗绿呼吸,不被报警覆盖 */
	else if (s_light_seen_comm && comm_elapsed_ms >= 5000)
		want = LT_ALARM;                                /* 5s 无有效帧: 红色闪烁报警 */
	else if (g_action_state == ACTION_SCRIPT &&
	    (s_script == scr_walk_forward || s_script == scr_walk_backward ||
	     s_script == scr_turn_left    || s_script == scr_turn_right))
		want = LT_MOVE;                                 /* 只有四足步态算"运动" */
	else switch (g_current_expr_id)                     /* 其余看表情 */
	{
		case EXP_ANGRY: want = LT_ANGRY; break;
		case EXP_CRY:   want = LT_CRY;   break;
		case EXP_HAPPY: want = LT_HAPPY; break;
		default:        want = LT_IDLE;  break;         /* NORMAL/发呆/未知 */
	}

	if (want == s_light_last) return;                   /* 灯效未变: 不发消息不占时间 */
	printf("LIGHT %u->%u comm=%u act=%u expr=%u\r\n",
	       (unsigned)s_light_last, (unsigned)want, (unsigned)comm_elapsed_ms,
	       (unsigned)g_action_state, (unsigned)g_current_expr_id);
	s_light_last = want;
	light_apply(want);
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
	//Servo_Head(0);   // PB1 头 占空比2.5%，0度
	//Servo_Tail(0);   // PB7 尾巴 占空比2.5%，0度
	Servo_Leg_FL(0);  // PB8 左前脚
    //Servo_Leg_FR(0);  // PB6 右前脚
  //Servo_Leg_BL(0);  // PB9 左后脚
   // Servo_Leg_BR(0);  // PB0 右后脚
	delay_ms(1000);
	  LED0 = !LED0;
	//Servo_Head(60); // PB1 头 占空比7.5%，90度
	//Servo_Tail(60); // PB7 尾巴 占空比7.5%，90度
	Servo_Leg_FL(60);  // PB8 左前脚
   // Servo_Leg_FR(60);  // PB6 右前脚
   //Servo_Leg_BL(60);  // PB9 左后脚
   // Servo_Leg_BR(60);  // PB0 右后脚
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
