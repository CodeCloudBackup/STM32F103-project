#include "usart_protocol.h"
#include "sys.h"
#include "usart.h"
#include <stdio.h>

#define USART_RX_BUF_SIZE 256

/* ==================== 单路接收上下文 ====================
 * 每路串口独立一套"环形缓冲 + 解析状态机", 避免 USART1/USART3 字节
 * 交叉进入同一状态机导致帧错乱
 */
typedef struct {
    uint8_t         buf[USART_RX_BUF_SIZE];
    volatile uint16_t head;   /* 写指针(中断中写) */
    volatile uint16_t tail;   /* 读指针(主循环中读) */
} UsartRing;

typedef struct {
    UsartRing  ring;
    ParseState state;
    uint8_t    parse_buf[USART_FRAME_MAX];
    uint8_t    idx;
    uint8_t    data_len;
    uint8_t    checksum;
} UsartProto;

static UsartProto s_u1;   /* USART1(PA10) 接收实例 */
static UsartProto s_u3;   /* USART3(PB11) 接收实例 */

/* ==================== 环形缓冲操作 ==================== */
static void ring_init(UsartProto *p)
{
    memset(p->ring.buf, 0, USART_RX_BUF_SIZE);
    p->ring.head  = 0;
    p->ring.tail  = 0;
    p->state      = STATE_WAIT_HEAD1;
    p->idx        = 0;
    p->data_len   = 0;
    p->checksum   = 0;
}

static void ring_push(UsartProto *p, u8 byte)
{
    u16 next = (p->ring.head + 1) % USART_RX_BUF_SIZE;
    if (next != p->ring.tail)         /* 未满则写入, 满则丢弃 */
    {
        p->ring.buf[p->ring.head] = byte;
        p->ring.head = next;
    }
}

static u8 ring_pop(UsartProto *p, u8 *byte)
{
    if (p->ring.tail == p->ring.head)
        return 0;                     /* 空 */
    *byte = p->ring.buf[p->ring.tail];
    p->ring.tail = (p->ring.tail + 1) % USART_RX_BUF_SIZE;
    return 1;
}

/* USART3 硬件初始化已集中到 SYSTEM/usart.c 的 USART3_Init(PB10=TX, PB11=RX);
 * USART1 由同文件的 uart_init(PA9/PA10) 建立. 本文件只做协议解析,
 * USART_Protocol_Init 直接调用 usart.c 提供的串口初始化接口.
 */

void USART_Protocol_Init(u32 baud)
{
    ring_init(&s_u1);
    ring_init(&s_u3);
    USART3_Init(baud);
}

/* ==================== 接收中断: 各自推入本路环形缓冲 ==================== */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
        ring_push(&s_u1, (u8)USART_ReceiveData(USART1));
    if (USART_GetFlagStatus(USART1, USART_FLAG_ORE) != RESET)
        USART_ReceiveData(USART1);    /* 读 DR 清除 ORE, 防止卡死 */
}

void USART3_IRQHandler(void)
{
    if (USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)
        ring_push(&s_u3, (u8)USART_ReceiveData(USART3));
    if (USART_GetFlagStatus(USART3, USART_FLAG_ORE) != RESET)
        USART_ReceiveData(USART3);
}

/* ==================== 发送函数(固定 USART1/PA9) ==================== */
void USART_SendByte(u8 byte)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, byte);
}

void USART_SendArray(const u8 *data, u16 len)
{
    u16 i;
    for (i = 0; i < len; i++)
        USART_SendByte(data[i]);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
}

/* 组帧: 帧头 + 命令 + 长度 + 数据 + 累加校验 + 帧尾, 返回整帧字节数 */
static u16 proto_build_frame(u8 *frame, CmdCode cmd, const u8 *data, u8 data_len)
{
    u16 offset = 0;
    u16 i;
    u8  checksum = 0;

    frame[offset++] = USART_HEAD1;
    frame[offset++] = USART_HEAD2;
    frame[offset++] = (u8)cmd;
    frame[offset++] = data_len;
    for (i = 0; i < data_len; i++)
        frame[offset++] = data[i];
    for (i = 0; i < offset; i++)          /* 校验: 帧头到数据域累加和取低8位 */
        checksum += frame[i];
    frame[offset++] = checksum;
    frame[offset++] = USART_TAIL1;
    frame[offset++] = USART_TAIL2;
    return offset;
}

/* 打包并发送一帧到 USART1(PA9) */
void USART_SendFrame(CmdCode cmd, const u8 *data, u8 data_len)
{
    u8  frame[USART_FRAME_MAX];
    u16 offset = proto_build_frame(frame, cmd, data, data_len);
    USART_SendArray(frame, offset);
}

