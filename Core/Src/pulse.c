/*
 * ecg_Rwave.c
 *
 *  Created on: 2023年10月10日
 *      Author: Johnny
 */
#include "main.h"



/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ public var define start ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv public var define end vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/



/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ private var define start ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/

// pulse status
static pulse_status_typeDef pulse_status;
static bool pulseIsWorking;

// pulse config (these values will store in EE before LPM)
static pulse_config_typeDef pulse_config;
// rocord dont pulsing time period(sotre in EE before LPM)
static pulse_unpulsing_period_typeDef pulse_unpulsingPeriod;

// pulse work time is 3 hours
static u32 pulse_workOverTick;

// ecg set startup pulsing flag
static bool pulse_ecgPulsingOn;

// APP ask pulse ON/OFF(default: OFF)
static bool pulse_blePulsingOn;

// record pulse config time index of group
static int pulse_conTimeIndex;



/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv private var define end vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/



/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ private function define start ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


/*
  brief:
    1. 
*/
static u32 pulse_getNextMinute(u32 _m)
{
  u32 ret;

  if(_m + 1 == MCU_MAX_MINUTE_EACH_DAY)
    ret = 0;
  else
    ret = _m + 1;

  return ret;
}


/*
  brief:
    1. 
*/
static u32 pulse_getLastMinute(u32 _m)
{
  u32 ret;

  if(_m == 0)
    ret = MCU_MAX_MINUTE_EACH_DAY - 1;
  else
    ret = _m - 1;

  return ret;
}

/*
  brief:
    1. We will config pulse time every day;
    2. If no config, default time is: 00:30 ~ 03:30;
    3. convert time into Minutes for comparing;
    4. 
*/
static bool pulse_isPulseTime(void)
{
#ifndef LiuJH_DEBUG
  return true;
#else
  bool ret = false;
	// current time is m1, and pulse time is m2;(unit: minute)
  u32 m1, m2;
	int i;

  // get current date and time
  HAL_RTC_GetTime(&hrtc, &mcu_time, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, &mcu_date, RTC_FORMAT_BIN);

  /*
    if it is in unpulsing period, dont pulsing
  */
  // convert year to months
  m1 = mcu_date.Year * 12 + mcu_date.Month;
  // convert months to days
  m1 = m1 * 31 + mcu_date.Date;
  // couvert days to hours and convert hours to minutes
  m1 = (m1 * 24 + mcu_time.Hours) * 60 + mcu_time.Minutes;
  // is in unpulsing period?
  if(m1 >= pulse_unpulsingPeriod.startDt
    && m1 <= pulse_unpulsingPeriod.endDt){
    return ret;
  }

  /* compare pulsing time of every day */

  // dont need send pulse?
  if(pulse_config.pulse_timeNum == 0){
		return ret;
  }

	// current time convert to Minutes
	m1 = mcu_time.Hours * 60 + mcu_time.Minutes;

	// get all start time of pulse config time and compare
	for(i = 0; i < pulse_config.pulse_timeNum; i++){
		// get time
		m2 = pulse_config.pulse_timeBuf[i].startTime;

		// compare time(unit: minutes)
		if(m1 == m2 || m1 == pulse_getLastMinute(m2) || m1 == pulse_getNextMinute(m2)){
			// record this index for calculating work time
			pulse_conTimeIndex = i;
			ret = true;
			break;
		}
	}

  return ret;
#endif
}

/*
  brief:
    1. Detecting heart rate(bpm) is less 120???
*/
static bool pulse_isInnerPeace()
{
#ifndef LiuJH_DEBUG
  // test only for pulse
  return true;
#else
  return ecg_getBpm() < mcu_getBpmCalMax()->mcu_bpmCalmMax;
#endif
}

/*
  brief:
    1. if only pulse is working, check batt;
*/
static bool pulse_isUltraBattLevel(void)
{
  bool ret = false;

  if(mcu_rstFlag == mcu_rstFlag_RtcTimer
    && wpr_isUltraLowBattLevel())
    ret = true;

  return ret;
}

/*
*/
static void pulse_enterLpm(void)
{
  // make sure ovbc is shut down
  if(ovbc_isWorking())
    ovbc_shutdown();
  // make sure wpr is shut down
  wpr_shutdown();
  
  // stop work and go into idle(we can LPM)
  pulse_status = pulse_idle_status;
}

/*
  brief:
    1. ble working time is over or pulse working time is over;
    2. 
*/
static bool pulse_workTimeIsOver(void)
{
  if(
        // ble start up this working?
       ((mcu_rstFlag == mcu_rstFlag_MagnetHall) && !ble_isWorking())
       // pulse start up this working?
    || ((mcu_rstFlag == mcu_rstFlag_RtcTimer) && (HAL_GetTick() > pulse_workOverTick))
    )
    return true;

  return false;
}

/*
  brief:
    1. creating pulsing state machine;
    2. finish pulsing, update to waiting status;
    3. waiting pulsing is OK, then return next status for next pulsing;
*/
static void pulse_smPusingProc(void)
{
  // pulsing is working
  if(ovbc_isWorking()) return;

  /* ovbc is finished pulsing work */

  // for next pulsing
  pulse_status = pulse_waiting_status;
}

