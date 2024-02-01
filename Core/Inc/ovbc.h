/*
 * ovbc.h
 *
 *  Created on: Nov 23, 2023
 *      Author: Johnny
 */

#ifndef INC_OVBC_H_
#define INC_OVBC_H_

// this ChipEn start up use 15ms (maybe), stable; try it with 5ms
#define OVBC_CHIP_ENABLE_TIME       5	// 15

// Unit of pulse config->pulse_width is 0.1ms
#define OVBC_PULSE_WIDTH_UNIT       10
#define OVBC_PULSE_WIDTH_HALF_UNIT  5




typedef enum{
  /*
  */
  ovbc_idle_status,
  /*
  */
  ovbc_inited_status,
  /*
  */
  ovbc_startup_status,
  /*
  */
  ovbc_chipEnable_status,
  /*
  */
  ovbc_chipEnabling_status,
  /*
  */
  ovbc_VposEn_status,
  /*
  */
  ovbc_VnegEn_status,
  /*
  */
  ovbc_onePulseEnd_status,
  /*
  */
  ovbc_Tim6Stop_status,



  ovbc_max_status
} ovbc_status_typeDef;




extern void ovbc_init(void);
extern bool ovbc_isWorking(void);
extern void ovbc_startup(void);
extern void ovbc_shutdown(void);
extern void ovbc_stateMachine(void);
extern void ovbc_cbStateMachine(void);







#endif /* INC_OVBC_H_ */
