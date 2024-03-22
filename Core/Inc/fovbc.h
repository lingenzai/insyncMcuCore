/*
 * fovbc.h
 *
 *  Created on: 2024年1月5日
 *      Author: Johnny
 */

#ifndef INC_FOVBC_H_
#define INC_FOVBC_H_


// fpulse: high pulse width + low pulse width + delay time = 300ms; 200 bpm
// fpulse: 0.1ms + 0.1ms + 299.8ms = 300ms(200bpm)
#define FOVBC_PULSETIME       300
// tim6: (c+1)(p+1)=2097*time(ms)

#define FOVBC_PULSEWIDTH_1MS      1
// when time is 1ms, (c+1)(p+1) = 2097 = 9 * 233, So: p = 8, c = 232
#define FOVBC_TIM6_UP_PRESCA_1MS  8

#define FOVBC_PULSEWIDTH_2MS      2
// when time is 2ms, (c+1)(p+1) = 2097 * 2 = 18 * 233, So: p = 17, c = 232
#define FOVBC_TIM6_UP_PRESCA_2MS  17

#define FOVBC_PULSEWIDTH_5MS      5
// when time is 5ms, (c+1)(p+1) = 2097 * 5 = 45 * 233, So: p = 44, c = 232
#define FOVBC_TIM6_UP_PRESCA_5MS  44

// try in usint 1ms pulse width
#define FOVBC_PULSEWIDTH          FOVBC_PULSEWIDTH_2MS
#define FOVBC_TIM6_UP_PRESCA      FOVBC_TIM6_UP_PRESCA_2MS
#define FOVBC_TIM6_UP_PERIOD      232





typedef enum{
  /*
  */
  fovbc_idle_status,
  /*
  */
  fovbc_inited_status,
  /*
  */
//  fovbc_startup_status,
  /*
  */
  fovbc_chipEnabling_status,
  /*
  */
  fovbc_VposEn_status,
  /*
  */
  fovbc_VnegEn_status,
  /*
  */
  fovbc_onePulseEnd_status,
  /*
  */
  fovbc_Tim6Stop_status,
  /*
  */
  fovbc_pulseDelay_status,



  fovbc_max_status
} fovbc_status_typeDef;



extern void fovbc_init(void);
extern bool fovbc_isWorking(void);
extern void fovbc_startup(void);
extern void fovbc_shutdown(void);
extern void fovbc_stateMachine(void);
extern void fovbc_setPulseWidth(u8 _width);
extern void fovbc_TIM6_periodElapsedCB(TIM_HandleTypeDef *htim);
extern void fovbc_cbStateMachine(void);


#endif /* INC_FOVBC_H_ */