/*
  brief:
    1. waiting pulsing time;
    2. ecg detect current Rs peak point, notify this proc;
    3. pulse delay = Rsvi + config.delay;
    4. we MUST check bleEnableFlag(if ble is working) at FIRST;
    5. and then check motion and bpm;
    5. if all conditions is OK, start pulsing process;
    6. pulsing: 

*/
static void pulse_smWaitingProc(void)
{
  // ble working time is over or pulse working time is over?
  // if only pulse is working, check batt
  if(pulse_workTimeIsOver() || pulse_isUltraBattLevel()){

    // enter standby mode
    pulse_enterLpm();

    // mcu will startup LPM working
    goto pulse_smWaitingProcEnd;
  }

  /* NOW: we can check pulsing switch and flag */

  if(fpulse_isWorking())
    goto pulse_smWaitingProcEnd;

  // is charging? dont pulsing
  if(wpr_isCharging())
    goto pulse_smWaitingProcEnd;

  // if ble is working, ble_pulsing switch is OFF?
  if(ble_isWorking() && !pulse_blePulsingOn)
    goto pulse_smWaitingProcEnd;

  /* NOW: we can Detecting motion status and bpm */

  if(ble_isWorking() && pulse_blePulsingOn){
    // ignore checking both of accel and bpm
  }else{
    // check motion
    if(!accel_isMotionless())
      goto pulse_smWaitingProcEnd;
    
    // check bpm
    if(!pulse_isInnerPeace())
      goto pulse_smWaitingProcEnd;
  }

  /* NOW: we will check ecg pulsing flag every R peak piont */

  // ecg notify pulsing startup?
  if(pulse_ecgPulsingOn){
    // start up pulsing
    ovbc_startup();

    // record pulse total num
    mcu_getBaseData()->mcu_pulseTotalNum++;

    // update status
    pulse_status = pulse_pulsing_status;
  }


pulse_smWaitingProcEnd:
  // OFF this switch for next trigger
  pulse_ecgPulsingOn = false;
}

/*
  brief:
    1. startup adc; startup accel;
    2. record start tick for 10 minutes and 3 hours timeout;
    3. 
*/
static void pulse_smStartupProc(void)
{
  u32 minutes;
	pulse_time_typeDef *p = pulse_config.pulse_timeBuf + pulse_conTimeIndex;

  pulse_init();
  pulseIsWorking = true;

  // start all six channel
  adc_startup();

#ifndef LiuJH_DEBUG
  minutes = 1;
#else
	// calculate working time(unit: minute)
  if(p->endTime < p->startTime)
    minutes = MCU_MAX_MINUTE_EACH_DAY + p->endTime - p->startTime;
  else
    minutes = p->endTime - p->startTime;
#endif
  // get tick(unit: ms)
  pulse_workOverTick = HAL_GetTick() + minutes * 60 * 1000;

  pulse_status = pulse_waiting_status;
}

/*
*/
static void pulse_smIdleProc(void)
{
  pulseIsWorking = false;
}

/*
  brief:
    1. check wakeup reason, if magnet hall go into idle status;
    2. if RTC timer wakeup, goto startup status;
*/
static void pulse_smInitedProc(void)
{
  // ble start working? OR it is pulsing time?
  if(ble_isWorking()
    || (mcu_rstFlag == mcu_rstFlag_RtcTimer && pulse_isPulseTime())){
      // start pulse working
      pulseIsWorking = true;
      pulse_status = pulse_startup_status;
  }else{
    // will LPM
    pulse_status = pulse_idle_status;
  }
}



/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv private function define end vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/



/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ public function define start ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/





/*
  brief:
    1. Pulse state machine process is here;
    2. It is working in main loop while;
    3. So dont delay!!!
*/
void pulse_StateMachine(void)
{
  switch(pulse_status){
    case pulse_inited_status:
      pulse_smInitedProc();
      break;
    case pulse_idle_status:
      pulse_smIdleProc();
      break;
    case pulse_startup_status:
      pulse_smStartupProc();
      break;
    case pulse_waiting_status:
      pulse_smWaitingProc();
      break;
    case pulse_pulsing_status:
      pulse_smPusingProc();
      break;


    default:
      break;
  }
}

bool pulse_blePulsingIsOn(void)
{
  return pulse_blePulsingOn;
}

/*
*/
void pulse_bleConfigPulseOn(bool _isOn)
{
  pulse_blePulsingOn = _isOn;
}

/*
  brief:
    1. ecg trigger pulse sending;
    2. set pulsing flag;
*/
void pulse_setEcgPulsingFlag(void)
{
  pulse_ecgPulsingOn = true;
}

/*
*/
bool pulse_isWorking(void)
{
  return pulseIsWorking;
}

ppulse_unpulsing_period_typeDef pulse_getUnpulsingPeriod(void)
{
  return &pulse_unpulsingPeriod;
}

/*
*/
ppulse_config_typeDef pulse_getConfig(void)
{
  return &pulse_config;
}

