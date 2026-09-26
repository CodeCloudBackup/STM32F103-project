#ifndef __BLE_UART_H
#define __BLE_UART_H

#include "stm32f10x.h"

void BLE_UART_Init(uint32_t baudrate);
void BLE_UART2_SendByte(u8 data);

#endif

