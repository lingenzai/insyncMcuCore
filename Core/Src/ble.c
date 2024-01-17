/*
 * ble_rsl10.c
 *
 *  Created on: 2023年11月9日
 *      Author: Johnny
 */

#include "main.h"



/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ private var define start ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/

// ble status
static ble_status_typeDef ble_status;
static u32 ble_queryTick, ble_timeoutTick;
// ble start working flag
static bool bleIsworking;

// smRTecgWorking is working flag
static bool ble_adcBufIsLocking;
// smRTecgWorking has sent data num
static u32 ble_spiSentnum;

// buffer to adc sample datas transfered to RSL10 through SPI.
//static u8 adc_data_buf[2 * BUFFER_SIZE_1KB] = {0};
static u8 adc_data_buf[BUFFER_SIZE_512B] = {0};
static size_t adc_data_buf_size = sizeof(adc_data_buf);
static u8 *padc_data_start = &adc_data_buf[0];
static u8 *padc_data_end = &adc_data_buf[0];
static size_t adc_data_num = 0;

// spi send and recv buf
static u8 ble_spiTxBuf[SPI_SEND_NUM];
static u8 ble_spiRxBuf[SPI_SEND_NUM];

// record ble selected adc channel num
static u8 ble_adcChNum1, ble_adcChNum2;
// adc max/min value pair
static int32_t ble_adcMaxValue, ble_adcMinValue;
// adc peak value in 2.5s
static int32 ble_adcPeakMaxValue, ble_adcPeakMinValue;
// trim adc peak timeout tick
static u32 ble_adcTrimPeakTick;

// APP set pulse on value
//static bool ble_pulseOnAction = false;
// ecg set can show pulsing flag
static bool ble_pulseShowFlag = false;
// temp record this value for setting
static u8 ble_VoutSetValue;


/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv private var define end vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/



/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ private function define start ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


/*
  brief:
    1. 
*/
static void ble_enterReqWaitingStatus(void)
{
  // req waiting period
  ble_timeoutTick = HAL_GetTick() + BLE_WAITING_TIMEOUT;

  // RSl10 command query period
  ble_queryTick = HAL_GetTick() + BLE_QUERY_TIMEOUT;

  // continue waiting next command
  ble_status = ble_reqWaiting_status;
}


/*
  brief:
    1. notice: pend point to the next byte of last data.
    2. this is loop buffer;
*/
static void ble_storeAdcDataToBuf(u8 _data)
{
  // store data to buffer
  *padc_data_end++ = _data;
  if(adc_data_num < adc_data_buf_size)
    adc_data_num++;

  // arrive top of buffer?
  if(padc_data_end >= adc_data_buf + adc_data_buf_size)
    padc_data_end -= adc_data_buf_size;

#ifndef LiuJH_DEBUG
  // over flow?? discard the oldest byte that pstart pointed to.
  if(adc_data_num >= adc_data_buf_size)
    padc_data_start = padc_data_end;
#endif

  // ble state machine is locking buffer?
  if(!ble_adcBufIsLocking){
    // trim sent data
    if(ble_spiSentnum){
      padc_data_start += ble_spiSentnum;
      if(padc_data_start >= adc_data_buf + adc_data_buf_size)
        padc_data_start -= adc_data_buf_size;
      adc_data_num -= ble_spiSentnum;
      ble_spiSentnum = 0;
    }
  }
}


/*
  brief:
    1. update adc max and min value for downward;
    2. 
*/
static void ble_trimAdcPeak(void)
{
  // trim timeout?
  if(HAL_GetTick() >= ble_adcTrimPeakTick){
    // if not equal, MUST: max > peak max
    if(!(ble_adcMaxValue ^ ble_adcPeakMaxValue)){
      ble_adcMaxValue += ble_adcPeakMaxValue;
      ble_adcMaxValue >>= 1;
    }

    // if not equal, MUST: min < peak min
    if(!(ble_adcMinValue ^ ble_adcPeakMinValue)){
      ble_adcMinValue += ble_adcPeakMinValue;
      ble_adcMinValue >>= 1;
    }

    // for next update
    ble_adcTrimPeakTick = HAL_GetTick() + ADC_TRIM_PEAK_TIMEOUT;
    ble_adcPeakMaxValue = ADC_MIN_VALUE;
    ble_adcPeakMinValue = ADC_MAX_VALUE;
  }
}

/*
  brief:
    1. condition:
        RTecg is working;
        ble switch ON pulse;
        ecg set show flag;
    2. 
*/
static bool ble_needShowPulsing(void)
{
  bool ret = false;

  // "RTecg is working" ignore(Be checked already)

  if(ble_pulseShowFlag){
    // Only once sending flag
    ble_pulseShowFlag = false;

    ret = true;
  }

  return ret;
}

/*
  brief:
    1. adjust adc data from 12 bits to byte;[0, 255]
*/
static u8 ble_adjustAdcValueToByte(i32 _adcValue)
{
//  return (uint8_t)((_adcValue - ecg_adcMinValue) * 255 / (ecg_adcMaxValue - ecg_adcMinValue));

  // for efficiency
  i32 sub1 = _adcValue - ble_adcMinValue;
  i32 sub2 = ble_adcMaxValue - ble_adcMinValue;
  return (u8)(((sub1 << 8) - sub1) / sub2);
}

/*
  brief:
    1. real-time update max and min value;
    2. real-time record peak value pair per 2.5s;
    3. 
*/
static void ble_recordPeakValue(i32 _adcValue)
{
  // update max and min data
  if(_adcValue > ble_adcMaxValue)
    ble_adcMaxValue = _adcValue;
  if(_adcValue < ble_adcMinValue)
    ble_adcMinValue = _adcValue;

  // record peak value interval 2.5s
  if(_adcValue > ble_adcPeakMaxValue)
    ble_adcPeakMaxValue = _adcValue;
  if(_adcValue < ble_adcPeakMinValue)
    ble_adcPeakMinValue = _adcValue;
}

