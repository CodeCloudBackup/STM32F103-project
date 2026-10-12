#ifndef __BLE_PROTOCOL_H
#define __BLE_PROTOCOL_H

#include "stm32f10x.h"
#include <string.h>
#include "ble_uart.h"
#include "bluetooth_jdy32.h"

/* ==================== 协议常量 ==================== */
#define BLE_HEAD1       0xAA
#define BLE_HEAD2       0x55
#define BLE_TAIL1       0x0D
#define BLE_TAIL2       0x0A
#define BLE_MAX_DATA    64          /* 最大数据域长度 */
#define BLE_FRAME_MAX   (2+1+1+BLE_MAX_DATA+1+2)  /* 最大帧长 */

/* ========== 命令码（与小程序端一致）========== */
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
    CMD_BATTERY       = 0x40,   /* 接收：电量上报 */
    CMD_STATUS_REPORT = 0x41,   /* 接收：状态上报 */
} CmdCode;

/* ========== 动作指令值 ========== */
/* ==================== 动作值 ==================== */
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
    uint8_t  data[BLE_MAX_DATA];
    uint8_t  data_len;
    uint8_t  valid;   /* 1=有效帧, 0=无效 */
} ParsedFrame;


/* ==================== 环形缓冲区 ==================== */
#define BLE_RX_BUF_SIZE 256
typedef struct {
    uint8_t  buf[BLE_RX_BUF_SIZE];
    volatile uint16_t head;   /* 写指针（中断中写） */
    volatile uint16_t tail;   /* 读指针（主循环中读） */
} BleRingBuf;

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
extern BleRingBuf g_ble_ring;
extern volatile uint32_t g_sys_tick_ms;  /* 由 TIM2 中断递增 */
extern volatile uint16_t g_isr_rx_cnt;   /* ISR 收到的总字节数（调试用） */
extern volatile uint16_t g_pop_cnt;      /* 状态机消费的总字节数（调试用） */
extern volatile ParseState g_parse_state_dbg;
/* ==================== 函数声明 ==================== */
void BLE_Protocol_Init(uint32_t baud);
u8  BLE_PollFrame(ParsedFrame *frame);  /* 主循环中轮询解析 */
void BLE_SendByte(uint8_t byte);
void BLE_SendArray(const uint8_t *data, uint16_t len);
void BLE_SendFrame(CmdCode cmd, const uint8_t *data, uint8_t data_len);



#endif

