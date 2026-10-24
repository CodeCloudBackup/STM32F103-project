#include "func.h"
#include "timer.h"
#include "expression.h"
#include "touch.h"
/************************************************
 * SmartDog 串口协议 bring-up
 *   USART1 (PA9/PA10 @115200): 接收上位机送来的协议命令帧
 *   USART3 (PB10=TX  @115200): 作为打印输出口(usart1_printf)
 *   主循环: 解析 USART1 收到的数据, 打印"需要执行的操作";
 *           表情指令(CMD_EXPRESSION)经脏标记+轮询真正刷新到 OLED,
 *           表情ID实际变化时同步把 CMD_EXPRESSION 帧发送到 USART3,
 *           摇尾指令(CMD_ACTION/ACT_WAG_TAIL)与 PA0 触摸点击都把动作帧转发到 USART3,
 *           其余预定动作指令(CMD_ACTION)由 handle_action_cmd 启动姿势/步态脚本,
 *           主循环 handle_action 非阻塞逐步执行(预定运动 20s 后自动收腿站立),
 *           也不直接打印原始命令字节
 *   注意: 字符串字面量只用 ASCII(Keil/ARMCC 按 GBK 读取 UTF-8 源文件时,
 *         中文字符串可能产生 0x5C 破坏右引号); 中文注释不受影响
 ************************************************/

/* 动作码 -> 待执行操作名(与 func.c 的 handle_action_cmd 分发保持一致) */
static const char *action_name(u8 a)
{
	switch (a)
	{
	case ACT_STAND:        return "Stand";
	case ACT_SIT_DOWN:     return "Sit down";
	case ACT_LIE_FORWARD:  return "Lie forward";
	case ACT_LIE_BACK:     return "Lie back";
	case ACT_WAVE:         return "Wave/Handshake";
	case ACT_MARK:         return "Mark";
	case ACT_MOVE_FORWARD: return "Move forward 20s";
	case ACT_MOVE_BACK:    return "Move back 20s";
	case ACT_TURN_LEFT:    return "Turn left 20s";
	case ACT_TURN_RIGHT:   return "Turn right 20s";
	case ACT_SHAKE_HEAD:   return "Shake head (removed,no-op)";
	case ACT_WAG_TAIL:     return "Wag tail (removed,no-op)";
	default:               return "Unknown action";
	}
}

/* 表情码 -> 待切换表情(与 func.c 的表情处理一致) */
static const char *expr_name(u8 e)
{
	switch (e)
	{
	case EXP_HAPPY:  return "Happy";
	case EXP_CRY:    return "Cry";
	case EXP_DAZE:   return "Daze";
	case EXP_ANGRY:  return "Angry";
	case EXP_SLEEP:  return "Sleep";
	case EXP_NORMAL: return "Normal";
	default:         return "Unknown expression";
	}
}

/* ==================== 表情变化执行(非阻塞) ====================
 * 遵循"脏标记 + 主循环轮询"约定: 收到表情指令只置目标与脏标记,
 * 真正的清屏/绘制/刷新放到 expression_poll 中执行, 避免阻塞命令解析
 */
/* 请求切换表情: 记录目标ID并置脏标记(已是该表情则不重复刷新) */
static void expression_request(u8 expr_id)
{
	if (g_target_expr_id == expr_id && g_current_expr_id == expr_id)
		return;
	g_target_expr_id = expr_id;
	g_expr_dirty     = 1;
}

/* 表情变化执行: 若有脏标记则绘制并刷新到 OLED, 更新当前表情ID;
 * 表情ID确有变化时同步把 CMD_EXPRESSION 帧发送到 USART3(与摇尾转发同格式);
 * 仅眼睛偏移变化(ID不变)不重复发送 */
static void expression_poll(void)
{
	u8 prev_expr_id;

	if (!g_expr_dirty)
		return;
	prev_expr_id = g_current_expr_id;
	handle_expression();                 /* 清GRAM + 绘制目标表情 + 刷新 */
	g_current_expr_id = g_target_expr_id;/* 记录已显示表情 */
	g_expr_dirty      = 0;
	if (g_current_expr_id != prev_expr_id)
	{
		USART3_SendFrame(CMD_EXPRESSION, &g_current_expr_id, 1);  /* 表情切换指令同步发送给 USART3 */
		usart1_printf("expr 0x%02X -> USART3\r\n", g_current_expr_id);
	}
}

/* 摇杆坐标 -> 待执行运动(复用 handle_joystick 的死区/主导轴阈值) */
static const char *joystick_op(u8 dataX, u8 dataY)
{
	int dx = (int)dataX - 128;              /* >0 右, <0 左 */
	int dy = (int)dataY - 128;             /* >0 前, <0 后 */
	int adx = (dx < 0) ? -dx : dx;
	int ady = (dy < 0) ? -dy : dy;

	if (adx < 25 && ady < 25)   return "Center -> stand (stop)";
	if (adx > ady + 15)         return (dx > 0) ? "Turn right" : "Turn left";
	if (ady > adx + 15)         return (dy > 0) ? "Move forward" : "Move back";
	return "Diagonal zone (keep last gait)";
}

