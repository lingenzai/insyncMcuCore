/*
 * RRdetect.h
 *
 *  Created on: 2024年3月19日
 *      Author: Johnny
 */

#ifndef CORE_RRDETECT_H_
#define CORE_RRDETECT_H_


/* macro define ***************************************************/

#ifndef LiuJH_DEBUG
// test of using Rs-RDET and Rv-RDET data of Animal Test 1
#define LiuJH_ECG
#endif


/*
  1. Sample time per point is (12.5 + 160.5) * 1000 / (16 * 2^20 / 64),
      It is 173 * 125 / 32768 = 0.6599 ms;
  2. If step is 6(ADC_SAMPLE_STEP), the time per point is 0.6599 * 6 = 4 ms;
  3. if bpm is 30(this is imposible min value), 60 * 1000 / 30 / 4 = 500 bytes;
  3. so we can get one R value in 512 bytes;
  4. If use not loop buffer, we must have two R(R1 and R2) peak point in buffer,
    so, 500 + 500 = 1000;
*/
#define ECG_BUF_R1_NEED_NUM         500
#define ECG_BUF_R2_NEED_NUM         ECG_BUF_R1_NEED_NUM
// for quickly, we get the result
#define ECG_BUF_SIZE                1000  // ECG_BUF_R1_NEED_NUM + ECG_BUF_R2_NEED_NUM // 1000

#define ECG_MAX_BYTE_VALUE          (0xFF)
#define ECG_MIN_BYTE_VALUE          0

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
// skip point num of 180 ms(180/4=45)
#define ECG_R2_SKIP_POINT_NUM      (TIMEOUT_180MS * 3 / 2 / ADC_SAMPLE_STEP)
// R2 detect window point size
#define ECG_R2_MW_SIZE              6

// define slope default value
#define ECG_SLOPEV_DEFAULT          15
// slope step(get ONE slope per four points)
#define ECG_SLOPE_STEP              3
// we will get four slopes left R1 and right R1
#define ECG_SLOPE_NUM               4
// use 0.80 as slope weight
#define ECG_SLOPE_WEIGHT            4 / 5

// use 0.7 as R1-->R2 value weight
#define ECG_R_VALUE_WEIGHT1         7 / 10
// use 0.9 as Rn value weight
#define ECG_R_VALUE_WEIGHT          9 / 10

// Rn half of move window size
#define ECG_Rn_MW_HALF_SIZE         6 // 5 // 3 // 2  // 4 //
// Rn detect data num
#define ECG_RN_DETECT_NUM           3
// 4 + 1 + 4, guess Rn peak point is in middle of these points
//#define ECG_Rn_MW_SIZE              (ECG_Rn_MW_HALF_SIZE * 2 + 1) // 9
// if Rw escape, Rw size will update
#define ECG_Rn_MW_Weight            2

// continue get Rn numbers we can pulsing
#define ECG_RN_DETECTED_MIN_NUM     3 // 4  //

// R peak point escaped max number
#define ECG_ESCAPED_MAX_NUM         1 // 0 // 2 //

#define ECG_ADC_CH_NUM              2
#define ECG_RS_INDEX                0
#define ECG_RV_INDEX                1
// weight of bpm change value
#define ECG_BPM_WEIGHT              10

#define ECG_BPM_MIN                 65  // 50 //

// Avoid having two maximum values at the same time
#define ECG_ADC_MAX_VALUE_GAIN      50
// ADC value weight of no signal
#define ECG_ADC_NO_SIGNAL_WEIGHT    50


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
  ecg_inited_status = 0,
  /*
    1. do noting;
    2. no working;
  */
  ecg_idle_status,          // 1
  /*
    1. startup adc;
    2. go into next status;
  */
  ecg_startup_status,       // 2
  /*
    1. only store u16 data into buf, and sample peak point;
    2. dont convert to u8 data;
  */
  ecg_stableWaiting_status, // 3
  /*
    1. only synchronous Loop stateMachine and ADC callback;
    2. update all vars of buf in Loop stateMachine, and AdcCB do nothing;
  */
  ecg_sync1_status,         // 4
  /*
    1. adc sample start, and record adc data;
    2. until 2.5 s, make sure include one R peak point;
    3. so go into R1detect status;
  */
  ecg_R1waiting_status,     // 5
  /*
    1. start flag: magnet wakeup mcu or adc sample start;
    2. job brief:
      detect the first R peak point;
      calculate V(average value of the slope);
    3. stop flag: get R1 and V;
  */
  ecg_R1Detect_status,      // 6
  /*
    1. start flag: get R1 and V;
    2. job brief:
      detect the second R peak point;
      calculate RRi value;
    3. stop flag: get R2 and RRi value;
  */
  ecg_R2Detect_status,      // 7
  /*
    1. start flag: get R2 peak point;
    2. job brief:
      update all vars about buf in adc callback;
  */
  ecg_Rnwaiting_status,     // 8
  /*
    1. RRi and slopeV got it;
    2. waiting MCU working;
    3. if MCU start working, goto next status;
  */
  ecg_Rnwaiting2_status,    // 9
  /*
    1. start flag: get R2 and RRi value;
    2. job brief:
      Repeatedly catching each R peak point;
      ...
    3. stop flag: ADC sample stop;
  */
  ecg_RnDetect_status,      // 10

  /*
    1.
  */
  ecg_RnDetected_status,    // 11


  pulse_Max_status
} ecg_Status_typeDef;

/*
*/
typedef struct{
  u8 isValid;


  // ADC value weight of no signal
  u8 ecg_AdcNoSignalWeight;
  // Avoid having two maximum values at the same time
  u8 ecg_AdcMaxValueGain;
} ecg_AdcWeightGain_typeDef;



/* extern variable *************************************************/




/* extern function ************************************************/

extern void ecg_init(void);
extern void ecg_stateMachine(void);
extern void ecg_startup(void);
extern void ecg_adcConvCpltCB(u8 _curCh);
extern void ecg_calibrateAdcWeightGain(void);
extern ecg_AdcWeightGain_typeDef* ecg_getAdcWeightGain(void);
extern u8 ecg_getStatus(void);
extern void ecg_getAdcPeakValue(u16 *_pmax, u16 *_pmin);
extern u8 ecg_getBpm(void);
extern u32 ecg_getRnTick(void);
extern bool ecg_getRsviAbout(u8 *_pdata);
extern u8 ecg_getRsvi(void);


/* called function ************************************************/
// function used in lib

/*
  NOTE:
    uint32_t HAL_GetTick(void);
*/
uint32 HAL_GetTick(void);
/*
  NOTE:
    void pulse_setEcgPulsingFlag(void);
*/
void mcu_setEcgPulsingFlag(void);
/*
  NOTE:
    bool adc_isWorking(void);
*/
bool mcu_adcIsWorking(void);
/*
  NOTE:
    uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *hadc);
*/
uint32 mcu_getAdcValue(void);
/*
  NOTE:
    bool ee_readOrWriteKeyValue(ee_keyvalue_typeDef _key, bool _isRead);
*/
bool mcu_eeReadOrWriteKeyValue(u8 _key, bool _isRead);



#endif /* CORE_RRDETECT_H_ */
