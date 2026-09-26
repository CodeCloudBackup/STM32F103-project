#include "bluetooth_jdy32.h"
#include "ble_uart.h"
#include "timer.h"

/* ==================== 私有变量 ==================== */
static uint8_t  at_rx_buf[128];           /* AT指令接收缓冲区 */
static uint8_t  at_rx_idx  = 0;           /* 接收索引 */

// PA1 STAT
// PA4 PWRX
void JDY32_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

  // 1. 开启 GPIOA 时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

	// 3. 配置 PA1 为 下拉输入
	GPIO_InitStructure.GPIO_Pin = JDY32_STAT_PIN;//设置PA1引脚 stat引脚
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD ; /* 下拉输入 */
	GPIO_Init(JDY32_STAT_PORT,&GPIO_InitStructure);//初始化
	
	// 3. 配置 PA4 为推挽输出
	GPIO_InitStructure.GPIO_Pin = JDY32_PWRC_PIN;//设置PA4引脚 -- //ESP8266使能引脚
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP ;//设置输出模式为推挽输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz ;//设置输出速度为50MHZ
	GPIO_Init(JDY32_PWRC_PORT,&GPIO_InitStructure);//初始化

	// 默认唤醒状态（高电平，根据具体模块手册调整）
  GPIO_SetBits(JDY32_PWRC_PORT, JDY32_PWRC_PIN);
	
}

void JDY32_Sleep(void) {
	GPIO_ResetBits(JDY32_PWRC_PORT, JDY32_PWRC_PIN); // 拉低进入睡眠
	printf("JDY32 Enter Sleep Mode\r\n");
}

void JDY32_WakeUp(void) {
	GPIO_SetBits(JDY32_PWRC_PORT, JDY32_PWRC_PIN);   // 拉高唤醒
	printf("JDY32 Wake Up\r\n");
}

u8 JDY32_IsConnected(void)
{
	/* 高电平表示已连接，根据实际硬件调整 */
	return GPIO_ReadInputDataBit(JDY32_STAT_PORT, JDY32_STAT_PIN) == Bit_SET;
}

void JDY32_SendString(const char *str)
{
	 while (*str) {
       BLE_UART2_SendByte((uint8_t)*str++);
    }

}

/**
 * @brief 发送 AT 指令（不等待响应，用于设置类指令）
 * @param cmd  AT 指令字符串
 */
void JDY32_SendAT_NoReply(const char *cmd)
{
		if (cmd == NULL) return;
    JDY32_SendString(cmd);
    JDY32_SendString("\r\n");
}


static uint8_t JDY32_GetByte(uint8_t *data, uint32_t timeout_ms)
{
    uint32_t start = g_sys_tick_ms;  /* 假设你已有全局系统滴答计时 */
    while (g_sys_tick_ms - start < timeout_ms) {
        if (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET) {
            *data = (uint8_t)USART_ReceiveData(USART2);
            return 1;
        }
    }
    return 0;  /* 超时 */
}

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
uint8_t JDY32_SendAT(const char *cmd, char *resp, uint16_t resp_len, uint32_t timeout_ms)
{
    uint8_t byte;
    uint32_t start;

    if (!cmd || !resp || resp_len == 0) return 0;

    /* 清空接收缓冲区 */
    memset(at_rx_buf, 0, sizeof(at_rx_buf));
    at_rx_idx = 0;

    /* 发送 AT 指令 + \r\n */
    JDY32_SendString(cmd);
    JDY32_SendString("\r\n");

    /* 等待响应，以 \r\n 为结束标志，或超时退出 */
    start = g_sys_tick_ms;
    while (g_sys_tick_ms - start < timeout_ms)
    {
        if (JDY32_GetByte(&byte, 10))  /* 每次最多等 10ms */
        {
            if (at_rx_idx < resp_len - 1) {
                at_rx_buf[at_rx_idx++] = byte;
            }

            /* 检测 \r\n 结尾，表示响应结束 */
            if (at_rx_idx >= 2 &&
                at_rx_buf[at_rx_idx - 2] == '\r' &&
                at_rx_buf[at_rx_idx - 1] == '\n')
            {
                at_rx_buf[at_rx_idx] = '\0';
                strncpy(resp, (char *)at_rx_buf, resp_len - 1);
                resp[resp_len - 1] = '\0';
                return 1;
            }
        }
    }

    /* 超时，返回已收到的内容（可能不完整） */
    at_rx_buf[at_rx_idx] = '\0';
    strncpy(resp, (char *)at_rx_buf, resp_len - 1);
    resp[resp_len - 1] = '\0';
    return 0;
}

/**
 * @brief  查询 JDY-32 版本号
 */
u8 JDY32_GetVersion(char *buf, u16 len)
{
	return JDY32_SendAT("AT+VERSION", buf, len, 500);
}

/**
 * @brief  查询 BLE MAC 地址
 */
u8 JDY32_GetBleMac(char *buf, u16 len)
{
	return JDY32_SendAT("AT+MAC", buf, len, 500);
}

/**
 * @brief  查询 SPP MAC 地址
 */
u8 JDY32_GetSppMac(char *buf, u16 len)
{
	return JDY32_SendAT("AT+MACS", buf, len, 500);
}


/**
 * @brief 设置波特率（参数为索引值）
 * @param baud_idx  1=1200, 2=2400, 3=4800, 4=9600, 5=19200, 6=38400, 7=57600, 8=115200
 */
void JDY32_SetBaud(uint8_t baud_idx)
{
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "AT+BAUD=%d", baud_idx);
    JDY32_SendAT_NoReply(cmd);
}

/**
 * @brief 设置蓝牙广播名称（BLE）
 */
void JDY32_SetBLEName(const char *name)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+NAME=%s", name);
    JDY32_SendAT_NoReply(cmd);
}
/**
 * @brief 设置 SPP 广播名称
 */
void JDY32_SetSPPName(const char *name)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+NAMES=%s", name);
    JDY32_SendAT_NoReply(cmd);
}

/**
 * @brief 设置 SPP 连接密码
 */
void JDY32_SetPIN(const char *pin)
{
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "AT+PIN=%s", pin);
    JDY32_SendAT_NoReply(cmd);
}

/**
 * @brief  设置 SPP 密码连接类型
 * @param  type 常见值：
 *         0：无需密码
 *         1：认证配对
 *         2：认证配对 + 密码
 * @note   具体取值以 JDY-32 实际手册为准。
 */
void JDY32_SetSppType(u8 type)
{
	char cmd[16];
	snprintf(cmd, sizeof(cmd), "AT+TYPE=%d", type);
	JDY32_SendAT_NoReply(cmd);
}

/**
 * @brief  断开当前连接
 */
void JDY32_Disconnect(void)
{
	JDY32_SendAT_NoReply("AT+DISC");
}

/**
 * @brief  JDY-32 软件复位
 * @note   复位后模块会重启，部分参数修改需要复位后才生效。
 */
void JDY32_SoftReset(void)
{
	JDY32_SendAT_NoReply("AT+RST");
}

/**
 * @brief  恢复 JDY-32 出厂默认设置
 */
void JDY32_RestoreDefault(void)
{
	JDY32_SendAT_NoReply("AT+DEFAULT");
}
