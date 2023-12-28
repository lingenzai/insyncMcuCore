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
static uint8_t ecg_Rpeak_buf[ECG_RPEAK_BUF_SIZE];
// ADC sample data num in Rpeak_buffer
static u32 ecg_Rpeak_buf_num;
// record the last byte tick in buffer
static u32 ecg_bufLastDataTick;

// Index of R1 peak point, max point in RpeakBuf of 2.5 second
static u8 *ecg_R1_pindex;
// record R, max point tick
static u32 ecg_R_tick;
// record av of R1 and R2 peak point value
static u16 ecg_R_value;
static u16 ecg_R_valueV;
/// record R wave average of slope
static int32_t ecg_R_slopeV;
// rercord detected point num from R1 to R2
static u32 ecg_R_detected;


/*
  record detected byte number from R1 point for R2 peak point.
  this num is a time line(4ms per point).
*/
// RR interval point number
static u32 ecg_RR_interval;
// RR interval time(point num * sample time per point ECG_RR_TIME_PER_POINT)
static u32 ecg_RRi_ms;
// adc data time per point(us)
static u32 ecg_PPi_us;
static u32 ecg_bpm;
// record num of Rn detected
static u32 ecg_Rn_detected;
// record num of Rn escaped
static u32 ecg_Rn_escaped;
static u32 ecg_Rn_continue_escaped;

// record Rv detected values
static u32 ecg_Rv_bpm;
static u32 ecg_Rv_R1_tick;
static u32 ecg_Rv_RRi_ms;
// record Rs and Rv interval(unit: ms)
// this value will store in EE before LPM
static u32 ecg_Rs_Rv_interval_ms;

// record ecg selected adc channel num
static u8 ecg_adcChNum;
// adc max/min value pair
static int32_t ecg_adcMaxValue, ecg_adcMinValue;
// adc peak value in 2.5s
static int32 ecg_adcPeakMaxValue, ecg_adcPeakMinValue;
// trim adc peak timeout tick
static u32 ecg_adcTrimPeakTick;
// trim RDET adc sample some times at first
// and then it is flag of the first data tick in buffer
static u32 ecg_adcStableTick;


/* private fuction define *****************************************/


