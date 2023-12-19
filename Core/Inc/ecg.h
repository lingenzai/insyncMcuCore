/*
 * ecg.h
 *
 *  Created on: 2023年11月18日
 *      Author: Johnny
 */

#ifndef INC_ECG_H_
#define INC_ECG_H_

/* macro define ***************************************************/

/*
  1. Sample time per point is (12.5 + 160.5) * 1000 / (16 * 2^20 / 64),
      It is 173 * 125 / 32768 = 0.6599 ms;
  2. If step is 6(ADC_SAMPLE_STEP), the time per point is 0.6599 * 6 = 4 ms;
  3. If sample time is 2.5s, the total point is: 2.5 * 1000 / 4 = 625;
  4. If use not loop buffer, we must have two R peak point in buffer,
    so, 625 + 625 = 1250;
*/
#define ECG_RPEAK_BUF_NEED_NUM      625
#define ECG_BUF_ATTACH_NUM          625
#define ECG_RPEAK_BUF_SIZE          ECG_RPEAK_BUF_NEED_NUM + ECG_BUF_ATTACH_NUM // 1250

#define ECG_MAX_INIT_VALUE          0
#define ECG_MIN_INIT_VALUE          (0xFF)

// R wave slope coefficient
// NOTE: dont add ()
#ifdef LiuJH_DEBUG
// RDET detect is OK
#define ECG_R1_SLOPEV_MUL           12/25 // is 0.48; // 23/50 // is 0.46; found 79 and 37 slope  // 
#define ECG_R1_SLOPEV_MUL1          12
#define ECG_R1_SLOPEV_MUL2          25
#else
// IEGM detect is OK
#define ECG_R1_SLOPEV_MUL           7/12  // is 0.58
#define ECG_R1_SLOPEV_MUL1          7
#define ECG_R1_SLOPEV_MUL2          
#endif

// after R1 point, skip some byte for detecting R2
// DONT add ()
#define ECG_RR_TIME_PER_POINT       396 / 100   // 4ms
// skip point num of 180 ms(180/4=45)
#define ECG_R2_SKIP_POINT_NUM      (TIMEOUT_180MS * 3 / 2 / ADC_SAMPLE_STEP)
// R2 detect window point size
#define ECG_R2_MW_SIZE              4
// In R2 detected state, step of each detect
#ifdef LiuJH_DEBUG
// RDET is OK
#define ECG_R2_DETECT_STEP          1
#else
// IEGM is OK
#define ECG_R2_DETECT_STEP          2
#endif
// Rn move window size
#define ECG_Rn_MW_HALF_SIZE         4 // 2
// 4 + 1 + 4, guess Rn peak point is in middle of these points
//#define ECG_Rn_MW_SIZE              (ECG_Rn_MW_HALF_SIZE * 2 + 1) // 9
// if Rw escape, Rw size will update
#define ECG_Rn_MW_Weight            2

// continue get Rn numbers we can pulsing
#define ECG_RN_DETECTED_MIN_NUM     5






/* type define ****************************************************/

/*
  1. A set of state machines that are relatively independent of the MCU state machine;
  2. It is the processing of ECG wave at different stages;
  3. 
*/
typedef enum{
  /*
    1. init all vars about pulse_Rwave;
    2. check wakeup reason, if magnet hall go into idle status;
    3. if RTC timer wakeup, goto startup status;
  */
  ecg_inited_status,
  /*
    1. do noting;
    2. no working;
  */
  ecg_idle_status,
  /*
    1. startup adc;
    2. go into next status;
  */
  ecg_startup_status,
  /*
    1. init all vars except vars about Rv;
    2. others work is same of startup status;
  */
  ecg_startup2_status,
  /*
    1. adc sample start, and record adc data;
    2. until 2.5 s, make sure include one R peak point;
    3. so go into R1detect status;
  */
  ecg_R1waiting_status,
  /*
    1. start flag: magnet wakeup mcu or adc sample start;
    2. job brief: 
      detect the first R peak point;
      calculate V(average value of the slope);
    3. stop flag: get R1 and V;
  */
  ecg_R1Detect_status,
  /*
    1. start flag: get R1 and V;
    2. job brief:
      detect the second R peak point;
      calculate RRi value;
    3. stop flag: get R2 and RRi value;
  */
  ecg_R2Detect_status,
  /*
    1. start flag: get R2 peak point;
    2. job brief:
      update all vars about buf in adc callback;
  */
  ecg_Rnwaiting_status,
  /*
    1. RRi and slopeV got it;
    2. waiting MCU working;
    3. if MCU start working, goto next status;
  */
  ecg_Rnwaiting2_status,
  /*
    1. start flag: get R2 and RRi value;
    2. job brief:
      Repeatedly catching each R peak point;
      ...
    3. stop flag: ADC sample stop;
  */
  ecg_RnDetect_status,

  /*
  */
  ecg_RwDetect_status,


  pulse_Max_status
} ecg_Status_typeDef;



/* extern variable *************************************************/




/* extern function ************************************************/

extern void ecg_init(void);
extern void ecg_stateMachine(void);
extern void ecg_startup(void);
extern void ecg_adcConvCpltCB(void);
extern u8 ecg_getAdcChNum(void);
extern u8 ecg_getBpm(void);
extern u32 ecg_getRnTick(void);
extern bool ecg_getRsviAbout(u8 *_pdata);
extern u8 ecg_getRsvi(void);

#endif /* INC_ECG_H_ */
