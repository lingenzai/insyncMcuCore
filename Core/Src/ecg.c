/*
 * ecg.c
 *
 *  Created on: 2023年11月18日
 *      Author: Johnny
 */

#include "main.h"

/* variable define ************************************************/

static ecg_Status_typeDef ecg_status;
static bool ecgIsWorking;

// ADC sample data buffer for detecting R peak point
static u8 ecg_RsBuf[ECG_BUF_SIZE], ecg_RvBuf[ECG_BUF_SIZE];
// ADC sample data num in Rpeak_buffer
static u32 ecg_RsBufNum, ecg_RvBufNum;
// record the first byte tick in buffer
static u32 ecg_RsBufFirstTick, ecg_RvBufFirstTick;
// record the last byte tick in buffer
static u32 ecg_RsBufLastTick, ecg_RvBufLastTick;

// offset of R1 peak point, max value point in RpeakBuf of 2 second
static u32 ecg_Rs1Off, ecg_Rv1Off;
// record R, max point tick
static u32 ecg_RsTick, ecg_RvTick;
// record av of R1 and R peak point value
static u16 ecg_RsValue, ecg_RsValueV;
static u16 ecg_RvValue, ecg_RvValueV;
/// record R wave average of slope
static int32_t ecg_RsSlopeV, ecg_RvSlopeV;
// rercord detected point num from R1 to R2, and Rn
static u32 ecg_RsDetected, ecg_RvDetected;

// RR interval point number
static u32 ecg_RRsiPoint, ecg_RRviPoint;
// RR interval time(point num * sample time per point ECG_RR_TIME_PER_POINT)
static u32 ecg_RRsiTime, ecg_RRviTime;
// adc data time per point(us)
static u32 ecg_RsPPiTime, ecg_RvPPiTime;
static u32 ecg_RsBpm, ecg_RvBpm;

// record interval(unit: ms) between Rs and Rv
// this value will store in EE before LPM
static u32 ecg_RsviTime;

// record num of Rn detected
static u32 ecg_RnDetected;
// record num of Rn continue escaped num and All num
static u32 ecg_RnEscaped, ecg_RnEscapedAll;

// adc max/min value pair
static int32_t ecg_adcMaxValue, ecg_adcMinValue;
// adc peak value in 2.5s
static int32 ecg_adcPeakMaxValue, ecg_adcPeakMinValue;
// trim adc peak timeout tick
static u32 ecg_adcTrimPeakTick;
// trim RDET adc sample some times at first
// and then it is flag of the first data tick in buffer
static u32 ecg_adcStableTick;
// record process finished
static bool ecg_RsOk, ecg_RvOk;


/* private fuction define *****************************************/


/*
  brief:
    1. unit is ms;
*/
static void ecg_calculateRsvi(void)
{
  u32 RRi_ms1, RRi_ms2;

  if(ecg_RsTick > ecg_RvTick){
    // we must move Rv peak point
    RRi_ms1 = (ecg_RsTick - ecg_RvTick) % ecg_RRviTime;
    RRi_ms2 = ecg_RRviTime - RRi_ms1; // is correct
  }else{
    // we must move Rs peak point
    RRi_ms1 = (ecg_RvTick - ecg_RsTick) % ecg_RRsiTime; // is correct
    RRi_ms2 = ecg_RRsiTime - RRi_ms1;
  }

  // record min value
  ecg_RsviTime = (RRi_ms1 < RRi_ms2) ? (RRi_ms1) : (RRi_ms2);
}

/*
  brief:
    1. update adc max and min value for downward;
    2. 
*/
static void ecg_trimAdcPeak(void)
{
  // trim timeout?
  if(HAL_GetTick() >= ecg_adcTrimPeakTick){
    // if not equal, MUST: max > peak max
    if(!(ecg_adcMaxValue ^ ecg_adcPeakMaxValue)){
      ecg_adcMaxValue += ecg_adcPeakMaxValue;
      ecg_adcMaxValue >>= 1;
    }

    // if not equal, MUST: min < peak min
    if(!(ecg_adcMinValue ^ ecg_adcPeakMinValue)){
      ecg_adcMinValue += ecg_adcPeakMinValue;
      ecg_adcMinValue >>= 1;
    }

    // for next update
    ecg_adcTrimPeakTick = HAL_GetTick() + ADC_TRIM_PEAK_TIMEOUT;
    ecg_adcPeakMaxValue = ADC_MIN_VALUE;
    ecg_adcPeakMinValue = ADC_MAX_VALUE;
  }
}