/*
  brief:
    1. reset RSL10 chip for wakup it;
*/
static void ble_startup(void)
{
#ifndef LiuJH_DEBUG
  // use wakup pin to wakeup RSL10 if magnethall exist(Rising edge)
  HAL_Delay(10);
  HAL_GPIO_WritePin(CCM_PIN20_RSL10_WKUP_GPIO_Port, CCM_PIN20_RSL10_WKUP_Pin, GPIO_PIN_SET);
  HAL_Delay(10);
  HAL_GPIO_WritePin(CCM_PIN20_RSL10_WKUP_GPIO_Port, CCM_PIN20_RSL10_WKUP_Pin, GPIO_PIN_RESET);
#else
  // use NRESET pin of RSL10 chip to wakeup RSL10 if magnethall exist(Rising edge)
  HAL_GPIO_WritePin(CCM_PIN18_RSL10_RST_GPIO_Port, CCM_PIN18_RSL10_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(10);
  HAL_GPIO_WritePin(CCM_PIN18_RSL10_RST_GPIO_Port, CCM_PIN18_RSL10_RST_Pin, GPIO_PIN_SET);
#endif
}

/*
  brief:
    1. OK  statement is: A5 CMD 01 00 00 CS;
    2. Err statement is: A5 CMD 00 00 00 CS;
*/
static void ble_respUserReqOkOrErr(u8 _cmd, bool _isOK)
{
  u8 *ptx = ble_spiTxBuf;
  u16 len = sizeof(ble_spiTxBuf);

  // clear txbuf
  memset(ptx, 0, len);

  /* 1. send OK or ERROR response to RSl10 through SPI */
  // head
  *ptx++ = BLE_P_HEAD;
  // command id
  *ptx++ = _cmd;
  // OK or ERROR
  if(_isOK)
    *ptx++ = BLE_P_OK_FLAG;
  else
    *ptx++ = BLE_P_ERROR_FLAG;

  ptx = ble_spiTxBuf;
  // update check sum
  ptx[SPI_SEND_NUM - 1] = ptx[0] ^ ptx[1] ^ ptx[2];

  // send the response to RSL10 through SPI in blocked mode
  BLE_CS_ASSERTED;
  HAL_SPI_Transmit(&hspi2, ptx, len, BLE_SPI_TIMEOUT);
  BLE_CS_DEASSERTED;
}

/*
  brief:
    1. send response to user with data;
    2. 
*/
static void ble_respUserReqBase(u8 _cmd, u8 *_pdata, u32 _datalen)
{
  u8 *ptx = ble_spiTxBuf;
  u16 len = sizeof(ble_spiTxBuf);
  // all data len except 0 and cs
  u32 num = BLE_P_DATA_INDEX + _datalen;

  // check param
  if(!_pdata || num >= len) return;

  // clear txbuf
  memset(ptx, 0, len);

  /* 1. send u8 data to RSl10 through SPI; */
  // head
  *ptx++ = BLE_P_HEAD;
  // command id
  *ptx++ = _cmd;
  // set data value
  memcpy(ptx, _pdata, _datalen);

  // check xor
  ptx = ble_spiTxBuf + SPI_SEND_NUM - 1;
  *ptx = 0;
  for(u32 i = 0; i < num; i++){
    *ptx ^= ble_spiTxBuf[i];
  }

  // send the response to RSL10 through SPI in blocked mode
  BLE_CS_ASSERTED;
  HAL_SPI_Transmit(&hspi2, ble_spiTxBuf, len, BLE_SPI_TIMEOUT);
  BLE_CS_DEASSERTED;
}


/*
*/
static void ble_startupFpulse(u8 _reqId)
{
  // startup force pulsing
  fpulse_startupPulsing(ble_spiRxBuf[BLE_P_DATA_INDEX]);


  // response RSL10
  ble_respUserReqOkOrErr(_reqId, true);
}

/*
  brief:
    1. 
*/
static void ble_reqWriteAccelCfg(u8 _reqId)
{
  u8 *prx = ble_spiRxBuf + BLE_P_DATA_INDEX;
  mcu_MotionConfig_typeDef *pm = mcu_getMotionCfg();

  /* got accel config values, store them */

  // motion period MSB
  pm->mcu_motionPeriod = *prx++;
  pm->mcu_motionPeriod = pm->mcu_motionPeriod << 8;
  // motion period LSB
  pm->mcu_motionPeriod |= *prx++;
  // motion threshold MSB
  pm->mcu_motionThreshold = *prx++;
  pm->mcu_motionThreshold = pm->mcu_motionThreshold << 8;
  // motion threshold LSB
  pm->mcu_motionThreshold |= *prx;

  // set data valid flag
  pm->isValid = MCU_DATA_STRUCT_VALID_VALUE;

  // response RSL10
  ble_respUserReqOkOrErr(_reqId, true);

  // store this key value into EEPROM
  ee_storeKeyValue(ee_kv_motionPeriod);
}

/*
  brief:
    1. send motion config value to RSL10;
*/
static void ble_reqReadAccelCfg(u8 _reqId)
{
  mcu_MotionConfig_typeDef *pm = mcu_getMotionCfg();
  u8 *ptx = ble_spiTxBuf;
  u16 len = sizeof(ble_spiTxBuf);
  u32 num = 0;

  // clear txbuf
  memset(ptx, 0, len);

  /* 1. send Date Time to RSl10 through SPI; */
  // head
  *ptx++ = BLE_P_HEAD;
  // command id
  *ptx++ = _reqId;

  // pad value
  *ptx++ = (u8)(pm->mcu_motionPeriod >> 8);
  *ptx++ = (u8)(pm->mcu_motionPeriod & 0xFF);
  *ptx++ = (u8)(pm->mcu_motionThreshold >> 8);
  *ptx++ = (u8)(pm->mcu_motionThreshold & 0xFF);

  // length
  num = ptx - ble_spiTxBuf;

  // check xor
  ptx = ble_spiTxBuf + SPI_SEND_NUM - 1;
  *ptx = 0;
  for(u32 i = 0; i < num; i++){
    *ptx ^= ble_spiTxBuf[i];
  }

  // send the response to RSL10 through SPI in blocked mode
  BLE_CS_ASSERTED;
  HAL_SPI_Transmit(&hspi2, ble_spiTxBuf, len, BLE_SPI_TIMEOUT);
  BLE_CS_DEASSERTED;
}

/*
  brief:
    1. RSL10 write pulse holiday date&time(0x35);
    2. format: head + command + 10 bytes data + CS;
    3. data: 
       start time: YY MM DD HH mm
       end time:   YY MM DD HH mm
*/
static void ble_reqWritePulseHolidayDt(u8 _reqId)
{
  pulse_unpulsing_period_typeDef *pdt = pulse_getUnpulsingPeriod();
  u8 *prx = ble_spiRxBuf + BLE_P_DATA_INDEX;

  // store these values
  pdt->startY = *prx++;
  pdt->startMo = *prx++;
  pdt->startD = *prx++;
  pdt->startH = *prx++;
  pdt->startMi = *prx++;
  pdt->endY = *prx++;
  pdt->endMo = *prx++;
  pdt->endD = *prx++;
  pdt->endH = *prx++;
  pdt->endMi = *prx++;

  // calculate minute total for compare
  // (((YY * 12 + MM) * 31 + DD) * 24 + HH) * 60 + mm
  pdt->startDt = (((pdt->startY * 12 + pdt->startMo) * 31 + pdt->startD) * 24 + pdt->startH) * 60 + pdt->startMi;
  pdt->endDt = (((pdt->endY * 12 + pdt->endMo) * 31 + pdt->endD) * 24 + pdt->endH) * 60 + pdt->endMi;

  // set data valid flag
  pdt->isValid = MCU_DATA_STRUCT_VALID_VALUE;

  // response RSL10
  ble_respUserReqOkOrErr(_reqId, true);

  // store this key value into EEPROM
  ee_storeKeyValue(ee_kv_unpulsingPeriod);
}

/*
  brief:
    1. RSL10 read pulse holiday date&time(0x34);
    2. format: head + command + 10 bytes data + CS;
    3. data: 
       start time: YY MM DD HH mm
       end time:   YY MM DD HH mm
*/
static void ble_reqReadPulseHolidayDt(u8 _reqId)
{
  pulse_unpulsing_period_typeDef *pdt = pulse_getUnpulsingPeriod();
  u8 *ptx = ble_spiTxBuf;
  u16 len = sizeof(ble_spiTxBuf);
  u32 num = 0;

  // clear txbuf
  memset(ptx, 0, len);

  /* 1. send Date Time to RSl10 through SPI; */
  // head
  *ptx++ = BLE_P_HEAD;
  // command id
  *ptx++ = _reqId;

  // pad value
  *ptx++ = pdt->startY;
  *ptx++ = pdt->startMo;
  *ptx++ = pdt->startD;
  *ptx++ = pdt->startH;
  *ptx++ = pdt->startMi;
  *ptx++ = pdt->endY;
  *ptx++ = pdt->endMo;
  *ptx++ = pdt->endD;
  *ptx++ = pdt->endH;
  *ptx++ = pdt->endMi;

  // length
  num = ptx - ble_spiTxBuf;

  // check xor
  ptx = ble_spiTxBuf + SPI_SEND_NUM - 1;
  *ptx = 0;
  for(u32 i = 0; i < num; i++){
    *ptx ^= ble_spiTxBuf[i];
  }

  // send the response to RSL10 through SPI in blocked mode
  BLE_CS_ASSERTED;
  HAL_SPI_Transmit(&hspi2, ble_spiTxBuf, len, BLE_SPI_TIMEOUT);
  BLE_CS_DEASSERTED;
}

/*
*/
static void ble_reqWritePulseVoutSetStatus(u8 _reqId)
{
  bool isOK = true;
  u8 *prx = ble_spiRxBuf + BLE_P_DATA_INDEX;

  // check param
  if(*prx > (u8)mcu_Vos_Input_status
    || *prx < (u8)mcu_Vos_OutL_status){
    isOK = false;
  }else{
    // get new value of Voutset
    ble_VoutSetValue = *prx;
    // enter next status for waiting set Vout_set
    ble_status = ble_setVoutSet_status;
  }

  ble_respUserReqOkOrErr(_reqId, isOK);

  // store this key value into EEPROM
  ee_storeKeyValue(ee_kv_VoutSet);
}

/*
  brief:
    1. RSL10 read Rs Rv interval(unit: ms)(0x31);
    2. format: head + command + 13 bytes data + CS;
    3. 13B_RRVi Data Format:
       Data：D1 D2 D3 D4 D5 D6 D7 D8 D9 D10 D11 D12 D13  
       Note：RVSi:    D1.      
             RS_Rtick:D2～D5.
             RS_RRi:  D6～D7.
             RV_Rtick:D8～D11.
             RV_RRi:  D12～D13.
       Unit:ms
*/
static void ble_reqReadRsRvInterval(u8 _reqId)
{
  u8 *ptx = ble_spiTxBuf;
  u16 len = sizeof(ble_spiTxBuf);

  // clear txbuf
  memset(ptx, 0, len);

  /* 1. send Date Time to RSl10 through SPI; */
  // head
  *ptx++ = BLE_P_HEAD;
  // command id
  *ptx++ = _reqId;

  // pad ecg data
  ecg_getRsviAbout(ptx);

  // check xor
  ptx = ble_spiTxBuf + SPI_SEND_NUM - 1;
  *ptx = 0;
  for(u32 i = 0; i < len - 1; i++){
    *ptx ^= ble_spiTxBuf[i];
  }

  // send the response to RSL10 through SPI in blocked mode
  BLE_CS_ASSERTED;
  HAL_SPI_Transmit(&hspi2, ble_spiTxBuf, len, BLE_SPI_TIMEOUT);
  BLE_CS_DEASSERTED;
}

/*
  brief:
    1. when we sending ecg data, check RSL10 req command;
    2. only is curren ch command and connected is OK;
    3. All of others command will return true;
*/
static bool ble_isStopReq(u8 _reqId)
{
  bool ret = true;

#ifdef LiuJH_DEBUG
  if(_reqId == ble_p_stop_readingEcg)
    ret = true;
  else
    ret = false;
#else
  switch(_reqId){
    // rsl10 told mcu: its status is connected with APP(0x62)
    case ble_p_rsl10IsConnected:
      ret = false;
      break;
    // request RT_ECG_RA_IEGM(0x24)
    case ble_p_read_RtEcg_RaIegm:
      if(ble_adcChNum1 == ADC_CH_NUM_RA_IEGM)
        ret = false;
      break;
    // request RT_ECG_RS_RDET(0x25)
    case ble_p_read_RtEcg_RsRdet:
      if(ble_adcChNum1 == ADC_CH_NUM_RS_RDET)
        ret = false;
      break;
    // request RT_ECG_RS_IEGM(0x26)
    case ble_p_read_RtEcg_RsIegm:
      if(ble_adcChNum1 == ADC_CH_NUM_RS_IEGM)
        ret = false;
      break;
    // request RT_ECG_RV_RDET(0x27)
    case ble_p_read_RtEcg_RvRdet:
      if(ble_adcChNum1 == ADC_CH_NUM_RV_RDET)
        ret = false;
      break;
    // request RT_ECG_RV_IEGM(0x28)
    case ble_p_read_RtEcg_RvIegm:
      if(ble_adcChNum1 == ADC_CH_NUM_RV_IEGM)
        ret = false;
      break;
    // RSL10 read twins channel ecg data(0x37)
    case ble_p_read_RtEcg_twins:
      if(ble_adcChNum1 == ADC_CH_NUM_RS_IEGM
        && ble_adcChNum2 == ADC_CH_NUM_RV_IEGM)
        ret = false;
      break;

    default:
      break;
  }
#endif

  return ret;
}

/*
  brief:
    1. 
*/
static void ble_reqStopReadingEcg(u8 _reqId)
{
  // response user req
  ble_respUserReqOkOrErr(_reqId, true);

  // clear session buf
  padc_data_start = &adc_data_buf[0];
  padc_data_end = padc_data_start;
  adc_data_num = 0;

  // continue waiting next command
  ble_enterReqWaitingStatus();
}

/*
  brief:
    1. read date&time;
    2. set mcu data&time;
    3. response rsl10;
*/
static void ble_reqWriteDateTime(u8 _reqId)
{
  u8 *prx = ble_spiRxBuf + BLE_P_DATA_INDEX;
  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};


  // NOW: got date and time, set mcu date&time
  // format: year month day hour minute second weekday; total 7 bytes
  sDate.Year = *prx++;
  sDate.Month = *prx++;
  sDate.Date = *prx++;
  sTime.Hours = *prx++;
  sTime.Minutes = *prx++;
  sTime.Seconds = *prx++;
  sDate.WeekDay = *prx++;
  
  HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
  HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

  // response RSL10
  ble_respUserReqOkOrErr(_reqId, true);
}

