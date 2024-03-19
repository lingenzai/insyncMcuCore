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

// code running period time weight(assume 1ms)
#define OVBC_CODE_RUNNING_WEIGHT		1

#ifndef LiuJH_DEBUG
// only test OVBC
#define LiuJH_OVBC
#endif

#ifndef LiuJH_DEBUG
// only test: 5ms pos + 2ms neg pulsing
#define LiuJH_OVBC2
#endif

typedef enum{
  /*
  */
  ovbc_idle_status,
  /*
  */
  ovbc_inited_status,
  /*
  	1. According Rn tick and current tick and pulse config;
  	2. calculate three tim6 prescaler value for chipEnable waiting, chipEnabling and pulse width;
  	3. startup tim6 and waiting chipEnable or startup chipEnabling;
  */
  ovbc_startup_status,
  /*
		1. NOTE: tim6CB will be called immediately when start_it was called;
		2. So the first tim6CB is noUsed;
		3. just update status for next called;
  */
  ovbc_willChipEnable_status,
  /*
  	1. set VPon pin;
  	2. reinit tim6 prescaler for chipenabling;
  	3. startup tim6;
  */
  ovbc_chipEnable_status,
  /*
  	1. as same with willChipEnable status, this status is no use;
  	2. update to next status only;
  */
  ovbc_chipEnabling_status,
  /*
  	1. reset VnegEn pin and set VposEn pin;
  	2. reinit tim6 prescaler for pulse;
  	3. restartup tim6;
  	4. update next status;
  */
  ovbc_VposEn_status,
  /*
  	1. as same with chipEnabling status and willChipEnable status;
  	2. this status is no use;
  	3. update to next status only;
  */
  ovbc_VPosEnabling_status,
  /*
  	1. reset VposEn pin and set VnegEn pin;
  	2. ONE pulse is finished;
  	3. upate to next status;
  */
  ovbc_VnegEn_status,
#ifdef LiuJH_OVBC2
  /*
    1. as same with chipEnabling status and willChipEnable status;
    2. this status is no use;
    3. update to next status only;
  */
  ovbc_VnegEnabling_status,
#endif
  /*
  	1. according pulse num of pulse config;
  	2. if we will continue pulsing, reset VnegEn pin and set VposEn pin,
  		and then go into VnegEn status for next pulse;
  	3. if work is over, shutdown;
  */
  ovbc_onePulseEnd_status,


  ovbc_max_status
} ovbc_status_typeDef;




extern void ovbc_init(void);
extern bool ovbc_isWorking(void);
extern void ovbc_startup(void);
extern void ovbc_shutdown(void);
extern void ovbc_cbStateMachine(void);







#endif /* INC_OVBC_H_ */