/*
  brief:
    1. We got Rv values at first, and then got Rs values;
    2. So: R tick of Rv > R tick of Rs;
    3. RRi_msV = (Rs_RRi_ms + Rv_RRi_ms) / 2;
    4. Rsvi_ms = RRi_msV - (Rs_tick - Rv_tick) % RRi_msV;
*/
static void ecg_calculateRsRvInterval(void)
{
  u32 RRi_msV = ecg_RRi_ms;

  if(ecg_RRi_ms != ecg_Rv_RRi_ms)
    RRi_msV = (ecg_RRi_ms + ecg_Rv_RRi_ms) >> 1;

  ecg_Rs_Rv_interval_ms = RRi_msV - (ecg_R_tick - ecg_Rv_R1_tick) % RRi_msV;
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
static void ecg_addAdcDataToRbuf(u8 _data)
{
  // buffer is full?
  if(ecg_Rpeak_buf_num < ECG_RPEAK_BUF_SIZE){
    // add adc data to buf
    ecg_Rpeak_buf[ecg_Rpeak_buf_num++] = _data;

    // collected the last byte?
    if(ecg_Rpeak_buf_num == ECG_RPEAK_BUF_SIZE){
      // store current tick(buf last data tick)
      ecg_bufLastDataTick = HAL_GetTick();
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
static int32_t ecg_calculateSlope(u8 *_pbufindex, bool _useDefault)
{
  int32 ret = 0;
  int32 buf[8];
  int32 *p = buf;
  // pointer big value point
  u8 *p0 = _pbufindex - ECG_SLOPE_STEP;
  // pointer small value point
  u8 *p3 = p0 - ECG_SLOPE_STEP;
  int i;

  // check param
  if(ecg_Rpeak_buf + ECG_R2_MW_SIZE >= _pbufindex
    || _pbufindex + ECG_R2_MW_SIZE >= ecg_Rpeak_buf + ECG_RPEAK_BUF_SIZE){

    if(_useDefault)
      ret = ECG_SLOPE_DEFAULT;

    return ret;
  }

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
  ret = 0;
  for(i = 0; i < 8; i++){
    if(buf[i] > ret){
      ret = buf[i];
    }
  }

  return ret;
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
  static u8 prevdata = 0;
  static u32 tick = 0;

#ifdef LiuJH_DEBUG
  // For testing Rn detected ERROR
  if(ecg_Rpeak_buf_num >= ECG_RPEAK_BUF_SIZE)
    ecg_Rpeak_buf_num = 0;
  ecg_addAdcDataToRbuf(_data);
#endif

  // define R move window size for detecting R peak point
  detectmin = ecg_RR_interval - ECG_Rn_MW_HALF_SIZE;
  detectmax = ecg_RR_interval + ECG_Rn_MW_HALF_SIZE;

  // have enough data? need detecting?
  if(ecg_R_detected <= detectmin){
    // The first point of Rn move window, rocord it, dont need detect
    prevdata = _data;
    tick = HAL_GetTick();

    // dont need checking, skip it
    ecg_R_detected++;

    return;
  }

  // escape? continue next R peak point detect
  if(ecg_R_detected > detectmax){
    // record Rn detected and escaped num
    ecg_Rn_detected++;
    ecg_Rn_escaped++;
    ecg_Rn_continue_escaped++;

    // twice continue escaped? restart R1 waiting...
    if(ecg_Rn_continue_escaped >= 2){
      // redo R detect, restart up ecg detection
      ecg_startup();  // Rv need redo detect
//      ecg_status = ecg_startup_status;
    }else{
      // temp work, continue detecting next Rn peak point
      ecg_R_detected = ECG_Rn_MW_HALF_SIZE;
    }

    return;
  }

  /*
    NOW:
      1. We have two data in [RRi-3, RRi+4];
      2. Compare prev data and current data;
      3. if exist inflection, the prev data is Rn peak point;
  */

  // check the inflection and the point value
  if(_data < prevdata && prevdata > ecg_R_valueV){
    // update R value and R value V
    ecg_R_value = (ecg_R_value + prevdata) >> 1;
    ecg_R_valueV = ecg_R_value * ECG_R_VALUE_WEIGHT;
    // record current R peak point tick value for pulse
    ecg_R_tick = tick;

    // record detected num
    ecg_Rn_detected++;
    // clear escaped continued
    ecg_Rn_continue_escaped = 0;

    // Need trim current RRi or NOT???
    if(ecg_RR_interval != ecg_R_detected){
      ecg_RR_interval += ecg_R_detected;
      ecg_RR_interval >>= 1;
    }

    // for detecting next Rn+1 peak point(prevdata is Rn peak point)
    ecg_R_detected = 0;

    // toast ble this tick of R peak point detected
    ble_setPulseShowFlag();

    // trigger pulse sending
    if(ecg_Rn_detected >= ECG_RN_DETECTED_MIN_NUM
      && ecg_Rn_continue_escaped == 0)
      pulse_setPulsingFlag();

  }  else{
    // update prev data for next compare
    prevdata = _data;
    tick = HAL_GetTick();
  }

  // count for next R peak point detecting
  ecg_R_detected++;
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
  u32 tick = HAL_GetTick();

  // get RRi_ms and ecg_bpm
  ecg_RRi_ms = ecg_RR_interval * ecg_PPi_us / 1000;
  ecg_bpm = 60 * 1000 / ecg_RRi_ms;

  // for CCM
  if(ecg_adcChNum == ADC_CH_NUM_RV_RDET){
    // record R1_tick and RRi_ms of Rv
    ecg_Rv_R1_tick = ecg_R_tick;
    ecg_Rv_RRi_ms = ecg_RRi_ms;
    ecg_Rv_bpm = ecg_bpm;

    // restart up ecg seletct channel of ADC_CH_NUM_RS_RDET
    ecg_adcChNum = ADC_CH_NUM_RS_RDET;
    ecg_status = ecg_startup2_status;

    return;
  }else if(ecg_adcChNum == ADC_CH_NUM_RS_RDET){
    // check bpm is qual
    if(ecg_bpm != ecg_Rv_bpm){
      // restart detect R1, R2 of Rv RDET channel
      ecg_adcChNum = ADC_CH_NUM_RV_RDET;
      ecg_status = ecg_startup_status;

      return;
    }
  }

  /* NOW: we can calculate Rs_Rv delay time(unit: ms) */
  ecg_calculateRsRvInterval();

  /*
    NOW:
      1. we get RR_internal, RRi_ms and ecg_bpm;
      2. Base current tick, we can know next R peak point appear time;
  */
  // Base R1 tick and current tick, calculate R_detected value for next Rn peak point
  tick = HAL_GetTick() - ecg_R_tick;
  tick %= ecg_RRi_ms;
  ecg_R_detected = tick * 1000 / ecg_PPi_us;

#ifdef LiuJH_DEBUG
  /*
    For testing:
      1. Loop record data in buffer;
      2. if full, num set 0, and loop record data in buffer continue;
      3. for each Rn detected, check where is ERROR;
  */
  ecg_Rpeak_buf_num = 0;
#endif
  // detected Rn num and escaped Rn num init
  ecg_Rn_detected = ecg_Rn_escaped = ecg_Rn_continue_escaped = 0;

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
static void ecg_cbSmRnWaiting(u8 _data)
{
  // current point tick
  u32 tick = HAL_GetTick();
  // points num in buffer(add current data, so "+1")
  u32 pointnum = ecg_Rpeak_buf_num + 1;

  // add current data into buf
  ecg_addAdcDataToRbuf(_data);

  // calculate RRi_ms
  // if some data escape buffer
  if(ecg_Rpeak_buf_num >= ECG_RPEAK_BUF_SIZE){
    // Rpeak_buf is full, the last byte tick record
    tick = ecg_bufLastDataTick;
    // record point num in buffer
    pointnum = ECG_RPEAK_BUF_SIZE;
  }

  // get tick interval of all points
  tick -= ecg_adcStableTick;

  // calculate time(Unit: us) per point
  ecg_PPi_us = tick * 1000 / pointnum;

  // goto next status
  ecg_status = ecg_Rnwaiting2_status;
}

/*
  brief:
    1. NOW: we have five important value:
        index of R1 peak pint;
        ecg_R1Tick;
        EcgIsForward;
        ecg_R_slopeV;
    2. calculate slope of four bytes(before R2 peak point);
    3. if greate or equal ecg_R1slope_V, the R2 will appear;
    4. 
*/
static void ecg_smR2Detect(void)
{
  // pointer current point(will check this point)
  u8 *p = ecg_Rpeak_buf + ecg_R_detected;
  // pointer next point for comparing with *p
  u8 *pn = p + 1;
  int32_t slope;

#ifdef LiuJH_DEBUG
    /*
      NOTICE:
        1. exceed, deal here;
    */
    if(ecg_R_detected + ECG_R2_MW_SIZE + 1 > ECG_RPEAK_BUF_SIZE){
      // redo R detect, restart up ecg detection
      ecg_status = ecg_startup_status;
      return;
    }
#endif

  // Have byte for detecing?
  if(ecg_Rpeak_buf_num < ecg_R_detected + ECG_R2_MW_SIZE + 1)
    return;

  // this point is NOT suspected R peak point? flag this point is detected, and continue next detect
  if(*p < ecg_R_valueV) goto ecg_smR2Detect_end;

  /*
    NOW:
      1. *p is suspected R peak point;
      2. we will check other param for confirming this point;
  */

  // check this point is the max value point in next six point
  for(int i = 0; i < ECG_R2_MW_SIZE; i++){
    // current point is not max value point? continue next detect
    if(pn[i] > *p) goto ecg_smR2Detect_end;
  }

  // check slope
  slope = ecg_calculateSlope(p, false);
  if(slope < ecg_R_slopeV) goto ecg_smR2Detect_end;


  /*
    NOW:
      1. we found R2 point(*p);
      2. record important params for us;
  */

  // record ecg_RR_interval value
  ecg_RR_interval = p - ecg_R1_pindex;

#ifdef LiuJH_DEBUG
  // average R value
  ecg_R_value = (ecg_R_value + *p) >> 1;
  // update R value V
  ecg_R_valueV = ecg_R_value * ECG_R_VALUE_WEIGHT;
#endif

  // go into next status
  ecg_status = ecg_Rnwaiting_status;


ecg_smR2Detect_end:
  // loop next R1 detect or enter into next status
  ecg_R_detected++;
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
  int32_t slope;
  u32 num;

//  RwaveUseTick = HAL_GetTick();

  // we need ECG_R2_MW_SIZE data after peak point
  num = ecg_R1_pindex - ecg_Rpeak_buf;

#ifdef LiuJH_DEBUG
  /*
    NOTICE:
      1. Sometimes there may be ERROR(unknown reasons), deal here
  */
  if(num + ECG_R2_MW_SIZE >= ECG_RPEAK_BUF_SIZE){

    // redo R detect, restart up ecg detection
    ecg_status = ecg_startup_status;
    return;
  }

#endif

  // We have enough data for slope?
  if(ecg_Rpeak_buf_num < num + ECG_R2_MW_SIZE)
    return;

  // calculate R peak point slope
  slope = ecg_calculateSlope(ecg_R1_pindex, true);

  // calculate slope V value(0.80)
  ecg_R_slopeV = slope * ECG_SLOPE_WEIGHT;

  // get R1 value V for detect R2(NOTICE: MUST more than 0.83)
  ecg_R_valueV = ecg_R_value * ECG_R_VALUE_WEIGHT;

  /*
    NOW:
      1. R1 detect status process finished.
      2. Ignore points of the left of R1 peak point;
      3. init for next status(skip 180 ms from R1, and then start find R2);
  */
  ecg_R_detected = (ecg_R1_pindex - ecg_Rpeak_buf) + ECG_R2_SKIP_POINT_NUM;
  ecg_status = ecg_R2Detect_status;
}

/*
  brief:
    1. Value of _data is [0, 255];
    2. If _data is adcMaxValue, his value is 255;
    3. If _data is adcMinValue, his value is 0;
    4. store data in buffer untill num is 625(2.5 seconds);
*/
static void ecg_cbSmR1waiting(u8 _data)
{

  // waiting for adc sample data is correct
  if(HAL_GetTick() < ecg_adcStableTick) return;

  // record the first data tick in buffer
  if(ecg_Rpeak_buf_num == 0){
    ecg_adcStableTick = HAL_GetTick();
  }

  // is the peak point? record it
  // NOTICE: make sure ecg_R-value is the last max value
  if(_data >= ecg_R_value && ecg_Rpeak_buf_num > ECG_R2_MW_SIZE){
    ecg_R_value = _data;

    // record index in buf and its tick
    ecg_R1_pindex = ecg_Rpeak_buf + ecg_Rpeak_buf_num;
    ecg_R_tick = HAL_GetTick();
  }

  // add data to buf
  ecg_addAdcDataToRbuf(_data);

  // continue max value, skip it, and clear buf
  if(ecg_R_value == ECG_MAX_BYTE_VALUE
    && *(ecg_R1_pindex - 1) == ECG_MAX_BYTE_VALUE){
    ecg_R_value = 0;
    ecg_Rpeak_buf_num = 0;
  }

  // check data num(time line), update ecg status
  if(ecg_Rpeak_buf_num >= ECG_RPEAK_BUF_NEED_NUM
    // for slope
    && ecg_R1_pindex + ECG_R2_MW_SIZE < ecg_Rpeak_buf + ecg_Rpeak_buf_num)
    ecg_status = ecg_R1Detect_status;
}

/*
  brief:
    1. restart R1, R2 detection for Rs RDET channel;
    2. store Rv RDET channel vars detected;
*/
static void ecg_smStartup2(void)
{
  // init R peak point buf
  ecg_Rpeak_buf_num = 0;
  ecg_R_value = ECG_MAX_INIT_VALUE;
  ecg_R_tick = 0;
  ecg_bpm = 0xFF;

  // start R detection
  ecg_status = ecg_R1waiting_status;
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
static void ecg_cbStateMachine(u8 _adcByte)
{
  // Some status will real-time process with adcCB
  switch(ecg_status){
    case ecg_R1waiting_status:
      // collect enough datas for R1
      ecg_cbSmR1waiting(_adcByte);
      break;
    case ecg_R1Detect_status:
    case ecg_R2Detect_status:
      // in these status, collect data into buf only
      ecg_addAdcDataToRbuf(_adcByte);
      break;
    case ecg_Rnwaiting_status:
      ecg_cbSmRnWaiting(_adcByte);
      break;
    case ecg_RnDetect_status:
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
    case ecg_startup2_status:
      ecg_smStartup2();
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
    4. _data is the last adc sample byte at current tick.
    5. This function is always working at every ecg status;
    6. This function is working in adc callback interrupt,
       so please dont waste time.
    7. NOTE: return u8 data is test only;
*/
void ecg_adcConvCpltCB(void)
{
  i32 value;
  u8 byte;

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
  ecg_cbStateMachine(byte);
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

  *p++ = (u8)ecg_Rs_Rv_interval_ms;
  *p++ = (u8)(ecg_R_tick >> 24);
  *p++ = (u8)(ecg_R_tick >> 16);
  *p++ = (u8)(ecg_R_tick >> 8);
  *p++ = (u8)(ecg_R_tick);
  *p++ = (u8)(ecg_RRi_ms >> 8);
  *p++ = (u8)(ecg_RRi_ms);
  *p++ = (u8)(ecg_Rv_R1_tick >> 24);
  *p++ = (u8)(ecg_Rv_R1_tick >> 16);
  *p++ = (u8)(ecg_Rv_R1_tick >> 8);
  *p++ = (u8)(ecg_Rv_R1_tick);
  *p++ = (u8)(ecg_Rv_RRi_ms >> 8);
  *p++ = (u8)(ecg_Rv_RRi_ms);

  return true;
}

u8 ecg_getRsvi(void)
{
  return (u8)ecg_Rs_Rv_interval_ms;
}

u32 ecg_getRnTick(void)
{
  return ecg_R_tick;
}

/*
*/
u8 ecg_getBpm(void)
{
  return (u8)ecg_bpm;
}

u8 ecg_getAdcChNum(void)
{
  return ecg_adcChNum;
}

/*
  brief:
    1. 
*/
void ecg_startup(void)
{
  if(ecgIsWorking) return;

  // select channel number
#ifdef Insync_CCM
  /* for CCM */
  // default channel is RV_RDET(for getting RsRvi_ms value)
  // but use RS_RDET channel at last
  ecg_adcChNum = ADC_CH_NUM_RV_RDET;
//    ecg_adcChNum = ADC_CH_NUM_RV_IEGM;
#else
  /* for ICM */
  ecg_adcChNum = ADC_CH_NUM_RA_RDET;
#endif

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

  // init R peak point buf
  ecg_Rpeak_buf_num = 0;
  ecg_R_value = ECG_MAX_INIT_VALUE;
  ecg_R_valueV = 0;
  ecg_R1_pindex = ecg_Rpeak_buf;
  ecg_R_tick = ecg_bufLastDataTick = 0;
  ecg_R_slopeV = 0;
  ecg_RR_interval = ecg_PPi_us = ecg_RRi_ms = 0;
  ecg_bpm = 0xFF;
  ecg_Rn_detected = ecg_Rn_escaped = ecg_Rn_continue_escaped = 0;

  // init peak value
  ecg_adcMaxValue = ecg_adcPeakMaxValue = ADC_MIN_VALUE;
  ecg_adcMinValue = ecg_adcPeakMinValue = ADC_MAX_VALUE;

  /*
    NOTICE:
      adc channel num DONT init
  */

  ecg_Rv_bpm = 0;
  ecg_Rv_R1_tick = 0;
  ecg_Rv_RRi_ms = 0;
  ecg_Rs_Rv_interval_ms = 0;

  // status init
  ecg_status = ecg_inited_status;
}






