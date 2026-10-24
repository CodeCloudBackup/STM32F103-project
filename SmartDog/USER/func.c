#include "func.h"
#include "expression.h"

 
void handle_leg_reset(void)
{
	/* 六轴目标/当前角同步为站立逻辑角90, 底层自动叠加安装补偿:
	 * 左腿->87, 右前->82, 右后->98; 之后由 Servo_RampTick 统一驱动 */
	Servo_RampInit();
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
	OLED_ClearBuf();                       /* 只清GRAM不刷新, 避免先闪一帧黑屏 */
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

/* 摇杆步态方向记忆(用于对角模糊区维持上一个动作,避免相位被反复复位) */
#define GAIT_NONE   0
#define GAIT_FWD    1
#define GAIT_BACK   2
#define GAIT_LEFT   3
#define GAIT_RIGHT  4
static u8 s_last_gait = GAIT_NONE;

/* 摇杆指令: dataX=水平(左0~右255), dataY=垂直(后0~前255), 中心约128
 * 采用"带符号偏移 + 主轴判定 + 迟滞":
 *  - 双轴死区: 播放站立脚本斜坡收腿(不再冻结在步态中间姿态)
 *  - 主轴判定加迟滞: |adx-ady|<=15 视为对角模糊区, 维持上一个步态,
 *    避免斜向抖动让主轴在"转向/前后"间来回横跳而反复复位相位(前进顿挫主因之一)
 *  - 转向需 adx>ady+15(明显水平主导), 前后需 ady>adx+15(明显垂直主导) */
void handle_joystick(u8 dataX, u8 datay)
{
	int dx = (int)dataX - 128;   /* >0 右, <0 左 */
	int dy = (int)datay - 128;   /* >0 前, <0 后 */
	int adx = (dx < 0) ? -dx : dx;
	int ady = (dy < 0) ? -dy : dy;

	if (adx < 25 && ady < 25)   /* 双轴都在死区内: 回中→收腿站立 */
	{
		Joy_Stop();                 /* 播放站立脚本,目标角90由斜坡层平滑落位 */
		turn_expr_exit();           /* 表情回正,1s后恢复原表情 */
		s_last_gait = GAIT_NONE;
		return;
	}

	if (adx > ady + 15)         /* 水平明显主导: 左转/右转(带表情联动偏移) */
	{
		if (dx > 0) { Joy_Right(); s_last_gait = GAIT_RIGHT; turn_expr_enter(10);  } /* 右滑→右转:脸右偏10px */
		else        { Joy_Left();  s_last_gait = GAIT_LEFT;  turn_expr_enter(-10); }  /* 左滑→左转:脸左偏10px */
	}
	else if (ady > adx + 15)    /* 垂直明显主导: 前进/后退 */
	{
		turn_expr_exit();           /* 非转弯指令:退出联动(若激活则回正计时) */
		if (dy > 0) { Joy_Up();   s_last_gait = GAIT_FWD;  }  /* 上推→前进 */
		else        { Joy_Down(); s_last_gait = GAIT_BACK; }  /* 下拉→后退 */
	}
	else                        /* 对角模糊区: 维持上一个步态,不切换脚本(保相位) */
	{
		switch (s_last_gait)
		{
		case GAIT_FWD:   Joy_Up();    break;
		case GAIT_BACK:  Joy_Down();  break;
		case GAIT_LEFT:  Joy_Left();  turn_expr_enter(-10); break;
		case GAIT_RIGHT: Joy_Right(); turn_expr_enter(10);  break;
		default:                     /* 尚无历史: 按主导轴粗选一次 */
			if (adx > ady)
			{
				if (dx > 0) { Joy_Right(); s_last_gait = GAIT_RIGHT; turn_expr_enter(10);  }
				else        { Joy_Left();  s_last_gait = GAIT_LEFT;  turn_expr_enter(-10); }
			}
			else
			{
				turn_expr_exit();
				if (dy > 0) { Joy_Up();   s_last_gait = GAIT_FWD;  }
				else        { Joy_Down(); s_last_gait = GAIT_BACK; }
			}
			break;
		}
	}
}


/* ==================== 电量 ====================
 * 电量百分比(0~100),暂固定为 0,待接入 ADC 分压采样后由采样任务更新
 * 预留接口:后续在 ADC 采样中将电压换算为百分比写入 g_battery_level
 */
u8 g_battery_level = 0;

/* 心跳应答(收到 CMD_HEARTBEAT 0x01 时触发): 回发真实状态帧 CMD_STATUS_REPORT(0x41)
 * 数据域(4字节): [0]电量%  [1]动作状态  [2]当前表情  [3]通信状态(1=在线)
 * 除电量(暂未接入 ADC, 保持占位)外, 动作/表情/在线均为实时值 */
void handle_heartbeat(void)
{
	u8 status[4] = {0};
	status[0] = g_battery_level;              /* 电量百分比(除外: 暂为0占位) */
	status[1] = (u8)g_action_state;           /* 当前动作状态 */
	status[2] = g_current_expr_id;            /* 当前表情ID */
	status[3] = 1;                            /* 通信状态:能应答即在线 */
	USART_SendFrame(CMD_STATUS_REPORT, status, 4);
	printf("heartbeat -> status(action=%d expr=%d online=1)\r\n", status[1], status[2]);
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
/* 后退: 与前进同一套对角蹭步几何(同样承认是滑步, 不追求抬腿), 仅用两相保持时间
 * 非对称把净方向偏到后退。实测本机贴地滑步净方向由抓地/机械偏置决定: 单纯镜像或
 * 调换相位顺序仍是同向循环(原地蹭), 故让"后退驱动相"压久、"前进反推相"快掠过,
 * 靠持续扒地时间把身体往后顶。前进脚本保持原样不动。
 * 微调: 后退太弱 -> 拉长相2(如450)、缩短相1(如120); 若反而向前 -> 两相时间对调。 */
static const ServoStep scr_walk_backward[] = {
    STEP(90,90, L_FWD,  R_BACK, L_BACK, R_FWD,  150),  /* 相1: 前进反推相, 短保持快掠过 */
    STEP(90,90, L_BACK, R_FWD,  L_FWD,  R_BACK, 400),  /* 相2: 后退驱动相, 长保持强化朝后扒地 */
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

/* CMD_ACTION(0x30) 动作码 → 姿势/步态脚本分发
 * 所有预定义动作在执行本地脚本的同时, 都把动作帧(CMD_ACTION/act_val)同步发送到 USART3(PB10),
 * 由下游设备镜像执行同一动作(与摇尾 handle_wag_tail 的转发方式一致) */
void handle_action_cmd(u8 act_val)
{
    USART3_SendFrame(CMD_ACTION, &act_val, 1);   /* 动作指令同步发送给 USART3 */
    printf("action -> USART3 (act=0x%02X)\r\n", act_val);
    switch (act_val)
    {
    case ACT_STAND:       Servo_PlayScript(scr_stand,       SCRIPT_LEN(scr_stand),       0); break;
    case ACT_SIT_DOWN:    Servo_PlayScript(scr_sit,         SCRIPT_LEN(scr_sit),         0); break;
    case ACT_LIE_FORWARD: Servo_PlayScript(scr_lie_forward, SCRIPT_LEN(scr_lie_forward), 0); break;
    case ACT_LIE_BACK:    Servo_PlayScript(scr_lie_back,    SCRIPT_LEN(scr_lie_back),    0); break;
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

/* ==================== 抽奖触发(CMD_LOTTERY 0x06) ====================
 * 1) 停下所有动作(置空闲); 2) 立正: 播放站立脚本由斜坡收腿落位;
 * 3) 启动 OLED 转盘动画(非阻塞, 由主循环 lottery_poll 逐帧推进,
 *    动画结束时向 USART3 发送结果帧)。 */
void handle_lottery_trigger(void)
{
    g_action_state = ACTION_IDLE;                         /* 先停当前脚本 */
    Servo_PlayScript(scr_stand, SCRIPT_LEN(scr_stand), 0); /* 立正: 目标角90, 斜坡落位 */
    turn_expr_exit();                                     /* 若转弯联动中, 表情回正 */
    lottery_start();                                      /* 接管 OLED 播放转盘 */
    printf("lottery start: stop + stand + wheel\r\n");
}

/* ==================== 单圈抽奖触发(CMD_EXPRESSION 0x31 / data 0x07) ====================
 * 与 handle_lottery_trigger 同样的"停动作+立正"流程, 但转盘只轮选一轮(6步),
 * 转完一轮停在"谢谢惠顾"后停止, 结束同样向 USART3 发结果帧。 */
void handle_lottery_once(void)
{
    g_action_state = ACTION_IDLE;                         /* 先停当前脚本 */
    Servo_PlayScript(scr_stand, SCRIPT_LEN(scr_stand), 0); /* 立正: 目标角90, 斜坡落位 */
    turn_expr_exit();                                     /* 若转弯联动中, 表情回正 */
    lottery_start_once();                                 /* 单圈转盘 */
    printf("lottery once: stop + stand + wheel(1 round)\r\n");
}

/* 摇尾时把表情临时切为"开心"并持续 5s, 到时自动恢复摇尾前表情(非阻塞, 主循环轮询) */
static u8       s_wag_expr_active = 0;   /* 开心临时表情进行中 */
static uint32_t s_wag_expr_until  = 0;   /* 恢复时刻(g_sys_tick_ms) */
static u8       s_wag_expr_prev   = 0;   /* 进入前正在显示的表情ID */

/* 摇尾执行: 本机尾舵机已移除, 把摇尾动作帧(CMD_ACTION/ACT_WAG_TAIL)同步发送到 USART3(PB10),
 * 由下游设备(尾驱动/其它模块)完成实际摇尾.
 * 上位机下发摇尾指令与 PA0 触摸点击都调用此函数.
 * 同时把表情联动为"开心", 5s 后由 handle_wag_expr 自动恢复. */
void handle_wag_tail(void)
{
	u8 act = ACT_WAG_TAIL;
	USART3_SendFrame(CMD_ACTION, &act, 1);   /* 摇尾指令同步发送给 USART3 */
	printf("wag tail -> USART3 (act=0x%02X)\r\n", act);

	/* 摇尾联动: 首次进入记住原表情, 每次摇尾都把开心持续期刷新为 5s */
	if (!s_wag_expr_active)
	{
		s_wag_expr_prev   = g_current_expr_id;  /* 记下摇尾前正在显示的表情 */
		s_wag_expr_active = 1;
	}
	s_wag_expr_until = g_sys_tick_ms + 5000u;
	if (g_target_expr_id != EXP_HAPPY)
	{
		g_target_expr_id = EXP_HAPPY;
		g_expr_dirty     = 1;                    /* 置脏, 由主循环 expression_poll 刷新 */
	}
}

/* 开心临时表情到期恢复: 主循环每轮调用, 摇尾满 5s 后回到摇尾前表情(非阻塞) */
void handle_wag_expr(void)
{
	if (!s_wag_expr_active)
		return;
	if ((int32_t)(g_sys_tick_ms - s_wag_expr_until) < 0)
		return;                                  /* 未到 5s */
	s_wag_expr_active = 0;
	if (g_target_expr_id == EXP_HAPPY)           /* 期间未被用户手动切走才恢复 */
	{
		g_target_expr_id = s_wag_expr_prev;
		g_expr_dirty     = 1;
	}
}

/* ---- 摇杆手势→步态接口(手势方向 = 物理运动方向) ---- */
void Joy_Up(void)    { Servo_PlayScript(scr_walk_forward,  SCRIPT_LEN(scr_walk_forward),  1); }  /* 上推→前进 */
void Joy_Down(void)  { Servo_PlayScript(scr_walk_backward, SCRIPT_LEN(scr_walk_backward), 1); }  /* 下拉→后退 */
void Joy_Left(void)  { Servo_PlayScript(scr_turn_left,     SCRIPT_LEN(scr_turn_left),     1); }  /* 左滑→左转 */
void Joy_Right(void) { Servo_PlayScript(scr_turn_right,    SCRIPT_LEN(scr_turn_right),    1); }  /* 右滑→右转 */
void Joy_Stop(void)  { Servo_PlayScript(scr_stand,         SCRIPT_LEN(scr_stand),         0); }  /* 回中→收腿站立(单次,斜坡落位) */

/* ==================== 非阻塞动作执行器 ==================== */
/*
 * 每次调用只执行一小步，不阻塞主循环
 * 返回 1 表示动作仍在执行，0 表示动作完成/空闲
 */
u8 handle_action(void)
{
    /* 斜坡层每轮主循环推进一次(内部按 SERVO_RAMP_TICK_MS 节流):
     * 无论脚本处于哪种状态, 都把各轴向目标角平滑逼近, 消除硬跳变;
     * 空闲/回中时也让腿平滑收到站立角 */
    Servo_RampTick();

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
	//Servo_Leg_FL(0);  // PB8 左前脚
    //Servo_Leg_FR(0);  // PB6 右前脚
  //Servo_Leg_BL(0);  // PB9 左后脚
    Servo_Leg_BR(0);  // PB0 右后脚
	delay_ms(1000);
	  LED0 = !LED0;
	//Servo_Head(60); // PB1 头 占空比7.5%，90度
	//Servo_Tail(60); // PB7 尾巴 占空比7.5%，90度
	//Servo_Leg_FL(60);  // PB8 左前脚
   // Servo_Leg_FR(60);  // PB6 右前脚
   //Servo_Leg_BL(60);  // PB9 左后脚
    Servo_Leg_BR(60);  // PB0 右后脚
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
