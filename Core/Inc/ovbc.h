/*
 * ovbc.h
 *
 *  Created on: Nov 23, 2023
 *      Author: Johnny
 */

#ifndef INC_OVBC_H_
#define INC_OVBC_H_

// this ChipEn start up use 15ms (maybe), stable
#define OVBC_CHIP_ENABLE_TIME       15

// RTC Timer LSI RC(37KHz) DIV2 unit count
//#define OVBC_RTC_WKUP_COUNT_UNIT    37 / 2
// RTC Timer LSE ORC(32.768 KHz) DIV2 counts per 1 ms
#define OVBC_RTC_WKUP_COUNT_UNIT    16384 / 1000  // 32768 / 2000

// Unit of pulse config->pulse_width is 0.1ms
#define OVBC_PULSE_WIDTH_UNIT       10



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
  ovbc_VposEn_status,
  /*
  */
  ovbc_VnegEn_status,
  /*
  */
  ovbc_pulsingLoop_status,



  ovbc_max_status
} ovbc_status_typeDef;




extern void ovbc_init(void);
extern bool ovbc_isWorking(void);
extern void ovbc_startup(void);
extern void ovbc_shutdown(void);









#endif /* INC_OVBC_H_ */