/*
  brief:
    1. adjust adc data from 12 bits to byte;[0, 255]
*/
static u8 ecg_adjustAdcValueToByte(i32 _adcValue)
{
//  return (uint8_t)((_adcValue - ecg_adcMinValue) * 255 / (ecg_adcMaxValue - ecg_adcMinValue));

  // for efficiency
  i32 sub1 = _adcValue - ecg_adcMinValue;
  i32 sub2 = ecg_adcMaxValue - ecg_adcMinValue;
  return (u8)(((sub1 << 8) - sub1) / sub2);
}

/*
  brief:
    1. real-time update max and min value;
    2. real-time record peak value pair per 2.5s;
    3. 
*/
static void ecg_recordPeakValue(i32 _adcValue)
{
  // update max and min data
  if(_adcValue > ecg_adcMaxValue)
    ecg_adcMaxValue = _adcValue;
  if(_adcValue < ecg_adcMinValue)
    ecg_adcMinValue = _adcValue;

  // record peak value interval 2.5s
  if(_adcValue > ecg_adcPeakMaxValue)
    ecg_adcPeakMaxValue = _adcValue;
  if(_adcValue < ecg_adcPeakMinValue)
    ecg_adcPeakMinValue = _adcValue;
}

/*
  brief:
    1. Add adc byte current tick into buf at R1waint status;
    2. 
*/
static void ecg_addAdcDataToRbuf(u8 _curCh, u8 _data)
{
  bool isRsCh = _curCh ^ ADC_CH_NUM_RV_RDET;
  u8 *pbuf;
  u32 *pnum;
  u32 *pftick, *pltick;

  // Is Rs channel data?
  if(isRsCh){
    pbuf = ecg_RsBuf;
    pnum = &ecg_RsBufNum;
    pftick = &ecg_RsBufFirstTick;
    pltick = &ecg_RsBufLastTick;
  }else{  // is Rv channel data
    pbuf = ecg_RvBuf;
    pnum = &ecg_RvBufNum;
    pftick = &ecg_RvBufFirstTick;
    pltick = &ecg_RvBufLastTick;
  }

  // buffer is full?
  if(*pnum < ECG_BUF_SIZE){
    // It is the first data? record the tick
    if(*pnum == 0)
      *pftick = HAL_GetTick();

    // add adc data to buf
    pbuf[*pnum] = _data;
    *pnum += 1;

    // collected the last byte?
    if(*pnum == ECG_BUF_SIZE){
      // store current tick(buf last data tick)
      *pltick = HAL_GetTick();
    }
  }
}

/*
  brief:
    1. the param data list is: Pl6 Pl5 Pl4 Pl3 Pl2 Pl1 P0 Pr1 Pr2 Pr3 Pr4 Pr5 Pr6;
    2. Because P0 is the max value point so...
    3. We will get eight slope:
        Sl3 = (Pl3 - Pl6) / 3; Sl2 = (Pl2 - Pl5) / 3; Sl1 = (Pl1 - Pl4) / 3; Sl0 = (Pl0 - Pl3) / 3;
        Sr3 = (Pr3 - Pr6) / 3; Sr2 = (Pr2 - Pr5) / 3; Sr1 = (Pr1 - Pr4) / 3; Sr0 = (Pr0 - Pr3) / 3;
    4. will return the max value of these eight slope;
*/
static int32_t ecg_calculateSlope(u8 *_pbufindex)
{
  int32 ret = 0;
  int32 buf[8];
  int32 *p = buf;
  // pointer big value point
  u8 *p0 = _pbufindex - ECG_SLOPE_STEP;
  // pointer small value point
  u8 *p3 = p0 - ECG_SLOPE_STEP;
  int i;

  // calculate four slopes at left R point, and sotre in buf
  for(i = 0; i < ECG_SLOPE_NUM; i++){
    *p++ = ((*p0++) - (*p3++)) / 3;
  }

  // calculate four slopes at right R point, and sotre in buf
  /*
    NOW:
      1. p0 pointer R point;
      2. update p3, will pointer the right of R point;
  */
  p0 = _pbufindex;
  p3 = p0 + ECG_SLOPE_STEP;
  // calculate four slopes at right R point, and sotre in buf
  for(i = 0; i < ECG_SLOPE_NUM; i++){
    *p++ = ((*p0++) - (*p3++)) / 3;
  }

  // adjust all value to Absolute value
  for(i = 0; i < 8; i++){
    if(buf[i] < 0){
      buf[i] = -buf[i];
    }
  }

  // get the max slope of these eight values, and return it
  for(i = 0; i < 8; i++){
    if(buf[i] > ret){
      ret = buf[i];
    }
  }

  return ret;
}