/*
  brief:
    1. check valid or not;
    2. if unvalid, set default value:
      pulse_unpuling_start_dt:  2024-02-08 00:00
      pulse_unpuling_end_dt:    2024-02-18 00:00
*/
void pulse_calibrateUnpulsingPeriod(void)
{
  ppulse_unpulsing_period_typeDef p = &pulse_unpulsingPeriod;

  // unvalid? set default
  if(p->isValid ^ MCU_DATA_STRUCT_VALID_VALUE){
    p->startY = UNPULSING_PERIOD_START_Y_DEFAULT;
    p->startMo = UNPULSING_PERIOD_START_MO_DEFAULT;
    p->startD = UNPULSING_PERIOD_START_D_DEFAULT;
    p->startH = UNPULSING_PERIOD_START_H_DEFAULT;
    p->startMi = UNPULSING_PERIOD_START_MI_DEFAULT;
    p->startDt = UNPULSING_PERIOD_START_DT_DEFAULT;

    p->endY = UNPULSING_PERIOD_END_Y_DEFAULT;
    p->endMo = UNPULSING_PERIOD_END_MO_DEFAULT;
    p->endD = UNPULSING_PERIOD_END_D_DEFAULT;
    p->endH = UNPULSING_PERIOD_END_H_DEFAULT;
    p->endMi = UNPULSING_PERIOD_END_MI_DEFAULT;
    p->endDt = UNPULSING_PERIOD_END_DT_DEFAULT;

    // set valid
    p->isValid = MCU_DATA_STRUCT_VALID_VALUE;

		// store this key value into EEPROM
		ee_readOrWriteKeyValue(ee_kv_unpulsingPeriod, false);
  }
}

/*
  brief:
    1. check valid or not;
    2. if unvalid, set default config value:
        pulse_Rsvi_ms:      10
        pulse_Rv_delay_ms:  20;
        pulse_num:          2
        pulse_width:        5
        pulse_start_time:   00:30
        pulse_end_time:     03:30
*/
void pulse_calibrateConfig(void)
{
  ppulse_config_typeDef p = &pulse_config;

  // unvalid?
  if(p->pulse_configIsValid ^ MCU_DATA_STRUCT_VALID_VALUE){
    memset((void *)p, 0, sizeof(ppulse_config_typeDef));

    p->pulse_Rsvi_ms = PULSE_RSVI_DELAY_MS_DEFAULT;
    p->pulse_Rv_delay_ms = PULSE_RV_DELAY_MS_DEFAULT;
    p->pulse_num = PULSE_NUM_DEFAULT;
    p->pulse_width = PULSE_WIDTH_DEFAULT;

		// set all pulse time default time
		for(int i = 0; i < PULSE_TIME_BUF_SIZE - 1; i++){
#ifdef LiuJH_DEBUG
		// Test only(test code when we have six pulse time period)
		/*
			six pulse time period group is:
					[00:00, 01:00], [04:00, 05:00], [08:00, 09:00], [12:00, 13:00], [16:00, 17:00], [20:00, 21:00]
			convert these to minutes is:
					[0    , 60   ], [240  , 300  ], [480  , 540  ], [720  , 780  ], [960  , 1020 ], [1200 , 1800 ]
		*/
//		p->pulse_timeBuf[i].startTime = i * 4 * 60;
		/*
			five pulse time period group is:
					[00:00, 01:00], [04:48, 05:48], [09:36, 10:36], [14:24, 15:24], [19:12, 20:12]
			convert these to minutes is:
					[0    , 60   ], [288  , 348  ], [576  , 636  ], [864  , 924  ], [1152  , 1212]
		*/
		p->pulse_timeBuf[i].startTime = i * 288;
		p->pulse_timeBuf[i].endTime = p->pulse_timeBuf[i].startTime + 60;
#else
			p->pulse_timeBuf[i].startTime = PULSE_START_TIME_DEFAULT;
			p->pulse_timeBuf[i].endTime = PULSE_END_TIME_DEFAULT;
#endif
		}
#ifdef LiuJH_DEBUG
		// test only ( we have six pulse time period groups)
		p->pulse_timeNum = PULSE_TIME_BUF_SIZE;
#else
		p->pulse_timeNum = PULSE_TIME_NUM_DEFAULT;
#endif

#ifndef LiuJH_DEBUG
		// test only(test code when we have one pulse time period)
		p->pulse_timeBuf[0].startTime = 0;
		p->pulse_timeBuf[0].endTime = 1 * 60;
		p->pulse_timeNum = 1;
#endif

    // set valid
    p->pulse_configIsValid = MCU_DATA_STRUCT_VALID_VALUE;

		// store this key value into EEPROM
		ee_readOrWriteKeyValue(ee_kv_pulseConfig, false);
  }
}

/*
  brief:
    1. Init all about pulse;
    2. 
*/
void pulse_init(void)
{
  // init all about pulse
  pulseIsWorking = false;
  pulse_workOverTick = 0;
  pulse_ecgPulsingOn = false;
  pulse_blePulsingOn = false;
	pulse_conTimeIndex = 0;

  ovbc_init();

  // status init
  pulse_status = pulse_inited_status;
}

/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv public function define end vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/

