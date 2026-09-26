#include "func.h"

 
void handle_heartbeat(void)
{
	printf("heartbeat handle\r\n");
	BLE_SendFrame(CMD_HEARTBEAT, NULL, 0);
}

void handle_leg_reset(void)
{
	printf("leg_reset handle\r\n");
}

u8 g_current_expr_id =0;
u8 g_expr_dirty =0 ;
u8 g_target_expr_id =0;

/* ==================== 表情绘制 ====================
 * 全部用图元直接写入 OLED_GRAM，不保存任何位图数组(0额外字节RAM/Flash)。
 * 纯整数运算，不使用<math.h>与浮点。绘制后统一由 OLED_Refresh() 刷新一次。
 * 画布 128x64，左眼中心(40,24) 右眼中心(88,24) 嘴中心 x=64
 */
static void expr_dot(int x, int y)                 /* 带越界保护的画点 */
{
	if(x >= 0 && x < 128 && y >= 0 && y < 64)
		OLED_DrawPoint((u8)x, (u8)y, 1);
}

static void expr_fill_circle(int cx, int cy, int r)/* 实心圆 */
{
	int dx, dy;
	for(dy = -r; dy <= r; dy++)
		for(dx = -r; dx <= r; dx++)
			if(dx*dx + dy*dy <= r*r) expr_dot(cx + dx, cy + dy);
}

static void expr_ring(int cx, int cy, int r)       /* 约2px厚空心圆 */
{
	int dx, dy, d;
	for(dy = -r; dy <= r; dy++)
		for(dx = -r; dx <= r; dx++){
			d = dx*dx + dy*dy;
			if(d <= r*r && d >= (r-2)*(r-2)) expr_dot(cx + dx, cy + dy);
		}
}

static void expr_line(int x0, int y0, int x1, int y1)/* Bresenham直线 */
{
	int dx = x1 - x0, dy = y1 - y0;
	int sx = (dx >= 0) ? 1 : -1;
	int sy = (dy >= 0) ? 1 : -1;
	int err, e2;
	if(dx < 0) dx = -dx;
	if(dy < 0) dy = -dy;
	err = dx - dy;
	while(1){
		expr_dot(x0, y0);
		if(x0 == x1 && y0 == y1) break;
		e2 = 2 * err;
		if(e2 > -dy){ err -= dy; x0 += sx; }
		if(e2 <  dx){ err += dx; y0 += sy; }
	}
}

static void expr_line_thick(int x0,int y0,int x1,int y1,int t)/* 加粗直线(横向偏移t) */
{
	int i;
	for(i = -t; i <= t; i++)
		expr_line(x0 + i, y0, x1 + i, y1);
}

static void expr_mouth(int cx, int cy, int hw, int depth, int dir)
{
	int dx, k, dy;                         /* 抛物线嘴: dir>0微笑U, dir<0撇嘴∩ */
	for(dx = -hw; dx <= hw; dx++){
		dy = (dx * dx * depth) / (hw * hw);
		for(k = 0; k < 2; k++)
			if(dir > 0) expr_dot(cx + dx, cy + dy + k);
			else        expr_dot(cx + dx, cy - dy - k);
	}
}

static void expr_face_happy(void)         /* 开心: 弯弯笑眼 + U形笑嘴 */
{
	expr_line(32, 28, 40, 20); expr_line(40, 20, 48, 28);   /* 左眼 ^ */
	expr_line(80, 28, 88, 20); expr_line(88, 20, 96, 28);   /* 右眼 ^ */
	expr_mouth(64, 38, 20, 12, 1);                          /* 微笑 */
}

static void expr_face_cry(void)           /* 哭泣: 圆眼 + 泪滴 + 撇嘴 */
{
	expr_fill_circle(40, 24, 5); expr_fill_circle(88, 24, 5);   /* 眼睛 */
	expr_line_thick(40, 30, 40, 44, 1); expr_fill_circle(40, 47, 3);/* 左泪 */
	expr_line_thick(88, 30, 88, 44, 1); expr_fill_circle(88, 47, 3);/* 右泪 */
	expr_mouth(64, 54, 16, 9, -1);                               /* 撇嘴 */
}

