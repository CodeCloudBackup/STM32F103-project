#ifndef __USART_PROTOCOL_H
#define __USART_PROTOCOL_H

#include "stm32f10x.h"
#include <string.h>

/* ==================== 协议常量 ==================== */
#define USART_HEAD1     0xAA
#define USART_HEAD2     0x55
#define USART_TAIL1     0x0D
#define USART_TAIL2     0x0A
#define USART_MAX_DATA  64          /* 最大数据域长度 */
#define USART_FRAME_MAX (2+1+1+USART_MAX_DATA+1+2)  /* 最大帧长 */

/* ==================== 命令码（与上位机端一致）==================== */
typedef enum {
    CMD_HEARTBEAT     = 0x01,		// 心跳
    CMD_STATUS_REQ    = 0x02,		// 状态请求
    CMD_ERROR_REPORT  = 0x03,   /* 接收：错误码上报 */
    CMD_LEG_RESET     = 0x10,		// 四足复位
    CMD_REBOOT        = 0x11,		// 机器重启
    CMD_JOYSTICK      = 0x20,		// 摇杆坐标
    CMD_HEADING       = 0x21,		// 滑杆角度
    CMD_ACTION        = 0x30,		// 统一动作指令（12个按键合并）
    CMD_EXPRESSION    = 0x31,   /* 表情切换指令 */
    CMD_LOTTERY       = 0x06,   /* 抽奖指令(新增专用命令码, 避开 0x31/0x05 与正常表情的冲突) */
    CMD_BATTERY       = 0x40,   /* 接收：电量上报 */
    CMD_STATUS_REPORT = 0x41,   /* 接收：状态上报 */
} CmdCode;

/* ==================== 动作指令值 ==================== */
typedef enum {
    ACT_STAND       = 0x01,
    ACT_LIE_BACK    = 0x02,
    ACT_SIT_DOWN    = 0x03,
    ACT_LIE_FORWARD = 0x04,
    ACT_SHAKE_HEAD  = 0x05,
    ACT_WAG_TAIL    = 0x06,
    ACT_WAVE        = 0x07,
    ACT_MARK        = 0x08,
    ACT_CRAWL       = 0x09,
    ACT_PUSH_UP     = 0x0A,
    ACT_SWAY        = 0x0B,
    ACT_ROCK        = 0x0C,
    /* 预定运动动作(追加): 循环步态跑满 20s 后自动收腿站立 */
    ACT_MOVE_FORWARD = 0x0D,   /* 前进20s */
    ACT_MOVE_BACK    = 0x0E,   /* 后退20s */
    ACT_TURN_LEFT    = 0x0F,   /* 左转20s */
    ACT_TURN_RIGHT   = 0x10,   /* 右转20s */
} ActionValue;

/* ==================== 表情类型 ==================== */
typedef enum {
    EXP_HAPPY   = 0x00,
    EXP_CRY     = 0x01,
    EXP_DAZE    = 0x02,
    EXP_ANGRY   = 0x03,
    EXP_SLEEP   = 0x04,
    EXP_NORMAL  = 0x05,  /* 正常: 圆角正方形双眼(追加,不影响旧ID) */
} ExpressionType;

/* ==================== 解析结果结构体 ==================== */
typedef struct {
    uint8_t  cmd;
    uint8_t  data[USART_MAX_DATA];
    uint8_t  data_len;
    uint8_t  valid;   /* 1=有效帧, 0=无效 */
} ParsedFrame;

/* ==================== 解析状态机状态 ==================== */
typedef enum {
    STATE_WAIT_HEAD1,
    STATE_WAIT_HEAD2,
    STATE_WAIT_CMD,
    STATE_WAIT_LEN,
    STATE_WAIT_DATA,
    STATE_WAIT_CHECKSUM,
    STATE_WAIT_TAIL1,
    STATE_WAIT_TAIL2,
} ParseState;

/* ==================== 全局变量 ==================== */
extern volatile uint32_t g_sys_tick_ms;  /* 由 TIM2 中断递增 */

/* ==================== 函数声明 ==================== */
/* 初始化 USART3(PB10/PB11, baud) 接收通道; USART1(PA9/PA10) 由 uart_init 建立 */
void USART_Protocol_Init(uint32_t baud);
/* 轮询 USART1 与 USART3 两路接收缓冲, 解析到一帧返回 1, 否则返回 0 */
u8   USART_PollFrame(ParsedFrame *frame);
/* 以下发送固定走 USART1(PA9) */
void USART_SendByte(uint8_t byte);
void USART_SendArray(const uint8_t *data, uint16_t len);
void USART_SendFrame(CmdCode cmd, const uint8_t *data, uint8_t data_len);
/* 同格式帧发送到 USART3(PB10=TX): 供摇尾等指令转发给下游设备 */
void USART3_SendFrame(CmdCode cmd, const uint8_t *data, uint8_t data_len);

#endif
