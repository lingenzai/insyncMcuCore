/*
 * adc.c
 *
 *  Created on: 2023年11月18日
 *      Author: Johnny
 */

#include "main.h"

/* var define *******************************************************/

// adc status
static adc_status_typeDEF adc_status;

static bool adcIsWorking;

// adc data sequence channel num
static u8 adc_curChNum;

// adc start working tick
static u32 adc_workingTick;


/* private function define ******************************************/


/*
  brief:
    1. We have seven channels: 
       CH1_2: Ra_RDET, Ra_IEGM,
       CH3_4: Rs_RDET, Rs_IEGM,
       CH5_6: Rv_RDET, Rs_IEGM,
       CH7:   Batt_Measurement,
    2. NOTICE: We start channel 2, 3, 4, 5, 6, 7, No use Ra_RDET;
*/
static void adc_configChannel(void)
{
//  ADC_ChannelConfTypeDef sConfig = {0};


//  HAL_ADCEx_Calibration_Start(&hadc, ADC_SINGLE_ENDED);

  // NOW: we can start adc sample with interrupt mode

}

/*
  brief:
    1. 
*/
static void adc_smWorking(void)
{
}

/*
*/
static void adc_smStartup(void)
{
  // init all var about adc
  adc_init();

  adcIsWorking = true;

  // config adc channel
  adc_configChannel();

  // startup ADC in interrupt mode
  HAL_ADC_Start_IT(&hadc);

  // record adc start working tick
  adc_workingTick = HAL_GetTick();

  // update status
  adc_status = adc_working_status;
}

/* public function define *******************************************/

/*
  brief:
    1. get adc value, and conversion input voltage;
    2. Current setting:
        ADC clock is        16MHz;
        sample divided is   64;
        conversion cycle is 12.5;
        sample cycle is     160.5;
        So, sample time per point is (12.5 + 160.5) * 1000 / (16 * 2^20 / 64) = 0.6599ms;
    3. If select one point per 6 points, sample cycle is 0.6599 * 6 = 4ms,
      and sample rate is 1000 / 4 = 250 point per second;
    4. every six points is : ch2, ch3, ch4, ch5, ch6, ch7 sample data;
    5. so step value = chvalue - 1;
    6. Fix peek pair value per 2.5s;
    7. we are sampling channel 2 - 7 (six channels) in CCM project;
*/
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  /* check channel num, toast ecg or ble or igore */

  // is ecg channel data?
  if(!(adc_curChNum ^ ecg_getAdcChNum())){
    ecg_adcConvCpltCB();
  }

  // is ble channel data?
  if(ble_isBleAdcCh(adc_curChNum)){
    ble_adcConvCpltCB();
  }

  // is flash store twin1 or twin2?
  if(flash_isAdcStoreCh(adc_curChNum)){
    flash_adcConvCplCB(adc_curChNum);
  }


  // is batt measurement data?
  if(!(adc_curChNum ^ wpr_getAdcChNum())){
    wpr_adcConvCpltCB();
  }

  // others channel data will ignore

  // current channel isnot the last channel?
  if(adc_curChNum ^ ADC_END_CHANNEL_NUM)
    adc_curChNum++;
  else
    // next loop
    adc_curChNum = ADC_START_CHANNEL_NUM;
}

bool adc_isWorking()
{
  return adcIsWorking;
}

/*
*/
void adc_stop()
{
  if(adcIsWorking){
    // stop adc sample(interrupt mode)
    HAL_ADC_Stop_IT(&hadc);

    adc_status = adc_idle_status;
  }
}

/*
  brief:
    1. start up all channels(ch2-ch7);
    2. 
*/
void adc_startup(void)
{
  // startup already?
  if(adcIsWorking) return;

  // start up adc
  adc_status = adc_startup_status;

  // startup ecg R detection algorithm
  ecg_startup();
  // startup wpr batt measure
  wpr_startup();
}

/*
*/
void adc_stateMachine(void)
{
  switch(adc_status){
    case adc_idle_status:
      adcIsWorking = false;
      break;
    case adc_inited_status:
      // do noting, waiting start up
      break;
    case adc_startup_status:
      // startup adc
      adc_smStartup();
      break;
    case adc_working_status:
      adc_smWorking();
      break;

    default:
      adcIsWorking = false;
      break;
  }
}


/*
// init all var about adc
*/
void adc_init(void)
{
  adc_curChNum = ADC_START_CHANNEL_NUM;
  adc_workingTick = 0;

  adcIsWorking = false;
}