static void ecg_init2(void)
{
  // init R peak point buf
  ecg_RsBufNum = ecg_RvBufNum = 0;
  ecg_RsBufFirstTick = ecg_RvBufFirstTick = 0;
  ecg_RsBufLastTick = ecg_RvBufLastTick = 0;

  // init var about R1 and R2
  ecg_Rs1Off = ecg_Rv1Off = 0;
  ecg_RsTick = ecg_RvTick = 0;
  ecg_RsValue = ecg_RvValue = 0;
  ecg_RsValueV = ecg_RvValueV = 0;
  ecg_RsSlopeV = ecg_RvSlopeV = 0;
  ecg_RsDetected = ecg_RvDetected = 0;
  ecg_RRsiPoint = ecg_RRviPoint = 0;
  ecg_RRsiTime = ecg_RRviTime = 0;
  ecg_RsPPiTime = ecg_RvPPiTime = 0;
  ecg_RsOk = ecg_RvOk = false;

  // init param for APP
  ecg_RsBpm = ecg_RvBpm = 0;
  ecg_RsviTime = 0;

  // init var for Rn
  ecg_RnDetected = 0;
  ecg_RnEscaped = 0;
  ecg_RnEscapedAll = 0;
}

/*
  brief:
    1. We have detected the num of ecg_r_detected data;
    3. Use R move window methold, 9 points(4 + 1 + 4);
    4. Check inflection in R window;
    5. if fixed Rn peak point, update this data to opposite and return;
*/
static void ecg_cbSmRnDetect(u8 _data)
{
  u32 detectmin, detectmax;
  // prevdata2, prevdata1, _data is the time line adc data
  // _data is current data
  static u8 prevd1 = 0, prevd2 = 0;
  static u32 tick = 0;

#ifdef LiuJH_DEBUG
  // For testing Rn detected ERROR
  if(ecg_RsBufNum >= ECG_BUF_SIZE)
    ecg_RsBufNum = 0;
  ecg_addAdcDataToRbuf(ADC_CH_NUM_RS_RDET, _data);
#endif

  // define R move window size for detecting R peak point
  detectmin = ecg_RRsiPoint - ECG_Rn_MW_HALF_SIZE;
  detectmax = ecg_RRsiPoint + ECG_Rn_MW_HALF_SIZE;

  // have enough data? need detecting?
  if(ecg_RsDetected <= detectmin){
    // The first point of Rn move window, rocord it, dont need detect
    prevd2 = prevd1;
    prevd1 = _data;
    tick = HAL_GetTick();

    // dont need checking, skip it
    ecg_RsDetected++;

    return;
  }

  // escape? continue next R peak point detect
  if(ecg_RsDetected > detectmax){
    // record Rn detected and escaped num
    ecg_RnDetected++;
    ecg_RnEscaped++;
    ecg_RnEscapedAll++;

    // twice continue escaped? restart R1 waiting...
#ifndef LiuJH_DEBUG
    // test only
    if(ecg_RnEscaped >= 8)
#else
    if(ecg_RnEscaped >= ECG_ESCAPED_MAX_NUM)
#endif
    {
      // redo R detect, restart up ecg detection
      ecg_status = ecg_startup_status;
    }else{
      // temp work, continue detecting next Rn peak point
      ecg_RsDetected = ECG_Rn_MW_HALF_SIZE;
    }
    return;
  }

  /*
    NOW:
      1. We have eight data in [RRi-3, RRi+4];
      2. Compare prev data and current data;
      3. if exist inflection, the prev data is Rn peak point;
  */

  // check the inflection and the peak point value
  if(prevd1 > ecg_RsValueV && prevd1 > _data && prevd1 > prevd2){
    // update R value and R value V
    ecg_RsValue = (ecg_RsValue + prevd1) >> 1;
    ecg_RsValueV = ecg_RsValue * ECG_R_VALUE_WEIGHT;
    // record current R peak point tick value for pulse
    ecg_RsTick = tick;

    // record detected num
    ecg_RnDetected++;
    // clear escaped continued
    ecg_RnEscaped = 0;

    // Need trim current RRi or NOT???
    if(ecg_RRsiPoint != ecg_RsDetected){
      ecg_RRsiPoint += ecg_RsDetected;
      ecg_RRsiPoint >>= 1;
    }

    // for detecting next Rn+1 peak point(prevdata is Rn peak point)
    ecg_RsDetected = 0;

#ifndef LiuJH_DEBUG
    // toast ble this tick of R peak point detected
    ble_setPulseShowFlag();
#endif

    // trigger pulse sending
    if(ecg_RnDetected >= ECG_RN_DETECTED_MIN_NUM)
      pulse_setPulsingFlag();

  }  else{
    // update prev data for next compare
    prevd2 = prevd1;
    prevd1 = _data;
    tick = HAL_GetTick();
  }

  // count for next R peak point detecting
  ecg_RsDetected++;
}

