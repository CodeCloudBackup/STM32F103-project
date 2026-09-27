/* main.c - 四足机器狗蓝牙遥控主程序（无IMU，直接角度控制） */
#include "stm32f10x.h"
#include <string.h>
#include "ble_uart.h"
#include "ble_protocol.h"
#include "servo_control.h"
#include "oled_display.h"
#include "adc_battery.h"

/* ==================== 系统时基 ==================== */
/* 由 TIM2 1ms 中断递增，所有模块的 volatile 变量都在中断中修改 */
volatile uint32_t g_sys_tick_ms = 0;

/* TIM2 配置为 1ms 中断（72MHz 系统时钟） */
void TIM2_Init_1ms(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    /* 72MHz / (71+1) = 1MHz，ARR=999 → 1ms 中断 */
    TIM_TimeBaseStructure.TIM_Period        = 999;
    TIM_TimeBaseStructure.TIM_Prescaler     = 71;
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    /* TIM2 中断优先级设为 2，低于 USART2（蓝牙）但高于其他任务 */
    NVIC_InitStructure.NVIC_IRQChannel                   = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(TIM2, ENABLE);
}

/* TIM2 中断服务函数 */
void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        g_sys_tick_ms++;
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    }
}

/* ==================== 系统总初始化 ==================== */
void System_Init(void)
{
    /* 系统时钟已在 startup 中由 SystemInit() 配置为 72MHz */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);  /* 2位抢占+2位子优先级 */
}

/* ==================== 蓝牙断开检测（可选） ==================== */
/* 如果蓝牙模块有 STATE/CONN 引脚，可接 GPIO 检测连接状态 */
#define BLE_STATE_GPIO      GPIOA
#define BLE_STATE_PIN       GPIO_Pin_0
#define BLE_STATE_RCC       RCC_APB2Periph_GPIOA

static uint8_t BLE_IsConnected(void)
{
    /* 高电平表示已连接，根据实际硬件调整 */
    return GPIO_ReadInputDataBit(BLE_STATE_GPIO, BLE_STATE_PIN) == Bit_SET;
}

static void BLE_State_Pin_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(BLE_STATE_RCC, ENABLE);
    GPIO_InitStructure.GPIO_Pin  = BLE_STATE_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;  /* 下拉输入 */
    GPIO_Init(BLE_STATE_GPIO, &GPIO_InitStructure);
}

/* ==================== 安全保护：蓝牙断开时舵机归零 ==================== */
static uint32_t g_ble_disconnect_tick = 0;  /* 蓝牙断开计时 */
#define BLE_DISCONNECT_TIMEOUT_MS  1000     /* 断开超过1秒则急停 */

static void Safety_Check(void)
{
    if (!BLE_IsConnected())
    {
        if (g_sys_tick_ms - g_ble_disconnect_tick >= BLE_DISCONNECT_TIMEOUT_MS)
        {
            /* 蓝牙断开超时，所有舵机归零 */
            uint16_t zero_angles[SERVO_COUNT] = {0};
            Servo_SetAllAngles(zero_angles);
            g_ble_target.motor_enable = 0;
        }
    }
    else
    {
        g_ble_disconnect_tick = g_sys_tick_ms;  /* 连接中，刷新计时 */
    }
}

