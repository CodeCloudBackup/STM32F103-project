#include "ble_protocol.h"
#include "timer.h"
#include "usart.h"
#include "led.h"
/* ==================== 全局变量定义 ==================== */
BleRingBuf g_ble_ring;

/* ==================== 状态机解析上下文 ==================== */
/* 放在 .c 文件内部，不对外暴露 */

static ParseState s_parse_state = STATE_WAIT_HEAD1;
static u8    s_parse_buf[BLE_FRAME_MAX];
static u8    s_parse_idx   = 0;
static u8    s_data_len    = 0;
static u8    s_checksum    = 0;
	

/* ==================== 环形缓冲区操作 ==================== */
static void RingBuf_Init(void)
{
    memset(g_ble_ring.buf, 0, BLE_RX_BUF_SIZE);
    g_ble_ring.head = 0;
    g_ble_ring.tail = 0;
}

/* 写一个字节到环形缓冲区（中断中调用） */
static void RingBuf_Push(u8 byte)
{
    u16 next = (g_ble_ring.head + 1) % BLE_RX_BUF_SIZE;
    if (next != g_ble_ring.tail)  /* 缓冲区未满 */
    {
        g_ble_ring.buf[g_ble_ring.head] = byte;
        g_ble_ring.head = next;
    }
    /* 满则丢弃，防止覆盖未读数据 */
}

/* 从环形缓冲区读一个字节（主循环中调用） */
static u8 RingBuf_Pop(u8 *byte)
{
    if (g_ble_ring.tail == g_ble_ring.head)
        return 0;  /* 空 */
    *byte = g_ble_ring.buf[g_ble_ring.tail];
    g_ble_ring.tail = (g_ble_ring.tail + 1) % BLE_RX_BUF_SIZE;
    return 1;  /* 成功 */
}

/* ====================蓝牙初始化 ==================== */
void BLE_Protocol_Init(u32 baud)
{
	BLE_UART_Init(baud);
	/* 初始化环形缓冲区 */
  RingBuf_Init();
	JDY32_Init();
}

volatile u16 g_isr_rx_cnt = 0;   // 中断收到的总字节数
volatile u16 g_pop_cnt = 0;      // 状态机消费的总字节数
volatile ParseState g_parse_state_dbg = STATE_WAIT_HEAD1;  /* 调试用镜像 */

/* ==================== USART2 中断服务函数 ==================== */
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
    {
        u8 data = USART_ReceiveData(USART2);
        RingBuf_Push(data);  /* 写入环形缓冲区 */
        g_isr_rx_cnt++;
        g_parse_state_dbg = s_parse_state;
       
    }
		// 2. 【关键】处理溢出错误 —— 不加这段，收到3个字节就会卡死
    if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) != RESET)
    {
        USART_ReceiveData(USART2);  // 读 DR 清除 ORE
    }
}

/* ==================== 发送函数 ==================== */

void BLE_SendArray(const u8 *data, u16 len)
{
    for (u16 i = 0; i < len; i++)
    {
        BLE_UART2_SendByte(data[i]);
    }
}

/* 打包并发送一帧（与 TS 端的 packFrame 逻辑完全一致） */
void BLE_SendFrame(CmdCode cmd, const u8 *data, u8 data_len)
{
    u8 frame[BLE_FRAME_MAX];
    u16 offset = 0;
    u8 checksum = 0;

    /* 1. 帧头 */
    frame[offset++] = BLE_HEAD1;
    frame[offset++] = BLE_HEAD2;
    /* 2. 命令码 */
    frame[offset++] = (u8)cmd;
    /* 3. 数据长度 */
    frame[offset++] = data_len;
    /* 4. 数据域 */
    for (u8 i = 0; i < data_len; i++)
    {
        frame[offset++] = data[i];
    }
    /* 5. 校验和：帧头到数据域的累加和取低8位 */
    for (u16 i = 0; i < offset; i++)
    {
        checksum += frame[i];
    }
    frame[offset++] = checksum;
    /* 6. 帧尾 */
    frame[offset++] = BLE_TAIL1;
    frame[offset++] = BLE_TAIL2;

    BLE_SendArray(frame, offset);
}

/* ==================== 协议解析状态机（主循环轮询） ==================== */
/*
 * 返回值：1 = 解析到一帧有效数据，0 = 无有效帧
 * 在主循环中循环调用，直到返回 0
 */