/*
*/
static void ble_reqReadDateTime(u8 _reqId)
{
  u8 *ptx = ble_spiTxBuf;
  u16 len = sizeof(ble_spiTxBuf);
  u32 num = 0;

  // clear txbuf
  memset(ptx, 0, len);

  /* 1. send Date Time to RSl10 through SPI; */
  // head
  *ptx++ = BLE_P_HEAD;
  // command id
  *ptx++ = _reqId;

  /*
    7B_DATE Data Format:
    Data：17 0B 10 0A 14 0F 04  
    Note：year+month+day+hour+minute+second+week
          2023.11.16.10：30：15 Thursday
  */
  {
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    // get current time
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    // get current date
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    // fill in buf
    *ptx++ = sDate.Year;
    *ptx++ = sDate.Month;
    *ptx++ = sDate.Date;
    *ptx++ = sTime.Hours;
    *ptx++ = sTime.Minutes;
    *ptx++ = sTime.Seconds;
    *ptx++ = sDate.WeekDay;
  }
  // length
  num = ptx - ble_spiTxBuf;

  // check xor
  ptx = ble_spiTxBuf + SPI_SEND_NUM - 1;
  *ptx = 0;
  for(u32 i = 0; i < num; i++){
    *ptx ^= ble_spiTxBuf[i];
  }

  // send the response to RSL10 through SPI in blocked mode
  BLE_CS_ASSERTED;
  HAL_SPI_Transmit(&hspi2, ble_spiTxBuf, len, BLE_SPI_TIMEOUT);
  BLE_CS_DEASSERTED;
}