static void expr_face_daze(void)          /* 发呆: 大瞪眼(空心圈+瞳点) + O形嘴 */
{
	expr_ring(40, 24, 9); expr_fill_circle(40, 24, 2);   /* 左瞪眼 */
	expr_ring(88, 24, 9); expr_fill_circle(88, 24, 2);   /* 右瞪眼 */
	expr_ring(64, 46, 4);                                /* O形嘴 */
}

static void expr_face_angry(void)         /* 生气: 倒八字眉 + 小眼 + 撇嘴 */
{
	expr_line_thick(30, 14, 50, 24, 1); expr_line_thick(98, 14, 78, 24, 1);/* 眉毛 \\ / */
	expr_fill_circle(42, 30, 4); expr_fill_circle(86, 30, 4);               /* 眼睛 */
	expr_mouth(64, 56, 18, 10, -1);                                        /* 撇嘴 */
}

void handle_expression(void)
{
	OLED_Clear();                        /* 清GRAM(内部会刷新一次空白) */
	switch(g_target_expr_id){
		case EXP_HAPPY: expr_face_happy(); break;
		case EXP_CRY:   expr_face_cry();   break;
		case EXP_DAZE:  expr_face_daze();  break;
		case EXP_ANGRY: expr_face_angry(); break;
		default:        expr_face_daze();  break;
	}
	OLED_Refresh();                      /* 一次性把表情刷到屏上 */
}

void handle_reboot(void)
{
	printf("Reboot command\r\n");
  NVIC_SystemReset();
}

void handle_joystick(u8 dataX, u8 datay)
{
	printf("reboot handle\r\n");
}

void handle_heading(u8 angle)
{
	printf("action heading\r\n");
}


void handle_status_req(void)
{
	u8 status[4] = {0};
  /* TODO: 填充实际状态：电量、模式、使能、连接状态 */
  BLE_SendFrame(CMD_STATUS_REPORT, status, 4);
	// printf("status_req handle\r\n");
}

ActionState g_action_state = ACTION_IDLE;
uint32_t    g_action_start_tick = 0;
u8     g_action_param = 0;

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

    case ACTION_LEG_RESET:
        /* 示例：复位动作持续 500ms，分 5 步执行，每步 100ms */
        if (g_sys_tick_ms - g_action_start_tick < 500)
        {
            /* 每 100ms 执行一步舵机调整 */
            if ((g_sys_tick_ms - g_action_start_tick) % 100 == 0)
            {
                /* TODO: 在这里写入单步舵机控制代码 */
                /* 例如：Servo_SetAngle(leg_id, target_angle); */
            }
            return 1; /* 仍在执行 */
        }
        else
        {
            g_action_state = ACTION_IDLE; /* 动作完成 */
            printf("Leg reset completed\r\n");
            return 0;
        }

    case ACTION_CUSTOM:
        /* 示例：自定义动作持续 300ms */
        if (g_sys_tick_ms - g_action_start_tick < 300)
        {
            if ((g_sys_tick_ms - g_action_start_tick) % 100 == 0)
            {
                /* TODO: 根据 g_action_param 执行对应动作单步 */
            }
            return 1;
        }
        else
        {
            g_action_state = ACTION_IDLE;
            printf("Action 0x%02X completed\r\n", g_action_param);
            return 0;
        }

    default:
        g_action_state = ACTION_IDLE;
        return 0;
    }
}

 
 // 硬件功能验证函数
 void Servo_Test(void)
 {
	//SG90_Set_Angle(0, 1);  //占空比2.5%，0度
	//SG90_Set_Angle(0, 2);  //占空比2.5%，0度
	SG90_Set_Angle(0, 3);  //占空比2.5%，0度
	SG90_Set_Angle(0, 4);  //占空比2.5%，0度
	delay_ms(1000);
	//SG90_Set_Angle(30, 1);  //占空比2.5%，0度
	//SG90_Set_Angle(60, 2);  //占空比2.5%，0度
	SG90_Set_Angle(180, 3);  //占空比7.5%，90度
	SG90_Set_Angle(180, 4);  //占空比7.5%，90度
	delay_ms(1000); 
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