/*
  brief:
    1. Now: we got ecg_RR_interval and ecg_PPi_us;
    2. get RRi_ms;
    3. for CCM:
      if adc channel is ADC_CH_NUM_RV_RDET, redo for ADC_CH_NUM_RS_RDET;
    4. ecg will always working in channel of ADC_CH_NUM_RS_RDET;
*/
static void ecg_smRnWaiting2(void)
{
  u32 tick;

  // get RRi_ms and ecg_bpm for Rs
  ecg_RRsiTime = ecg_RRsiPoint * ecg_RsPPiTime / 1000;
  if(ecg_RRsiTime)
    ecg_RsBpm = 60 * 1000 / ecg_RRsiTime;

  // get RRi_ms and ecg_bpm for Rv
  ecg_RRviTime = ecg_RRviPoint * ecg_RvPPiTime / 1000;
  if(ecg_RRviTime)
    ecg_RvBpm = 60 * 1000 / ecg_RRviTime;

  // check bpm is equal
//  if(ecg_RsBpm != ecg_RvBpm){
  // check RR point is equal or sub is 1
  if(ecg_RRsiPoint != ecg_RRviPoint
    && ecg_RRsiPoint != ecg_RRviPoint + 1
    && ecg_RRsiPoint != ecg_RRviPoint - 1){

    // restart detect both of Rs and Rv channel
    ecg_status = ecg_startup_status;

    return;
  }

  /* NOW: we can calculate Rs_Rv delay time(unit: ms) */
  ecg_calculateRsvi();

  /*
    NOW:
      1. we get RR_internal, RRi_ms and ecg_bpm;
      2. Base current tick, we can know next R peak point appear time;
      3. we only detect Rs-RDET;
  */
  // Base R1 tick and current tick, calculate R_detected value for next Rn peak point
  tick = HAL_GetTick() - ecg_RsTick;
  if(ecg_RRsiTime)
    tick %= ecg_RRsiTime;
  ecg_RsDetected = tick * 1000 / ecg_RsPPiTime;

#ifdef LiuJH_DEBUG
  /*
    For testing:
      1. Loop record data in buffer;
      2. if full, num set 0, and loop record data in buffer continue;
      3. for each Rn detected, check where is ERROR;
  */
  ecg_RsBufNum = 0;
#endif
  // detected Rn num and escaped Rn num init
  ecg_RnDetected = ecg_RnEscaped = 0;

  // start detecting Rn using R move window, so go into next status
  ecg_status = ecg_RnDetect_status;
}