/*
  brief:
    1. read pulse config values from RSl10 through SPI;
    2. format: HH MM HH MM delay_ms num width, total 7bytes
*/
static void ble_reqWritePulseConfig(u8 _reqId)
{
  u8 *prx = ble_spiRxBuf + BLE_P_DATA_INDEX;
  ppulse_config_typeDef pconfig = pulse_getConfig();

  // got pulse config values, store them
  // hours of start time
  pconfig->pulse_start_time = (*prx++) * 60;
  // minutes of start time
  pconfig->pulse_start_time += *prx++;
  // hours of end time
  pconfig->pulse_end_time = (*prx++) * 60;
  // minutes of end time
  pconfig->pulse_end_time += *prx++;
  // Rv delay time
  pconfig->pulse_Rv_delay_ms = *prx++;
  // pulse num
  pconfig->pulse_num = *prx++;
  // pulse width
  pconfig->pulse_width = *prx++;
  // pulse Rsvi delay(unit: ms)
  pconfig->pulse_Rsvi_ms = *prx++;

  // set data valid flag
  pconfig->pulse_configIsValid = MCU_DATA_STRUCT_VALID_VALUE;

  // response RSL10
  ble_respUserReqOkOrErr(_reqId, true);

  // store this key value into EEPROM
  ee_storeKeyValue(ee_kv_pulseConfig);
}