/* ==================== 主函数 ==================== */
int main(void)
{
    System_Init();
    TIM2_Init_1ms();
    BLE_State_Pin_Init();       /* 蓝牙连接状态引脚 */
    BLE_UART_Init(115200);      /* 蓝牙 UART（USART2） */
    BLE_Protocol_Init();        /* 蓝牙协议状态机 */
    Servo_Init();               /* 舵机 PWM（TIM4 CH1~CH4） */
    OLED_Init();                /* OLED 显示 */
    ADC_Battery_Init();         /* 电池电压 ADC */

    /* 所有舵机初始化到中位（90°），防止上电乱动 */
    uint16_t mid_angles[SERVO_COUNT] = {90, 90, 90, 90};
    Servo_SetAllAngles(mid_angles);

    /* ===== 各任务时间戳 ===== */
    uint32_t last_ble_tick     = 0;
    uint32_t last_oled_tick    = 0;
    uint32_t last_battery_tick = 0;
    uint32_t last_report_tick  = 0;
    uint32_t current_tick;
    ParsedFrame frame;

    while (1)
    {
        current_tick = g_sys_tick_ms;

        /* ========== 任务1：蓝牙协议轮询（2ms，最高优先级） ========== */
        if (current_tick - last_ble_tick >= 2)
        {
            last_ble_tick = current_tick;
            BLE_Protocol_Poll();  /* 从 UART 环形缓冲区取字节，喂状态机 */

            /* 处理所有解析完成的帧 */
            while (BLE_Protocol_GetFrame(&frame))
            {
                switch (frame.cmd)
                {
                case CMD_SET_SERVO:
                    /* 单舵机设置：data[0]=舵机ID(1~4), data[1]=角度(0~180) */
                    if (frame.data_len >= 2 &&
                        frame.data[0] >= 1 && frame.data[0] <= SERVO_COUNT)
                    {
                        Servo_SetAngle(frame.data[0], frame.data[1]);
                    }
                    break;

                case CMD_SET_ALL_SERVO:
                    /* 批量设置4个舵机：data[0~3] = 4个角度值 */
                    if (frame.data_len >= 4)
                    {
                        uint16_t angles[SERVO_COUNT] = {
                            frame.data[0], frame.data[1],
                            frame.data[2], frame.data[3]
                        };
                        Servo_SetAllAngles(angles);
                    }
                    break;

                case CMD_ACTION_MODE:
                    /* 动作模式切换：data[0]=模式ID
                     * 0=站立, 1=小跑, 2=趴下, 3=招手...
                     * 具体步态角度数组在 gait_generator 模块中定义
                     */
                    g_ble_target.action_mode = frame.data[0];
                    break;

                case CMD_MOTOR_ENABLE:
                    /* 电机使能/失能：data[0]=1 使能, 0 失能 */
                    g_ble_target.motor_enable = frame.data[0];
                    if (!g_ble_target.motor_enable)
                    {
                        /* 失能时舵机归零 */
                        uint16_t zero_angles[SERVO_COUNT] = {0};
                        Servo_SetAllAngles(zero_angles);
                    }
                    break;

                case CMD_HEARTBEAT:
                    /* 心跳回复 */
                    BLE_Protocol_SendFrame(CMD_HEARTBEAT, NULL, 0);
                    break;

                default:
                    break;
                }
            }
        }

        /* ========== 任务2：安全保护检查（每10ms） ========== */
        if (current_tick - last_ble_tick >= 10)  /* 复用 ble_tick，10ms 检查一次 */
        {
            Safety_Check();
        }

        /* ========== 任务3：OLED 刷新（100ms） ========== */
        if (current_tick - last_oled_tick >= 100)
        {
            last_oled_tick = current_tick;
            OLED_Refresh_Display(
                g_ble_target.action_mode,
                g_ble_target.motor_enable,
                ADC_Battery_GetPercentage()
            );
        }

        /* ========== 任务4：电池 ADC 采样（500ms） ========== */
        if (current_tick - last_battery_tick >= 500)
        {
            last_battery_tick = current_tick;
            uint16_t voltage_mv = ADC_Battery_ReadVoltage_mV();

            /* 低电量告警（通过蓝牙上报） */
            if (voltage_mv <= BATTERY_LOW_MV)
            {
                uint8_t alert_data[2] = {
                    (uint8_t)(voltage_mv >> 8),
                    (uint8_t)(voltage_mv & 0xFF)
                };
                BLE_Protocol_SendFrame(CMD_LOW_BATTERY, alert_data, 2);
            }
        }

        /* ========== 任务5：周期性状态上报（2s） ========== */
        if (current_tick - last_report_tick >= 2000)
        {
            last_report_tick = current_tick;
            uint8_t status_data[4] = {
                ADC_Battery_GetPercentage(),
                g_ble_target.action_mode,
                g_ble_target.motor_enable,
                BLE_IsConnected() ? 1 : 0
            };
            BLE_Protocol_SendFrame(CMD_STATUS_REPORT, status_data, 4);
        }
    }
}