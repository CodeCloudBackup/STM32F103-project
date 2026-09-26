#ifndef __SERVO_H
#define __SERVO_H

#include "sys.h"
#include "timer.h"

// PB0、
// PB1
// PA6
// PA7
void SG90_Servo_Init(void);
void SG90_Set_Angle(u8 angle, const u8 ch);

#endif