/*
  brief:
    1. send pulse config values to RSl10 through SPI;
    2. 
*/
static void ble_reqReadPulseConfig(u8 _reqId)
{
  u8 *ptx = ble_spiTxBuf;
  u16 len = sizeof(ble_spiTxBuf);
  ppulse_config_typeDef pconfig = pulse_getConfig();
  u32 num = 0;

  // clear txbuf
  memset(ptx, 0, len);

  /* 1. send pulse config values to RSl10 through SPI; */
  // head
  *ptx++ = BLE_P_HEAD;
  // command id
  *ptx++ = _reqId;

  /*
    7B_PC Data Format:
    Data: 01 14 03 0A 0F 02 19  
    Note: 
      Pulse_timer+Pulse_Delay+Pulse_Number+Pulse_Wideth.      
      Pulse_timer:01 14 03 0A indicate 01:20～03:10.
      Pulse_Delay:0F indicate 15ms.
      Pulse_Number:02 indicate 2 pulse.
      Pulse_Wideth:19 indicate 2.5ms(25/10).
  */
  *ptx++ = (u8)(pconfig->pulse_start_time / 60);
  *ptx++ = (u8)(pconfig->pulse_start_time % 60);
  *ptx++ = (u8)(pconfig->pulse_end_time / 60);
  *ptx++ = (u8)(pconfig->pulse_end_time % 60);
  *ptx++ = (u8)(pconfig->pulse_Rv_delay_ms);
  *ptx++ = (u8)(pconfig->pulse_num);
  *ptx++ = (u8)(pconfig->pulse_width);
  *ptx++ = (u8)(pconfig->pulse_Rsvi_ms);
  num = ptx - ble_spiTxBuf;

  // check xor
  ptx = &ble_spiTxBuf[SPI_SEND_NUM - 1];
  *ptx = 0;
  for(u32 i = 0; i < num; i++){
    *ptx ^= ble_spiTxBuf[i];
  }

  // send the response to RSL10 through SPI in blocked mode
  BLE_CS_ASSERTED;
  HAL_SPI_Transmit(&hspi2, ble_spiTxBuf, len, BLE_SPI_TIMEOUT);
  BLE_CS_DEASSERTED;
}

