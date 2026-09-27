#ifndef __TIMER_H
#define __TIMER_H
#include "sys.h"

//////////////////////////////////////////////////////////////////////////////////	 						  
//********************************************************************************
extern volatile u32 g_sys_tick_ms;

void TIM2_Init(void);

/* 通用定时器初始化，仅支持 TIM2、TIM3、TIM4 */
void TIM_Int_Init(TIM_TypeDef* TIMx, u16 arr, u16 psc);
void TIM_CH_Init(TIM_TypeDef* TIMx, u8 ch);
#endif
