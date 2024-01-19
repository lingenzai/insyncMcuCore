/*
 * fovbc.h
 *
 *  Created on: 2024年1月5日
 *      Author: Johnny
 */

#ifndef INC_FOVBC_H_
#define INC_FOVBC_H_

#define LiuJH_NOTE


/* force pulse width */
// 0.1ms(unit: 10us)
#define FOVBC_PULSE_WIDTH_MIN           10
// 1 ms(unit: 10us)
#define FOVBC_PULSE_WIDTH_MAX           100

// count of 200 Hz(5ms), 5 * OVBC_RTC_WKUP_COUNT_UNIT = 81.92 = 82
#define FOVBC_200HZ_COUNT               82

// count default(2 / 16.384 = 0.12ms)
#define FOVBC_PULSE_DEFAULT_COUNT       2
// FOVBC_200HZ_COUNT - FOVBC_PULSE_DEFAULT_COUNT * 2
#define FOVBC_PULSE_DELAY_DEFAULT_COUNT 78

// OVBC_CHIP_ENABLE_TIME * OVBC_RTC_WKUP_COUNT_UNIT = 245.76
#define OVBC_CHIP_EBABLE_COUNT          246


typedef enum{
  /*
  */
  fovbc_idle_status,
  /*
  */
  fovbc_inited_status,
  /*
  */
  fovbc_startup_status,
  /*
  */
  fovbc_chipEnable_status,
  /*
  */
  fovbc_VposEn_status,
  /*
  */
  fovbc_VnegEn_status,
  /*
  */
  fovbc_pulsingLoop_status,



  fovbc_max_status
} fovbc_status_typeDef;



extern void fovbc_init(void);
extern bool fovbc_isWorking(void);
extern void fovbc_startup(void);
extern void fovbc_shutdown(void);
extern void fovbc_stateMachine(void);
extern void fovbc_setPulseWidth(u8 _width);
extern void fovbc_TIM6_periodElapsedCB(TIM_HandleTypeDef *htim);


#endif /* INC_FOVBC_H_ */
