#ifndef __TIMER_H
#define __TIMER_H
#include "sys.h"

//////////////////////////////////////////////////////////////////////////////////	 						  
//********************************************************************************
extern volatile u32 g_sys_tick_ms;

void TIM2_Init(void);

void TIM3_Int_Init(u16 arr,u16 psc);
void TIM3_CH_Init(u8 ch);
#endif