/* 解析一帧数据, 打印其对应需要执行的操作 */
static void report_operation(const ParsedFrame *f)
{
	switch (f->cmd)
	{
	case CMD_HEARTBEAT:
		usart1_printf("OP: heartbeat -> return real status frame(excl. battery)\r\n");
		handle_heartbeat();   /* 心跳: 回发真实状态帧 CMD_STATUS_REPORT(电量除外) */
		break;

	case CMD_STATUS_REQ:
		usart1_printf("OP: status request -> return heartbeat frame\r\n");
		handle_heartbeat();  /* 状态请求: 回发一帧心跳 CMD_HEARTBEAT */
		break;

	case CMD_LEG_RESET:
		usart1_printf("OP: leg reset -> play stand script\r\n");
		break;

	case CMD_REBOOT:
		usart1_printf("OP: reboot device\r\n");
		break;

	case CMD_JOYSTICK:
		if (f->data_len >= 2)
			usart1_printf("OP: joystick -> %s (X=%d,Y=%d)\r\n",
				joystick_op(f->data[0], f->data[1]), f->data[0], f->data[1]);
		else
			usart1_printf("OP: joystick data too short to parse\r\n");
		break;

	case CMD_HEADING:
		usart1_printf("OP: heading slider -> head/tail removed, no-op\r\n");
		break;

	case CMD_ACTION:
		if (f->data_len >= 1)
		{
			usart1_printf("OP: exec action -> %s\r\n", action_name(f->data[0]));
			if (f->data[0] == ACT_WAG_TAIL)
				handle_wag_tail();   /* 摇尾: 本机无尾舵机, 把动作帧同步发送到 USART3 */
			else
				handle_action_cmd(f->data[0]);  /* 预定动作: 启动脚本, 由 handle_action 轮询执行 */
		}
		else
			usart1_printf("OP: action data too short to parse\r\n");
		break;

	case CMD_EXPRESSION:
		if (f->data_len >= 1)
		{
			if (f->data[0] == 0x06)        /* AA 55 31 01 06 37 0D 0A: 抽奖(3圈) */
			{
				usart1_printf("OP: lottery -> stop + stand + wheel\r\n");
				handle_lottery_trigger();
			}
			else if (f->data[0] == 0x07)   /* AA 55 31 01 07 38 0D 0A: 抽奖(单圈,转完停在谢谢惠顾) */
			{
				usart1_printf("OP: lottery once -> stop + stand + wheel(1 round)\r\n");
				handle_lottery_once();
			}
			else
			{
				usart1_printf("OP: switch expression -> %s\r\n", expr_name(f->data[0]));
				expression_request(f->data[0]);   /* 真正切换到 OLED(轮询中刷新) */
			}
		}
		else
			usart1_printf("OP: expression data too short to parse\r\n");
		break;

	case CMD_LOTTERY:
		usart1_printf("OP: lottery -> stop + stand + wheel\r\n");
		handle_lottery_trigger();  /* 抽奖: 停所有动作+立正, 启动转盘动画(非阻塞) */
		break;

	case CMD_BATTERY:
		if (f->data_len >= 1)
			usart1_printf("OP: battery report = %d%%\r\n", f->data[0]);
		break;

	default:
		usart1_printf("OP: undefined cmd(0x%02X), no-op\r\n", f->cmd);
		break;
	}
}

int main(void)
{
	ParsedFrame frame;

	// 中断优先级分组2: 2位抢占优先级 + 2位响应优先级
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	TIM2_Init();                   // 系统毫秒滴答(脚本引擎/斜坡层计时)
	uart_init(115200);             // USART1: 接收上位机蓝牙协议命令
	USART_Protocol_Init(9600);   // 初始化协议环形缓冲 + USART3(PB10/PB11)语音模块打印口
	SG90_Servo_Init();             // TIM3四路PWM(PA6/PA7/PB0/PB1): 预定动作执行前提
	Servo_RampInit();              // 斜坡层复位: 当前角=目标角=站立, 避免上电甩腿
	OLED_Init();                   // SSD1306(软件I2C: PB8=SCL, PB9=SDA), 默认扫描配置即表情所依赖
	touch_Init();                  // PA0 三线触摸开关: 上拉输入 + EXTI0 双边沿(消抖/连击合并)

	/* 开机默认表情: 正常 */
	g_target_expr_id = EXP_NORMAL;
	g_expr_dirty     = 1;
	expression_poll();             // 立即刷出开机脸

	usart1_printf("SmartDog boot: USART1 cmd-RX @115200, USART3 print @115200\r\n");

	while (1)
	{
		// 轮询解析收到的完整协议帧(USART1 为本演示的命令输入源)
		while (USART_PollFrame(&frame))
		{
			// 先解析数据含义, 再打印需要执行的操作
			report_operation(&frame);
		}
		handle_action();           // 预定动作脚本非阻塞推进(含抽奖期间的立正斜坡落位)
		if (lottery_is_active())   // 抽奖优先: 转盘占用 OLED, 期间不刷新表情/不响应触摸
			lottery_poll();        // 非阻塞逐帧推进转盘, 结束时自动发 USART3 结果帧
		else
		{
			handle_wag_expr();     // 摇尾开心表情满 5s 则恢复原表情(置脏, 下一句刷新)
			expression_poll();     // 有待刷新表情时非阻塞更新 OLED
			if (touch_poll())      // PA0 触摸一次有效点击(已消抖/合并连击)
				handle_wag_tail(); // 执行摇尾指令(同步发送给 USART3)
		}
	}
}
