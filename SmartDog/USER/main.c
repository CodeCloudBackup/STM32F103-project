#include "func.h"
#include "expression.h"
/************************************************

************************************************/


 int main(void)
 {	
	//char recvBuf[128];
	// 设置中断优先级分组为组2，2位抢占优先级，2位响应优先级	
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);  
	LED_Init();		  		// LED 初始化
	TIM2_Init(); 				// 轮询定时器初始化
	uart_init(115200);
	//USART2_Init(115200);
	 printf("usart driver init success\r\n");
	SG90_Servo_Init();  // SERVO 初始化
	 printf("servo driver init success\r\n");
	OLED_Init();				// OLED 初始化
	 printf("OLED driver init success\r\n");
	MY_Memory_Init(SRAMIN);		// 初始化内存池
	  printf("memory driver init success\r\n");
	BLE_Protocol_Init(9600);
	 printf("BLE_Protocol driver init success\r\n");
		
	// 否则硬件复位会清空电荷泵/寻址模式等配置且不再重新初始化，导致黑屏
	OLED_Display_On();
	OLED_Clear();
	OLED_DisplayTurn(1);

	//Expression_Test();
	/* 开机默认显示"发呆"表情，之后由蓝牙CMD_EXPRESSION(0x31)切换 */
	g_target_expr_id  = EXP_SLEEP;
	handle_expression();
	g_current_expr_id = g_target_expr_id;
	
	
	// 初始化舵机角
	handle_leg_reset();
	//Servo_Test();
	 // ===== 各任务时间戳 ===== 
	u32 last_status_tick  = 0;
	u32 last_expr_refresh_tick = 0;
	u32 last_valid_tick = 0;   /* 最近一次收到心跳/有效指令的时刻 */
	ParsedFrame frame;
	
	while(1)
	{
		
	  // ===== 任务1：蓝牙接收与解析（最高优先级，每轮必执行） ===== 
		// 先轮询接收字节，再解析完整帧 
		 while (BLE_PollFrame(&frame))
     {
			 printf("recv cmd=[%d]\r\n", frame.cmd);
			 last_valid_tick = g_sys_tick_ms;  /* 收到有效帧,刷新超时计时 */
			 switch (frame.cmd)
			 {
            case CMD_HEARTBEAT:
                /* 收到心跳后返回电量和状态 */
                handle_status_req();
                break;

            case CMD_LEG_RESET:
                if (frame.data_len >= 1)
                {
                    printf("LEG_RESET/ACT_STAND 0x%02X triggered\r\n", frame.data[0]);
                    handle_action_cmd(ACT_STAND); /* 分发到对应姿势脚本 */
                }
                break;

            case CMD_REBOOT:
								handle_reboot();
                break;

            case CMD_JOYSTICK:
                if (frame.data_len >= 2)
                {
					printf("joystick [0x%02X : 0x%02X]triggered\r\n", frame.data[0], frame.data[1]);
                   handle_joystick(frame.data[0], frame.data[1]);
                }
                break;

            case CMD_HEADING:
                if (frame.data_len >= 1)
                {
					printf("Action 0x%02X triggered\r\n", frame.data[0]);
                    handle_heading(frame.data[0]);
                }
                break;

            case CMD_ACTION:
                if (frame.data_len >= 1)
                {
                    printf("Action 0x%02X triggered\r\n", frame.data[0]);
                    handle_action_cmd(frame.data[0]); /* 分发到对应姿势脚本 */
                }
                break;

            case CMD_EXPRESSION:
                if (frame.data_len >= 1)
                {
                    uint8_t new_expr = frame.data[0];
										if (new_expr != g_current_expr_id) 
										{
											g_target_expr_id  = new_expr;
											g_expr_dirty = 1; 
										}
                }
                break;

            case CMD_STATUS_REQ:
                {
                    handle_status_req();
                }
                break;

            default:
                printf("Unknown cmd: 0x%02X\r\n", frame.cmd);
                break;
				}
		 }

		// ===== 任务2：通信超时保护（5s停动作 / 10min待机） ===== 
		handle_timeout_guard(&last_valid_tick);

		// ===== 任务2.5：转弯表情回正1s后恢复原表情(非阻塞轮询) ===== 
		handle_expr_return();

		// (g_sys_tick_ms - last_status_tick >= 1000)
		{
			last_status_tick = g_sys_tick_ms;
			//printf("rx=%u pop=%u head=%u tail=%u state=%d\r\n",
		//           g_isr_rx_cnt, g_pop_cnt, g_ble_ring.head, g_ble_ring.tail, g_parse_state_dbg);
			// 状态上报已由心跳应答触发,此处不再周期发送
		}
		// ===== 任务3：动作状态机调度 ===== 
		handle_action();
		// ===== 任务4：表情刷新调度 ===== 
		if (g_expr_dirty) {
			if (g_sys_tick_ms - last_expr_refresh_tick >= 50)
			{
				last_expr_refresh_tick = g_sys_tick_ms;
				handle_expression();
				// 3. 刷新完成，更新状态 
				g_current_expr_id = g_target_expr_id;
				g_expr_dirty = 0; // 清除脏标记 
			}
		}

	}
 }