/*
  brief:
    1. process all req command;
    2. this function running in ble_smReqWaiting status proc;
    3. NOW: spi is ready;
*/
static void ble_dealReqCommand(void)
{
  u8 data;
  u8 reqid = ble_spiRxBuf[BLE_P_COMMAND_INDEX];

  switch(reqid){
#ifdef LiuJH_DEBUG
    // app told mcu we can LPM(0x11)
    case ble_p_sleep:
      /*
        1. Only go into idle status;
        2. adc will be stopped in pulse process;
        3. enter LPM will be process in mcu;
      */
      ble_status = ble_idle_status;
      // response user OK
      ble_respUserReqOkOrErr(reqid, true);
      break;
#endif
    // app told mcu: startup charging(0x12)
    case ble_p_charge_on:
      wpr_setChargeSwitch(true);
      ble_respUserReqOkOrErr(reqid, true);
      break;
    // app told mcu: stop charging(0x13)
    case ble_p_charge_off:
      wpr_setChargeSwitch(false);
      ble_respUserReqOkOrErr(reqid, true);
      break;
    // read status of charging or not(0x14)
    case ble_p_read_charge_state:
      data = (u8)wpr_isCharging();
      ble_respUserReqBase(reqid, &data, sizeof(data));
      break;
    // startup pulsing(0x15)
    case ble_p_pulse_on:
      if(fpulse_isWorking()){
        ble_respUserReqOkOrErr(reqid, false);
      }else{
        pulse_bleConfigPulseOn(true);
        // response user OK
        ble_respUserReqOkOrErr(reqid, true);
      }
      break;
    // stop pulsing(0x16)
    case ble_p_pulse_off:
      pulse_bleConfigPulseOn(false);
      // response user OK
      ble_respUserReqOkOrErr(reqid, true);
      break;
    // read pulse config(0x17)
    case ble_p_read_pulseConfig:
      ble_reqReadPulseConfig(reqid);
      break;
    // write pulse config(0x18)
    case ble_p_write_pulseConfig:
      ble_reqWritePulseConfig(reqid);
      break;
    // request FOTA(0x19)
    case ble_p_req_fota:
    // rsl10 told mcu: its status is FOTA with APP(0x61)
    case ble_p_rsl10IsFota:
      // store some important value into spi flash;
//      ee_storeKeyValue();
      // told RSL10 OK
      ble_respUserReqOkOrErr(reqid, true);
      ble_status = ble_reqFota_status;
      break;
    // read bpm(0x1A)
    case ble_p_read_bpm:
      data = ecg_getBpm();
      ble_respUserReqBase(reqid, &data, sizeof(data));
      break;
    // read batt level(0x1B)
    case ble_p_read_batt_level:
      data = wpr_getBattPercent();
      ble_respUserReqBase(reqid, &data, sizeof(data));
      break;
    // read motion state(0x1C)
    case ble_p_read_motion_state:
      data = accel_getMotionState();
      ble_respUserReqBase(reqid, &data, sizeof(data));
      break;
    // read mcu date and time(0x1D)
    case ble_p_read_date_time:
      ble_reqReadDateTime(reqid);
      break;
    // write date and time(0x1E)
    case ble_p_write_date_time:
      ble_reqWriteDateTime(reqid);
      break;


    // request RT_ECG_RA_RDET(0x23)
    case ble_p_read_RtEcg_RaRdet:
      // NOTICE: For CCM, I Igored this channel for batt measurement adc;
      ble_respUserReqOkOrErr(reqid, false);
      break;

    // request RT_ECG_RA_IEGM(0x24)
    case ble_p_read_RtEcg_RaIegm:
      // response RSL10
      ble_respUserReqOkOrErr(reqid, true);
      // select adc channel
      ble_adcChNum1 = ADC_CH_NUM_RA_IEGM;
      ble_adcChNum2 = 0;
      // start ecg transfer working
      ble_status = ble_reqRTecg_status;
      break;

    // request RT_ECG_RS_RDET(0x25)
    case ble_p_read_RtEcg_RsRdet:
      // response RSL10
      ble_respUserReqOkOrErr(reqid, true);
      // select adc channel
      ble_adcChNum1 = ADC_CH_NUM_RS_RDET;
      ble_adcChNum2 = 0;
      // start ecg transfer working
      ble_status = ble_reqRTecg_status;
      break;

    // request RT_ECG_RS_IEGM(0x26)
    case ble_p_read_RtEcg_RsIegm:
      // response RSL10
      ble_respUserReqOkOrErr(reqid, true);
      // select adc channel
      ble_adcChNum1 = ADC_CH_NUM_RS_IEGM;
      ble_adcChNum2 = 0;
      // start ecg transfer working
      ble_status = ble_reqRTecg_status;
      break;

    // request RT_ECG_RV_RDET(0x27)
    case ble_p_read_RtEcg_RvRdet:
      // response RSL10
      ble_respUserReqOkOrErr(reqid, true);
      // select adc channel
      ble_adcChNum1 = ADC_CH_NUM_RV_RDET;
      ble_adcChNum2 = 0;
      // start ecg transfer working
      ble_status = ble_reqRTecg_status;
      break;

    // request RT_ECG_RV_IEGM(0x28)
    case ble_p_read_RtEcg_RvIegm:
      // response RSL10
      ble_respUserReqOkOrErr(reqid, true);
      // select adc channel
      ble_adcChNum1 = ADC_CH_NUM_RV_IEGM;
      ble_adcChNum2 = 0;
      // start ecg transfer working
      ble_status = ble_reqRTecg_status;
      break;

    // request ECG of storing in spi flash(0x29)
    case ble_p_read_StEcg_flash:
      // finish in the future
      ble_respUserReqOkOrErr(reqid, false);
//      ble_status = ble_reqSTecg_status;
      break;

    // stop ecg(one of six ecg) reading(0x2A)
    case ble_p_stop_readingEcg:
      // stop ecg
      ble_reqStopReadingEcg(reqid);
      break;


    // RSL10 read pulse working status(0x30)
    case ble_p_read_pulseWorkingStatus:
      data = (u8)pulse_blePulsingIsOn();
      ble_respUserReqBase(reqid, &data, sizeof(data));
      break;
    // RSL10 read Rs Rv interval(unit: ms)(0x31)
    case ble_p_read_RsRvInterval:
      ble_reqReadRsRvInterval(reqid);
      break;
    // RSL10 read pulse Vout_set status(0x32)
    case ble_p_read_pulseVoutSetStatus:
      data = (u8)(mcu_getVoutset()->value);
      ble_respUserReqBase(reqid, &data, sizeof(data));
      break;
    // RSL10 write: pulse Vout_set status(0x33)
    case ble_p_write_pulseVoutSetStatus:
      ble_reqWritePulseVoutSetStatus(reqid);
      break;
    // RSL10 read pulse holiday date&time(0x34)
    case ble_p_read_pulseHolidayDt:
      ble_reqReadPulseHolidayDt(reqid);
      break;
    // RSL10 write pulse holiday date&time(0x35)
    case ble_p_write_pulseHolidayDt:
      ble_reqWritePulseHolidayDt(reqid);
      break;


    // RSL10 read twins channel ecg data(0x37)
    case ble_p_read_RtEcg_twins:
      // response RSL10
      ble_respUserReqOkOrErr(reqid, true);
      // select adc channel
      ble_adcChNum1 = ADC_CH_NUM_RS_IEGM;
      ble_adcChNum2 = ADC_CH_NUM_RV_IEGM;
      // start ecg transfer working
      ble_status = ble_reqRTecg_status;
      break;


    // RSL10 read accel config value(0x39)
    case ble_p_read_AccelCfg:
      ble_reqReadAccelCfg(reqid);
      break;
    // RSL10 write accel config value(0x3A)
    case ble_p_write_AccelCfg:
      ble_reqWriteAccelCfg(reqid);
      break;

    // RSL10 told mcu: force pulsing ignore R wave and others condition(0x3C)
    case ble_p_forcePulsing:
      ble_startupFpulse(reqid);
      break;



/*
    // rsl10 told mcu: its status is IDLE, no other command(0x60)
    case ble_p_rsl10IsIdle:
      // rsl10 is idle, continue query, status dont change
      break;
*/
///*
    // rsl10 told mcu: its status is connected with APP(0x62)
    case ble_p_rsl10IsConnected:
        // igore this reply of rsl10
        break;
//*/
    // rsl10 response mcu's sleep notify(0x63)
    case ble_p_rsl10AgreeEnterLpm:
      // we will enter LPM
      ble_status = ble_idle_status;
      break;

    default:
      ble_respUserReqOkOrErr(reqid, false);
      break;
  }
}

/*
  brief:
    1. check ble_spiRxBuf data;
    2. return true: is valid command and is not IDLE;
*/
static bool ble_commandNeedDeal(void)
{
  bool ret = false;
  u8 *prx = ble_spiRxBuf;
  u32 len = sizeof(ble_spiRxBuf);
  u8 cs = 0;

  // protocol head byte isn't 0xA5?
  if(*prx != BLE_P_HEAD) goto error;

  // command code is idle or connected(ble_p_rsl10IsIdle, ble_p_rsl10IsConnected)?
  if(prx[BLE_P_COMMAND_INDEX] == ble_p_rsl10IsIdle)
    goto error;

  // check sum is correct
  for(u32 i = 0; i < len; i++){
    cs ^= prx[i];
  }
  // check sum is OK?
  if(!cs)
    ret = true;

error:
  return ret;
}

