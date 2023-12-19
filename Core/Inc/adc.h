/*
 * adc.h
 *
 *  Created on: 2023年11月18日
 *      Author: Johnny
 */

#ifndef INC_ADC_H_
#define INC_ADC_H_

/* macro define *****************************************************/

// adc channel number
#define ADC_CH_NUM_RA_RDET          1
#define ADC_CH_NUM_RA_IEGM          2
#define ADC_CH_NUM_RS_RDET          3
#define ADC_CH_NUM_RS_IEGM          4
#define ADC_CH_NUM_RV_RDET          5
#define ADC_CH_NUM_RV_IEGM          6
#define ADC_CH_NUM_BATT_MEASURE     7

// sample one point per 4ms, then 15000 points per minute.
#define ADC_SAMPLE_STEP             6
/* for CCM (six channel) */
// adc channel start num
#define ADC_START_CHANNEL_NUM       ADC_CH_NUM_RA_IEGM
// adc channel end num
#define ADC_END_CHANNEL_NUM         ADC_CH_NUM_BATT_MEASURE // ADC_START_CHANNEL_NUM + ADC_SAMPLE_STEP - 1: 7

// peak pair value
#define ADC_MAX_VALUE               4095
#define ADC_MIN_VALUE               0

// adc channel define
#define ADC_CH1_RA_RDET         ADC_CHANNEL_1
#define ADC_CH2_RA_IEGM         ADC_CHANNEL_2
#define ADC_CH3_Septum_RDET     ADC_CHANNEL_3
#define ADC_CH4_Septum_IEGM     ADC_CHANNEL_4
#define ADC_CH5_RV_RDET         ADC_CHANNEL_5
#define ADC_CH6_RV_IEGM         ADC_CHANNEL_6
#define ADC_CH7_V_MEASURE       ADC_CHANNEL_7

// adc stable tick
// ignore time(unit: ms) the first some points
#define ADC_WORKING_STABLE_TIME TIMEOUT_800MS

#define ADC_TRIM_PEAK_TIMEOUT   TIMEOUT_2P5S



/* typedef **********************************************************/

typedef enum{
  /*
  */
  adc_idle_status,
  /*
  */
  adc_inited_status,
  /*
  */
  adc_startup_status,
  /*
  */
  adc_working_status,

  adc_max_status
} adc_status_typeDEF;



/* extern public function *******************************************/

extern bool adc_isWorking(void);
extern void adc_stop(void);
extern void adc_startup(void);
extern void adc_stateMachine(void);
extern void adc_init(void);




#endif /* INC_ADC_H_ */