/*
  brief:
    1. Now: we got RR_internal;
    2. We fixed RRi value, skip all missed R peak point in
      loop buffer, and collect new data to loop buffer for
      fresh Rn peak point for deal it;
    3. Got PPi_us, RRi_ms, bpm KeyValues;
*/
static void ecg_cbSmRnWaiting(u8 _curCh, u8 _data)
{
  bool isRsCh = _curCh ^ ADC_CH_NUM_RV_RDET;
  // current point tick
  u32 tick;

  // add current data into buf
  ecg_addAdcDataToRbuf(_curCh, _data);

  /* calculate time(Unit: us) per point */
  if(isRsCh && !ecg_RsOk){
    // if some data escape buffer
    if(ecg_RsBufNum >= ECG_BUF_SIZE){
      tick = ecg_RsBufLastTick - ecg_RsBufFirstTick;
    }else{
      tick = HAL_GetTick() - ecg_RsBufFirstTick;
    }

    // calculate time(Unit: us) per point
    ecg_RsPPiTime = tick * 1000 / (ecg_RsBufNum - 1);
    ecg_RsOk = true;
  }else if(!isRsCh && !ecg_RvOk){
    // if some data escape buffer
    if(ecg_RvBufNum >= ECG_BUF_SIZE){
      tick = ecg_RvBufLastTick - ecg_RvBufFirstTick;
    }else{
      tick = HAL_GetTick() - ecg_RvBufFirstTick;
    }

    // calculate time(Unit: us) per point
    ecg_RvPPiTime = tick * 1000 / (ecg_RvBufNum - 1);
    ecg_RvOk = true;
  }

  // goto next status
  if(ecg_RsOk && ecg_RvOk){
    ecg_status = ecg_Rnwaiting2_status;
    ecg_RsOk = ecg_RvOk = false;
  }
}

/*
  brief:
    1. NOW: we have five important value:
        index of R1 peak pint;
        ecg_R1Tick;
        ecg_R_slopeV;
    2. calculate slope of four bytes(before R2 peak point);
    3. if greate or equal ecg_R1slope_V, the R2 will appear;
    4. 
*/
static void ecg_smR2Detect(void)
{
  bool *pOk;
  // pointer current point(will check this point)
  u8 *p;
  // pointer next point for comparing with *p
  u8 *pn;
  u32 *pdetected = NULL, *poff, *pRRi;
  u16 *pvalue, *pvalueV;
  int32 *pslopeV;
  int32_t slope;

  // NOTICE: exceed, deal here
  if(ecg_RsDetected + ECG_R2_MW_SIZE + 1 > ECG_BUF_SIZE
    || ecg_RvDetected + ECG_R2_MW_SIZE + 1 > ECG_BUF_SIZE){
    // redo R detect, restart up ecg detection
    ecg_status = ecg_startup_status;
    return;
  }

  // We have enough bytes for detecting in Rs buf?
  if(!ecg_RsOk && ecg_RsBufNum > ecg_RsDetected + ECG_R2_MW_SIZE){
    p = ecg_RsBuf + ecg_RsDetected;
    pdetected = &ecg_RsDetected;
    pvalue = &ecg_RsValue;
    pvalueV = &ecg_RsValueV;
    poff = &ecg_Rs1Off;
    pslopeV = &ecg_RsSlopeV;
    pRRi = &ecg_RRsiPoint;
    pOk = &ecg_RsOk;
  }else if(!ecg_RvOk && ecg_RvBufNum > ecg_RvDetected + ECG_R2_MW_SIZE){
    // We have enough bytes for detecting in Rv buf
    p = ecg_RvBuf + ecg_RvDetected;
    pdetected = &ecg_RvDetected;
    pvalue = &ecg_RvValue;
    pvalueV = &ecg_RvValueV;
    poff = &ecg_Rv1Off;
    pslopeV = &ecg_RvSlopeV;
    pRRi = &ecg_RRviPoint;
    pOk = &ecg_RvOk;
  }else{
    /*
      have not enough data or cur ch is OK,
      loop continue waiting data or other ch
    */
    goto ecg_smR2Detectend;
  }

  // pointer next point for comparing with *p
  pn = p + 1;

  // this point is NOT suspected R peak point? flag this point is detected, and continue next detect
  if(*p < *pvalueV) goto ecg_smR2Detectend;
  
  /*
    NOW:
      1. *p is suspected R peak point;
      2. we will check other param for confirming this point;
  */

  // check this point is the max value point in next six point
  for(int i = 0; i < ECG_R2_MW_SIZE; i++){
    // current point is not max value point? continue next detect
    if(pn[i] > *p) goto ecg_smR2Detectend;
  }

  // check slope
  slope = ecg_calculateSlope(p);
  if(slope < *pslopeV) goto ecg_smR2Detectend;

  /*
    NOW:
      1. we found R2 point(*p);
      2. record important params for us;
  */

  // record ecg_RR_interval value
  *pRRi = *pdetected - *poff;

#ifdef LiuJH_DEBUG
  // average R value
  *pvalue = (*pvalue + *p) >> 1;
  // update R value V
  *pvalueV = (*pvalue) * ECG_R_VALUE_WEIGHT;
#endif

  // detect finished?(both rs rv channel)
  *pOk = true;

ecg_smR2Detectend:

  // continue detect
  if(pdetected)
    *pdetected += 1;

  // detected OK both?
  if(ecg_RsOk && ecg_RvOk){
    // go into next status
    ecg_status = ecg_Rnwaiting_status;
    ecg_RsOk = ecg_RvOk = false;
  }
}