/*
  1. as SPI Master, query command of RSL10(and User APP);
  2. return true: check current command is valid and NOT IDLE; otherwise continue query;
  3. 
*/
static bool ble_queryReqFromUser(void)
{
  u8 *ptx = ble_spiTxBuf;
  // because recv important data, so Rx use global variable
  u8 *prx = ble_spiRxBuf;
  u16 len = sizeof(ble_spiTxBuf);
  bool ret = false;

  // clear tx buf
  memset(ptx, 0, len);
  // clear rx buf
  memset(prx, 0, len);

  // fill txbuf data; A5 5A 00 ... 00 cs
  ptx[0] = 0xA5; ptx[1] = 0x5A;
  // set cs
  ptx[SPI_SEND_NUM - 1] = ptx[0] ^ ptx[1];

  // send data through SPI in blocked mode
  BLE_CS_ASSERTED;
  if(HAL_SPI_TransmitReceive(&hspi2, ptx, prx, len, BLE_SPI_TIMEOUT) == HAL_OK)
    ret = true;
  BLE_CS_DEASSERTED;

  // check command valid and is NOT IDLE
  if(ret)
    ret = ble_commandNeedDeal();

  return ret;
}

/*
  brief:
    1. sending store ecg in spi flash to RSL10 through SPI;
    2. if sending completed, goto reqWaiting status;
*/
static void ble_smReqSTecg(void)
{
}

/*
  brief:
    1. this work may be interrupt by adc sample callback;
    2. So dont modify all var of buf;
    3. Only update sent data num;
    4. SET flag we are working;
    5. 
*/
static void ble_smRTecgWorking(void)
{
  u8 *psrc;
  u8 *ptx = ble_spiTxBuf;
  u8 *prx = ble_spiRxBuf;
  u16 len = 4;
//  u16 len = sizeof(ble_spiTxBuf);
  u16 len2 = sizeof(ble_spiRxBuf);
  u8 id;

  // Reduce sending times; send a mount of data one time.
  if(adc_data_num < len + ble_spiSentnum) return;

  // try to lock spi and deal with command
  if(!mcu_tryLockSpi(MCU_SPI_BLE)) return;

  /* NOW: locked spi, we can send data to RSL10 through SPI in blocked mode */

  {
    memset(ptx, 0, sizeof(ble_spiTxBuf));
    ptx += 6;
  }

  // set adc buffer locking flag
  ble_adcBufIsLocking = true;

  // got the top of data buf?
  psrc = padc_data_start + ble_spiSentnum;
  if(psrc >= adc_data_buf + adc_data_buf_size)
    psrc -= adc_data_buf_size;

  // poll out SPI_SEND_NUM data from adc_data_buf to spi_send_buf, and send it.
  for(int8_t i = 0; i < len; i++){
    // get one data
    ptx[i] = *psrc++;

    if(ptx[i] == 0)
      ptx[i] = 1;

    // got the top of data buf?
    if(psrc >= adc_data_buf + adc_data_buf_size)
      psrc -= adc_data_buf_size;
  }

  // count sent num
  ble_spiSentnum += len;

  // smRTecgWorking's job is finished
  ble_adcBufIsLocking = false;

  /* spi send */
  // clear recv buf command code
  memset(prx, 0, len2);
  ptx = ble_spiTxBuf;

  // send byte to rsl10 through SPI in blocked mode
  BLE_CS_ASSERTED;
  HAL_SPI_TransmitReceive(&hspi2, ptx, prx, len2, BLE_SPI_TIMEOUT);
  BLE_CS_DEASSERTED;

  // check RSL10 status(connected or IDLE or others?)
  id = ble_spiRxBuf[BLE_P_COMMAND_INDEX];

  // start pacing pulse status
/*
  if(id == ){
  }else
*/
  // all others command, we will stop Rtecg, and goto reqwaiting status
  if(ble_isStopReq(id)){
    ble_reqStopReadingEcg(id);
  }
/*
  else
    // rsl10 is connected, continue RtecgWorking
*/

  // so, we can release spi for other device state machine, and process the command in
  // next status;
  mcu_unlockSpi();

}

/*
  brief:
    1. sending real time ecg data to RSl10 through SPI;
    2. if get stop instruction, goto reqWaiting status;
*/
static void ble_smReqRTecg(void)
{
  // start adc
  adc_startup();

  // clear session buf and spi
  padc_data_start = adc_data_buf;
  padc_data_end = padc_data_start;
  adc_data_num = 0;
  ble_adcBufIsLocking = false;
  ble_spiSentnum = 0;

  // update status and wait stop instruction
  ble_status = ble_RTecg_working_status;
}

/*
  brief:
    1. wait ovbc is NOT working;
    2. set Vout_set for next pulse;
*/
static void ble_smSetVoutSet(void)
{
  if(ovbc_isWorking() || fovbc_isWorking()) return;

  // config Vout_set Pin
  mcu_setVoutsetPin(ble_VoutSetValue);

  // continue waiting next command
  ble_enterReqWaitingStatus();
}

/*
  brief:
    1. NOW: all valid command (except idle) will process here;
    2. all protocol need response, So need try lock SPI;
    3. three result:
        a. try lock spi for deal with command at current status;
        b. after dealed command, return reqwaiting status for next command;
        c. after dealed command, update status to other status;
*/
static void ble_smReqDealWith(void)
{
  // try to lock spi and deal with command
  if(!mcu_tryLockSpi(MCU_SPI_BLE)) return;

  /* NOW: we locked spi, can process command and response RSL10 through SPI in blocked mode */

  // deal with command
  ble_dealReqCommand();
  // release spi for other device state machine
  mcu_unlockSpi();

  // current command finished?
  if(ble_status == ble_reqDealWith_status)
  {
    // continue waiting next command
    ble_enterReqWaitingStatus();
  }
}

