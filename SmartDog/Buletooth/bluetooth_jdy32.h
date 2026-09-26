#ifndef __BLUETOOTH_JDY32_H
#define __BLUETOOTH_JDY32_H

#include "sys.h"
#include <stdio.h>
#include <string.h>

/* JDY-32 默认广播名称 */
#define JDY32_DEFAULT_BLE_NAME      "JDY-32-LE"
#define JDY32_DEFAULT_SPP_NAME      "JDY-32-SPP"

/* JDY-32 默认串口波特率 */
#define JDY32_DEFAULT_BAUDRATE      9600

/* JDY-32 默认 SPP 配对密码 */
#define JDY32_DEFAULT_PIN           "1234"

/* AT 指令响应等待时间，单位 ms，可根据实际模块调整 */
#define JDY32_AT_TIMEOUT_MS         200


/* 蓝牙模块相关定义 */
#define JDY32_UART USART2
#define JDY32_PWRC_PORT GPIOA
#define JDY32_PWRC_PIN GPIO_Pin_4
#define JDY32_STAT_PORT GPIOA
#define JDY32_STAT_PIN GPIO_Pin_1

// PA1 STAT
// PA4 PWRX
void JDY32_Init(void);

/**
 * @brief 让 JDY-32 进入睡眠模式（低功耗）
 * @note  拉低 PA4，模块进入睡眠，串口通信停止
 */
void JDY32_Sleep(void) ;

/**
 * @brief 唤醒 JDY-32
 * @note  拉高 PA4，模块退出睡眠，恢复串口通信
 */
void JDY32_WakeUp(void);

/**
 * @brief 检测蓝牙是否已连接
 * @return 1 = 已连接（PA1 高电平），0 = 未连接（PA1 低电平）
 */
u8 JDY32_IsConnected(void);

void JDY32_SendString(const char *str);

/**
 * @brief 发送 AT 指令（不等待响应，用于设置类指令）
 * @param cmd  AT 指令字符串
 */
void JDY32_SendAT_NoReply(const char *cmd);


/**
 * @brief 发送 AT 指令并等待响应
 * @param cmd     AT 指令字符串（不含 \r\n，函数会自动追加）
 * @param resp    接收响应的缓冲区
 * @param resp_len 缓冲区最大长度
 * @param timeout_ms 超时时间（毫秒）
 * @return 1 = 成功收到响应，0 = 超时
 *
 * @example
 *   char buf[64];
 *   JDY32_SendAT("AT+VERSION", buf, sizeof(buf), 500);
 *   // buf 中内容为 "+OK JDY-32-V1.0\r\n"
 */
uint8_t JDY32_SendAT(const char *cmd, char *resp, uint16_t resp_len, uint32_t timeout_ms);

/**
 * @brief  查询 JDY-32 版本号
 */
u8 JDY32_GetVersion(char *buf, u16 len);

/**
 * @brief  查询 BLE MAC 地址
 */
u8 JDY32_GetBleMac(char *buf, u16 len);

/**
 * @brief  查询 SPP MAC 地址
 */
u8 JDY32_GetSppMac(char *buf, u16 len);

/**
 * @brief 设置波特率（参数为索引值）
 * @param baud_idx  1=1200, 2=2400, 3=4800, 4=9600, 5=19200, 6=38400, 7=57600, 8=115200
 */
void JDY32_SetBaud(uint8_t baud_idx);

/**
 * @brief 设置蓝牙广播名称（BLE）
 */
void JDY32_SetBLEName(const char *name);
/**
 * @brief 设置 SPP 广播名称
 */
void JDY32_SetSPPName(const char *name);

/**
 * @brief 设置 SPP 连接密码
 */
void JDY32_SetPIN(const char *pin);

/**
 * @brief  设置 SPP 密码连接类型
 * @param  type 常见值：
 *         0：无需密码
 *         1：认证配对
 *         2：认证配对 + 密码
 * @note   具体取值以 JDY-32 实际手册为准。
 */
void JDY32_SetSppType(u8 type);

/**
 * @brief  断开当前连接
 */
void JDY32_Disconnect(void);

/**
 * @brief  JDY-32 软件复位
 * @note   复位后模块会重启，部分参数修改需要复位后才生效。
 */
void JDY32_SoftReset(void);

/**
 * @brief  恢复 JDY-32 出厂默认设置
 */
void JDY32_RestoreDefault(void);

#endif