/* 打包并发送一帧到 USART3(PB10=TX): 与 USART_SendFrame 同格式, 仅换输出口 */
void USART3_SendFrame(CmdCode cmd, const u8 *data, u8 data_len)
{
    u8  frame[USART_FRAME_MAX];
    u16 offset = proto_build_frame(frame, cmd, data, data_len);
    u16 i;

    for (i = 0; i < offset; i++)
    {
        while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET);
        USART_SendData(USART3, frame[i]);
    }
    while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET);
}

/* ==================== 单路解析状态机(主循环轮询) ==================== */
static u8 proto_poll(UsartProto *p, ParsedFrame *frame)
{
    u8 byte;
    while (ring_pop(p, &byte))
    {
        switch (p->state)
        {
        case STATE_WAIT_HEAD1:
            if (byte == USART_HEAD1)
            {
                p->idx = 0;
                p->parse_buf[p->idx++] = byte;
                p->checksum = byte;
                p->state = STATE_WAIT_HEAD2;
            }
            break;

        case STATE_WAIT_HEAD2:
            if (byte == USART_HEAD2)
            {
                p->parse_buf[p->idx++] = byte;
                p->checksum += byte;
                p->state = STATE_WAIT_CMD;
            }
            else
            {
                p->state = STATE_WAIT_HEAD1;
                if (byte == USART_HEAD1)  /* 该字节恰为帧头, 重新进入 HEAD1 */
                {
                    p->idx = 0;
                    p->parse_buf[p->idx++] = byte;
                    p->checksum = byte;
                    p->state = STATE_WAIT_HEAD2;
                }
            }
            break;

        case STATE_WAIT_CMD:
            p->parse_buf[p->idx++] = byte;
            p->checksum += byte;
            p->state = STATE_WAIT_LEN;
            break;

        case STATE_WAIT_LEN:
            p->data_len = byte;
            p->parse_buf[p->idx++] = byte;
            p->checksum += byte;
            if (p->data_len == 0)
                p->state = STATE_WAIT_CHECKSUM;
            else if (p->data_len > USART_MAX_DATA)
            {
                p->idx = 0;
                p->checksum = 0;
                p->state = STATE_WAIT_HEAD1;
            }
            else
                p->state = STATE_WAIT_DATA;
            break;

        case STATE_WAIT_DATA:
            if (p->idx >= USART_FRAME_MAX)    /* 防越界 */
            {
                p->idx = 0;
                p->checksum = 0;
                p->state = STATE_WAIT_HEAD1;
                break;
            }
            p->parse_buf[p->idx++] = byte;
            p->checksum += byte;
            p->data_len--;
            if (p->data_len == 0)
                p->state = STATE_WAIT_CHECKSUM;
            break;

        case STATE_WAIT_CHECKSUM:
            if (byte == (u8)(p->checksum & 0xFF))
            {
                p->parse_buf[p->idx++] = byte;
                p->state = STATE_WAIT_TAIL1;
            }
            else
            {
                p->idx = 0;
                p->checksum = 0;
                p->state = STATE_WAIT_HEAD1;
                printf("===Checksum FAILED!Byte:0x%02X\r\n", byte);
            }
            break;

        case STATE_WAIT_TAIL1:
            if (byte == USART_TAIL1)
            {
                p->parse_buf[p->idx++] = byte;
                p->state = STATE_WAIT_TAIL2;
            }
            else
            {
                p->idx = 0;
                p->checksum = 0;
                p->state = STATE_WAIT_HEAD1;
                printf("===TAIL1 FAILED!Byte:0x%02X\r\n", byte);
            }
            break;

        case STATE_WAIT_TAIL2:
            if (byte == USART_TAIL2)
            {
                p->parse_buf[p->idx++] = byte;
                /* ===== 一帧完整解析成功 ===== */
                frame->cmd      = p->parse_buf[2];
                frame->data_len = p->parse_buf[3];
                memcpy(frame->data, &p->parse_buf[4], frame->data_len);
                frame->valid    = 1;

                p->state = STATE_WAIT_HEAD1;
                p->idx = 0;
                p->checksum = 0;
                return 1;
            }
            else
            {
                p->idx = 0;
                p->checksum = 0;
                p->state = STATE_WAIT_HEAD1;
                printf("===TAIL2 FAILED!Byte:0x%02X\r\n", byte);
            }
            break;

        default:
            p->idx = 0;
            p->checksum = 0;
            p->state = STATE_WAIT_HEAD1;
            break;
        }
    }
    return 0;
}

/* ==================== 对外轮询: 先解析 USART1, 再 USART3 ==================== */
u8 USART_PollFrame(ParsedFrame *frame)
{
    if (proto_poll(&s_u1, frame)) return 1;
    if (proto_poll(&s_u3, frame)) return 1;
    return 0;
}