/*
  brief:
    1. waiting request from RSL10;
    2. read command from SPI each 100ms;
    3. if timeout(30s?), go to LPM;
    4. go into different state machine proc for different command;
*/
static void ble_smReqWaiting(void)
{
  // need read RSL10 request?
  if(HAL_GetTick() < ble_queryTick) return;

  // try to lock spi and query command from RSL10 or APP
  if(!mcu_tryLockSpi(MCU_SPI_BLE)) return;

  /* NOW: we locked spi, can send data to rsl10 through SPI in blocked mode */

  // query user request(the request will receive in ble_spiRxBuf)
  if(ble_queryReqFromUser()){
    // valid command need process
    ble_status = ble_reqDealWith_status;
  }

  // so, we can release spi for other device state machine, and process the command in
  // next status;
  mcu_unlockSpi();

  // still in this status? query loop continue
  if(ble_status == ble_reqWaiting_status){

#ifndef LiuJH_DEBUG // only test: dont sleep
    // current status is timeout?
    if(HAL_GetTick() > ble_timeoutTick){
      // loop in this status if we are charging
      if(wpr_isCharging()){
        // req waiting period
        ble_timeoutTick = HAL_GetTick() + BLE_WAITING_TIMEOUT;

      }else{
        // go into idle status
        ble_status = ble_idle_status;
      }
    }
#endif

    // next loop: RSl10 command query period
    ble_queryTick = HAL_GetTick() + BLE_QUERY_TIMEOUT;
  }
}

/*
  brief:
    1. because of being in while of main, so dont blocking;
    2. waiting sometimes(1 second) and then go into next status;
    3. 
*/
static void ble_smRsl10Waiting(void)
{
  if(HAL_GetTick() < ble_timeoutTick)
    return;

  // trim adc tick startup
  ble_adcTrimPeakTick = HAL_GetTick() + ADC_TRIM_PEAK_TIMEOUT;

  // continue waiting next command
  ble_enterReqWaitingStatus();
}

/*
  brief:
    1. do nothing in this status;
    2. waiting mcu go into LPM;
    3. 
*/
static void ble_smIdle(void)
{
  bleIsworking = false;
}

/*
  brief:
    1. detect magnet wakeup flag;
    2. YES: startup and go into waiting status;
    3. NO: go go into IDLE status;
*/
static void ble_smInited(void)
{
  if(mcu_rstFlag != mcu_rstFlag_MagnetHall){
    
    ble_status = ble_idle_status;
    return;
  }

  // set working flag
  bleIsworking = true;

  // select adc channel for ble
  ble_adcChNum1 = ADC_CH_NUM_RS_IEGM;
  ble_adcChNum2 = 0;

  // wakeup(reset) RSL10 chip
  // HAVE 10ms delay
  ble_startup();

  ble_timeoutTick = HAL_GetTick() + TIMEOUT_1S;
  ble_status = ble_Rsl10Waiting_status;
}


/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv private function define end vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/


/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ public function define start ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


/*
  brief:
    1. NOTE: working in adc callback, So deal quickly and DONT block;
    2. store this data in buf, and process in state machine;
    3. 
*/
void ble_adcConvCpltCB(void)
{
  i32 value;
  u8 byte;

  // get adc sample value
  value = (int32_t)(HAL_ADC_GetValue(&hadc) & 0x0FFF);

  /*
    update adc max and min value;
    record adc peak value pair interval 2.5s;
  */
  ble_recordPeakValue(value);

  // adjust adc value to byte: [0, 255]
  byte = ble_adjustAdcValueToByte(value);

  /*
    start ble real-time process
  */

  // send ecg byte to app through SPI
  if(ble_status == ble_RTecg_working_status){
    // simulate pulsing through updating byte value to 0
    if(ble_needShowPulsing())
      byte = 0;

    // Only store byte into buf(ble state machine will send them through SPI)
    ble_storeAdcDataToBuf(byte);
  }
}


/*
  brief:
    1. When ECG detects the R peak, set this value;
    2. 
*/
void ble_setPulseShowFlag(void)
{
  ble_pulseShowFlag = true;
}

bool ble_isBleAdcCh(u8 _curCh)
{
  return (!(_curCh ^ ble_adcChNum1))
    || (!(_curCh ^ ble_adcChNum2));
}

/*
  brief:
    1. for LPM;
*/
bool ble_isWorking(void)
{
  return bleIsworking;
}

/*
  brief:
    1. working in while of main;
    2. DONT use hal_delay func;
    3. 
*/
void ble_stateMachine(void)
{
  switch(ble_status){
    case ble_inited_status:
      ble_smInited();
      break;
    case ble_idle_status:
      ble_smIdle();
      break;
    case ble_Rsl10Waiting_status:
      ble_smRsl10Waiting();
      break;
    case ble_reqWaiting_status:
      ble_smReqWaiting();
      break;
    case ble_reqDealWith_status:
      ble_smReqDealWith();
      break;
    case ble_setVoutSet_status:
      ble_smSetVoutSet();
      break;
    case ble_reqRTecg_status:
      ble_smReqRTecg();
      break;
    case ble_RTecg_working_status:
      ble_smRTecgWorking();
      break;
    case ble_reqSTecg_status:
      ble_smReqSTecg();
      break;

    case ble_reqFota_status:
      // keep working status, but do nothing, untill RSL10 reset mcu
      break;
    default:
      break;
  }

  // trim adc peak value for R detection
  ble_trimAdcPeak();

}

/*
*/
void ble_resetRSL10(void)
{
  ble_startup();
}

/*
  brief:
    1. 
*/
void ble_init(void)
{
  bleIsworking = false;
  // init peak value
  ble_adcMaxValue = ble_adcPeakMaxValue = ADC_MIN_VALUE;
  ble_adcMinValue = ble_adcPeakMinValue = ADC_MAX_VALUE;
  ble_pulseShowFlag = false;
//  ble_pulseOnAction = false;


  ble_status = ble_inited_status;
}