/*
  brief:
    1. The ADC sample callback function is Storing adc u8 value in RpeakBuf
      and record current max and min value index in pRpPointmax and pRpPointmin;
    2. Get average of slope of R1;
    3. Get R1 index and tick;
    4. Update status to detect R2;
*/
static void ecg_smR1Detect(void)
{
  // NOTICE: exceed max size of buffer??
  if(ecg_Rs1Off + ECG_R2_MW_SIZE >= ECG_BUF_SIZE
    || ecg_Rv1Off + ECG_R2_MW_SIZE >= ECG_BUF_SIZE){
    // redo R detect, restart up ecg detection
    ecg_status = ecg_startup_status;
    return;
  }

  // calculate R peak point slope and slope V value(0.80)
  ecg_RsSlopeV = ecg_calculateSlope(ecg_RsBuf + ecg_Rs1Off) * ECG_SLOPE_WEIGHT;
  ecg_RvSlopeV = ecg_calculateSlope(ecg_RvBuf + ecg_Rv1Off) * ECG_SLOPE_WEIGHT;

  // get R1 value V for detect R2(NOTICE: MUST more than 0.83)
  ecg_RsValueV = ecg_RsValue * ECG_R_VALUE_WEIGHT;
  ecg_RvValueV = ecg_RvValue * ECG_R_VALUE_WEIGHT;

  /*
    NOW:
      1. redo R1 detect if the slopeV < ECG_SLOPEV_DEFAULT;
  */
  if(ecg_RsSlopeV < ECG_SLOPEV_DEFAULT || ecg_RvSlopeV < ECG_SLOPEV_DEFAULT){
    // redo R detect, restart up ecg detection
    ecg_status = ecg_startup_status;
    return;
  }

  /*
    NOW:
      1. R1 detect status process finished.
      2. Ignore points of the left of R1 peak point;
      3. init for next status(skip 180 ms from R1, and then start find R2);
  */
  ecg_RsDetected = ecg_Rs1Off + ECG_R2_SKIP_POINT_NUM;
  ecg_RvDetected = ecg_Rv1Off + ECG_R2_SKIP_POINT_NUM;

  ecg_status = ecg_R2Detect_status;
}