u8 BLE_PollFrame(ParsedFrame *frame)
{
    u8 byte;
    while (RingBuf_Pop(&byte))
    {
        g_pop_cnt++;
		//	printf("State:%d, Byte:0x%02X\r\n", s_parse_state, byte); 
        switch (s_parse_state)
        {
        case STATE_WAIT_HEAD1:
            if (byte == BLE_HEAD1)
            {
                s_parse_idx = 0;
                s_parse_buf[s_parse_idx++] = byte;
                s_checksum = byte;
                s_parse_state = STATE_WAIT_HEAD2;
            }
            break;

        case STATE_WAIT_HEAD2:
            if (byte == BLE_HEAD2)
            {
                s_parse_buf[s_parse_idx++] = byte;
                s_checksum += byte;
                s_parse_state = STATE_WAIT_CMD;
            }
            else
            {
                /* 第二个字节不匹配，重新开始 */
                s_parse_state = STATE_WAIT_HEAD1;
                /* 如果这个字节恰好是 0xAA，则重新进入 HEAD1 匹配 */
                if (byte == BLE_HEAD1)
                {
                    s_parse_idx = 0;
                    s_parse_buf[s_parse_idx++] = byte;
                    s_checksum = byte;
                    s_parse_state = STATE_WAIT_HEAD2;
                }
            }
            break;

        case STATE_WAIT_CMD:
            s_parse_buf[s_parse_idx++] = byte;
            s_checksum += byte;
            s_parse_state = STATE_WAIT_LEN;
            break;

        case STATE_WAIT_LEN:
            s_data_len = byte;
            s_parse_buf[s_parse_idx++] = byte;
            s_checksum += byte;
            if (s_data_len == 0)
            {
                s_parse_state = STATE_WAIT_CHECKSUM;
            }
            else if (s_data_len > BLE_MAX_DATA)
            {
                /* 数据长度超限，丢弃此帧 */
								s_parse_idx = 0;
								s_checksum = 0;
                s_parse_state = STATE_WAIT_HEAD1;
            }
            else
            {
                s_parse_state = STATE_WAIT_DATA;
            }
            break;

        case STATE_WAIT_DATA:
					 if (s_parse_idx >= BLE_FRAME_MAX)  /* 防止越界 */
						{
								s_parse_idx = 0;
								s_checksum = 0;
								s_parse_state = STATE_WAIT_HEAD1;
								break;
						}
            s_parse_buf[s_parse_idx++] = byte;
            s_checksum += byte;
            s_data_len--;
            if (s_data_len == 0)
            {
                s_parse_state = STATE_WAIT_CHECKSUM;
            }
            break;

        case STATE_WAIT_CHECKSUM:
        {
            u8 expected_checksum = s_checksum & 0xFF;
			//printf("State:5(CHECKSUM), Byte:0x%02X, Expected:0x%02X\r\n", byte, expected_checksum);
            if (byte == expected_checksum)
            {
				// printf("-> FRAME COMPLETE! cmd=");
                s_parse_buf[s_parse_idx++] = byte;
                s_parse_state = STATE_WAIT_TAIL1;
            }
            else
            {
                /* 校验失败，丢弃此帧 */
								s_parse_idx = 0;
                s_checksum = 0;
                s_parse_state = STATE_WAIT_HEAD1;
								printf("===Checksum FAILED!Byte:0x%02X\r\n",byte);
            }
            break;
        }

        case STATE_WAIT_TAIL1:
						//printf("State:6(TAIL1), Byte:0x%02X\r\n", byte);
            if (byte == BLE_TAIL1)
            {
								//printf("-> FRAME COMPLETE! cmd=");
                s_parse_buf[s_parse_idx++] = byte;
                s_parse_state = STATE_WAIT_TAIL2;
            }
            else
            {
                /* 帧尾不匹配，丢弃 */
								s_parse_idx = 0;
                s_checksum = 0;
                s_parse_state = STATE_WAIT_HEAD1;
								printf("===TAIL1 FAILED!Byte:0x%02X\r\n",byte);
            }
            break;

        case STATE_WAIT_TAIL2:
						//printf("State:7(TAIL2), Byte:0x%02X\r\n", byte);
            if (byte == BLE_TAIL2)
            {
                s_parse_buf[s_parse_idx++] = byte;
								// printf("  -> FRAME COMPLETE! cmd=0x%02X\r\n", s_parse_buf[2]);
                /* ===== 一帧完整解析成功 ===== */
                frame->cmd      = s_parse_buf[2];
                frame->data_len = s_parse_buf[3];
                memcpy(frame->data, &s_parse_buf[4], frame->data_len);
                frame->valid    = 1;

                /* 重置状态机 */
                s_parse_state = STATE_WAIT_HEAD1;
								s_parse_idx = 0;
                s_checksum = 0;
                return 1;  /* 返回成功 */
            }
            else
            {
								s_parse_idx = 0;
                s_checksum = 0;
                s_parse_state = STATE_WAIT_HEAD1;
								printf("===TAIL2 FAILED!Byte:0x%02X\r\n",byte);
            }
            break;

        default:
						s_parse_idx = 0;
            s_checksum = 0;
            s_parse_state = STATE_WAIT_HEAD1;
            break;
        }
    }

    return 0;  /* 缓冲区空或无完整帧 */
}

