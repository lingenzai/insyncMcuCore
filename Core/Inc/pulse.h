/*
 * ecg_Rwave.h
 *
 *  Created on: 2023年10月10日
 *      Author: Johnny
 */

#ifndef INC_PULSE_H_
#define INC_PULSE_H_

#include <string.h>


// heart rate max value of inner peace
#define PULSING_BPM_CALM_MAX        125

// default interval time of Rs peak point and Rv peak point
#define PULSE_RSVI_DELAY_MS_DEFAULT 10
// default delay time of Rv peak point and pulsing
#define PULSE_RV_DELAY_MS_DEFAULT   20
// default pulse number per R peak
#define PULSE_NUM_DEFAULT           2
// half width of pulse(unit is 0.1ms, so 5ms is: 50)
#define PULSE_WIDTH_DEFAULT         50
// dont pulse default(00:00 - 00:00) convert minutes
#define PULSE_START_TIME_DEFAULT    0
#define PULSE_END_TIME_DEFAULT      0
#define PULSE_TIME_NUM_DEFAULT			0


// format: YY:MM:DD-HH:MM convert minutes: (((YY * 12 + MM) * 31) * 24 + HH) * 60
// pulse_unpuling_start_dt default value:  2024-02-08 00:00 
#define UNPULSING_PERIOD_START_Y_DEFAULT  24
#define UNPULSING_PERIOD_START_MO_DEFAULT 5
#define UNPULSING_PERIOD_START_D_DEFAULT  1
#define UNPULSING_PERIOD_START_H_DEFAULT  0
#define UNPULSING_PERIOD_START_MI_DEFAULT 0
// (((24 * 12 + 2) * 31 + 8) * 24 + 0) * 60 + 0 =  12,957,120 ( 0x00 C5 B5 C0 )
#define UNPULSING_PERIOD_START_DT_DEFAULT  (0x00C5B5C0)

// pulse_unpuling_end_dt default value:  2024-02-18 00:00 
#define UNPULSING_PERIOD_END_Y_DEFAULT    24
#define UNPULSING_PERIOD_END_MO_DEFAULT   5
#define UNPULSING_PERIOD_END_D_DEFAULT    6
#define UNPULSING_PERIOD_END_H_DEFAULT    0
#define UNPULSING_PERIOD_END_MI_DEFAULT   0
// (((24 * 12 + 2) * 31 + 18) * 24 + 0) * 60 + 0 = 12,971,520 ( 0x00 C5 EE 00 )
#define UNPULSING_PERIOD_END_DT_DEFAULT   (0x00C5EE00)

// up to six groups of pulse time(start time and end time)
#define PULSE_TIME_BUF_SIZE							6
#define PULSE_TIME_CONFIG3_INDEX					3

// record Rv-Sense switch time(unit: ms) after pulse end
// so Rsvi + RvDelay + period(- 10) = 40 + period
#define PULSE_RV_SENSE_SWITCH_PERIOD      110  // 80  // 


/*
*/
typedef struct{
	u16 startTime;
	u16 endTime;
} pulse_time_typeDef;

/*
  1. store config params from USER;
  2. NOTICE: store EE before entering LPM;
  3. Read these params when mcu wakeup;
*/
typedef struct{
  // config is valid flag
  u8 pulse_configIsValid;

  // pulse Rsvi delay(unit: ms)
  u8 pulse_Rsvi_ms;
  // pulse Rv delay(unit: ms)
  u8 pulse_Rv_delay_ms;
  // pulse numbers
  u8 pulse_num;
  // pulse width(unit: 0.1ms), so width / 10 = count of ms
  u8 pulse_width;

  // convert to Minutes(unit: minute)(total 24 bytes)
  pulse_time_typeDef pulse_timeBuf[PULSE_TIME_BUF_SIZE];
	// avaliable time num(max is 6, min is 0)
	// It is no valid if both start time and end time are 0
  u8 pulse_timeNum;
} pulse_config_typeDef, *ppulse_config_typeDef;

// rocord dont pulsing data&time period(sotre in EE before LPM)
typedef struct{
  // this struct is valid flag
  u8 isValid;

/*
  10B_DATE Data Format:
  Data：17 0B 10 0A 14 17 0C 10 0A 14
  Note：year+month+day+hour+minute  to year+month+day+hour+minute
        2023.11.16 10:20 - 2023.12.16 10:20
*/
  u8 startY, startMo, startD, startH, startMi;
  u8 endY, endMo, endD, endH, endMi;

  // format: YY:MM:DD-HH:mm convert minutes; For compare
  // (((YY * 12 + MM) * 31 + DD) * 24 + HH) * 60 + mm
  u32 startDt, endDt;
} pulse_unpulsing_period_typeDef, *ppulse_unpulsing_period_typeDef;

/*
*/
typedef enum{
  /*
    1. pulse no working;
  */
  pulse_idle_status,
  /*
  */
  pulse_inited_status,
  /*
  */
  pulse_startup_status,
  /*
    1. pulse is working;
    2. waiting 20 ms;
  */
  pulse_waiting_status,
  /*
    1. creating pulsing state machine;
    2. finish pulsing, update to waiting status;
  */
  pulse_pulsing_status,

  pulse_max_status
} pulse_status_typeDef;




extern void pulse_init(void);
extern ppulse_config_typeDef pulse_getConfig(void);
extern ppulse_unpulsing_period_typeDef pulse_getUnpulsingPeriod(void);
extern void pulse_calibrateConfig(void);
extern void pulse_calibrateUnpulsingPeriod(void);
extern void pulse_StateMachine(void);
extern u8 pulse_cbStateMachine(u8 _data);
extern bool pulse_isWorking(void);
extern void pulse_bleConfigPulseOn(bool _isOn);
extern void pulse_bleConfigPulseValues(u8 *_p);
extern void pulse_setEcgPulsingFlag(void);
extern bool pulse_blePulsingIsOn(void);


#endif /* INC_PULSE_H_ */