/*
  brief:
    1. 
*/
static void ecg_cbSmR1Waiting(u8 _curCh, u8 _data)
{
  bool isRsCh = _curCh ^ ADC_CH_NUM_RV_RDET;
  u8 *pbuf;
  u32 *pnum, *poff, *ptick;
  u16 *pvalue;

  // wave is stable? we can start collecting??
  if(HAL_GetTick() < ecg_adcStableTick) return;

  /* NOW: we can collect data into buffer */

  // start collect data into Rs and Rv buffer
  ecg_addAdcDataToRbuf(_curCh, _data);

  // current channel is Rs-RDET channel?
  if(isRsCh && !ecg_RsOk){
    pbuf = ecg_RsBuf;
    pnum = &ecg_RsBufNum;
    pvalue = &ecg_RsValue;
    poff = &ecg_Rs1Off;
    ptick = &ecg_RsTick;
  }else if(!isRsCh && !ecg_RvOk){
    // current channel is Rv-RDET channel?
    pbuf = ecg_RvBuf;
    pnum = &ecg_RvBufNum;
    pvalue = &ecg_RvValue;
    poff = &ecg_Rv1Off;
    ptick = &ecg_RvTick;
  }else{
    /*
      have not enough data or cur ch is OK,
      loop continue waiting data or other ch
    */
    goto ecg_cbSmR1Waitingend;
  }

  /*
    1. ignore some data in the starting of buf(for slope);
    2. is the peak point? record it;
    3. NOTICE: make sure ecg_R_value is the last max value;
  */

  // check cur data is the max value data? YES, record it
  if(*pnum > ECG_R2_MW_SIZE && _data >= *pvalue){
    // record data value
    *pvalue = _data;
    // record data offset in buf
    *poff = (*pnum) - 1;
    // record data tick
    *ptick = HAL_GetTick();
  }

  // exceed buffer?
  if(*pnum >= ECG_BUF_SIZE
    // check the wave is stable or not(Continuous of max value)
    || (*pvalue == ECG_MAX_BYTE_VALUE
    && pbuf[*poff - 1] == ECG_MAX_BYTE_VALUE)){
    // restart collect data
    ecg_status = ecg_startup_status;
    goto ecg_cbSmR1Waitingend;
  }

  // collect data finished? update ecg status
  if(*pnum >= ECG_BUF_R1_NEED_NUM
    // for slope data num
    && *pnum > *poff + ECG_R2_MW_SIZE){
    // which channel finished?
    if(isRsCh)
      ecg_RsOk = true;
    else
      ecg_RvOk = true;
  }

ecg_cbSmR1Waitingend:
  // collecting data finished both Rs-RDET and Rv-RDET channel?
  if(ecg_RsOk && ecg_RvOk){
    // got into next status for R1 detecting
    ecg_status = ecg_R1Detect_status;

    // clear static var for next detect
    ecg_RsOk = ecg_RvOk = false;
  }
}

/*
  brief:
    1. Start up ecg ONCE adc starting up;
    2. NOTE: DONNT update current adc channel num;
*/
static void ecg_smStartup(void)
{
  // reinit
  ecg_init();

  // ecg start working flag
  ecgIsWorking = true;
  // trim adc tick startup
  ecg_adcTrimPeakTick = HAL_GetTick() + ADC_TRIM_PEAK_TIMEOUT;

  // start R detection
  ecg_status = ecg_R1waiting_status;
}

/*
  brief:
    1. Running in adc CB, MUST process quickly;
    2. 
*/
static void ecg_cbStateMachine(u8 _curCh, u8 _adcByte)
{
  // Some status will real-time process with adcCB
  switch(ecg_status){
    case ecg_R1waiting_status:
      ecg_cbSmR1Waiting(_curCh, _adcByte);
      break;
    case ecg_R1Detect_status:
    case ecg_R2Detect_status:
      // in these status, collect data into buf only
      ecg_addAdcDataToRbuf(_curCh, _adcByte);
      break;
    case ecg_Rnwaiting_status:
      ecg_cbSmRnWaiting(_curCh, _adcByte);
      break;
    case ecg_RnDetect_status:
      if(_curCh ^ ADC_CH_NUM_RV_RDET)
        ecg_cbSmRnDetect(_adcByte);
      break;

    default:
      // NOTE: other status dont add data to buf
      break;
  }
}

/* public function define *****************************************/

/*
*/
void ecg_stateMachine(void)
{
  switch(ecg_status){
    case ecg_inited_status:
      // do noting, waiting start up when adc start up
      break;
    case ecg_idle_status:
      // repeatedly set no working
      ecgIsWorking = false;
      break;
    case ecg_startup_status:
      ecg_smStartup();
      break;
    case ecg_R1Detect_status:
      ecg_smR1Detect();
      break;
    case ecg_R2Detect_status:
      ecg_smR2Detect();
      break;
    case ecg_Rnwaiting2_status:
      ecg_smRnWaiting2();
      break;
    case ecg_RnDetect_status:
      break;

    default:
      break;
  }

  // trim adc peak value for R detection
  ecg_trimAdcPeak();

  // work over?
  if(!adc_isWorking())
    ecg_status = ecg_idle_status;
}

/*
  brief:
    1. adc_CB toast ecg, real-time process data;
    2. add in ecg buf, and update vars about buffer;
    3. This state machine only process all vars about loop buf,
       all other jobs is processed in state machine of main while;
    4. This function is always working at every ecg status;
    5. This function is working in adc callback interrupt,
       so please dont waste time.
*/
void ecg_adcConvCpltCB(u8 _curCh)
{
  i32 value;
  u8 byte;

  if((_curCh ^ ADC_CH_NUM_RS_RDET) && (_curCh ^ ADC_CH_NUM_RV_RDET))
    return;

  // get adc sample value
  value = (int32_t)(HAL_ADC_GetValue(&hadc) & 0x0FFF);

  /*
    update adc max and min value;
    record adc peak value pair interval 2.5s;
  */
  ecg_recordPeakValue(value);

  // adjust adc value to byte: [0, 255]
  byte = ecg_adjustAdcValueToByte(value);

  /*
    start ecg real-time state machine process
  */
  ecg_cbStateMachine(_curCh, byte);
}

/*
  13B_RRVi Data Format:
         Data：D1 D2 D3 D4 D5 D6 D7 D8 D9 D10 D11 D12 D13
         Note：RSVi:    D1.
               RS_Rtick:D2～D5.
               RS_RRi:  D6～D7.
               RV_Rtick:D8～D11.
               RV_RRi:  D12～D13.
         Unit: ms
*/
bool ecg_getRsviAbout(u8 *_pdata)
{
  bool ret = false;
  u8 *p = _pdata;

  // check param
  if(!_pdata) return ret;

  *p++ = (u8)ecg_RsviTime;
  *p++ = (u8)(ecg_RsTick >> 24);
  *p++ = (u8)(ecg_RsTick >> 16);
  *p++ = (u8)(ecg_RsTick >> 8);
  *p++ = (u8)(ecg_RsTick);
  *p++ = (u8)(ecg_RRsiTime >> 8);
  *p++ = (u8)(ecg_RRsiTime);
  *p++ = (u8)(ecg_RvTick >> 24);
  *p++ = (u8)(ecg_RvTick >> 16);
  *p++ = (u8)(ecg_RvTick >> 8);
  *p++ = (u8)(ecg_RvTick);
  *p++ = (u8)(ecg_RRviTime >> 8);
  *p++ = (u8)(ecg_RRviTime);

  return true;
}

u8 ecg_getRsvi(void)
{
  return (u8)ecg_RsviTime;
}

/*
  brief:
    1. return current Rn peak point tick vlaue;
*/
u32 ecg_getRnTick(void)
{
  return ecg_RsTick;
}

/*
*/
u8 ecg_getBpm(void)
{
  return (u8)ecg_RsBpm;
}

/*
  brief:
    1. We want get Rs-RDET and Rv-RDET data;
*/
bool ecg_isEcgAdcCh(u8 _curCh)
{
  return (!(_curCh ^ ADC_CH_NUM_RS_RDET) || !(_curCh ^ ADC_CH_NUM_RV_RDET));
}

/*
  brief:
    1. 
*/
void ecg_startup(void)
{
  if(ecgIsWorking) return;

  // adc stable tick
  ecg_adcStableTick = HAL_GetTick() + ADC_WORKING_STABLE_TIME;

  // start up ecg detection
  ecg_status = ecg_startup_status;
}

/*
*/
void ecg_init(void)
{
  ecgIsWorking = false;

  ecg_init2();

  // init peak value
  ecg_adcMaxValue = ecg_adcPeakMaxValue = ADC_MIN_VALUE;
  ecg_adcMinValue = ecg_adcPeakMinValue = ADC_MAX_VALUE;

  // status init
  ecg_status = ecg_inited_status;
}


