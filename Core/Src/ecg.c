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
// record max and min value in half of ecg buffer
static u8 ecg_max_value, ecg_min_value;
// Index of R1 peak point, max point and min point in RpeakBuf of 2.5 second
static u8 *ecg_R1_pindex, *ecg_R1max_pindex, *ecg_R1min_pindex;
// record R, max and min point tick
static u32 ecg_R_tick, ecg_R1max_tick, ecg_R1min_tick;
// record the last byte tick in buffer
static u32 ecg_bufLastDataTick;
// record R wave is upward or downward
static bool ecg_R_isUpward;
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
// record av of R1 and R2 peak point value
static u8 ecg_R_value;
static u8 ecg_R1_valueV;
// record num of Rn detected
static u32 ecg_Rn_detected;
// record num of Rn escaped
static u32 ecg_Rn_escaped;
static u32 ecg_Rn_continue_escaped;

#ifdef Insync_CCM
static u32 ecg_Rv_bpm;
static u32 ecg_Rv_R1_tick;
static u32 ecg_Rv_RRi_ms;
// record Rs and Rv interval(unit: ms)
// this value will store in EE before LPM
static u32 ecg_Rs_Rv_interval_ms;
#endif

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
    // update at1data value for test
    _data = *pat1Data++;

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
    1. We only caculate four points right arg, because some points
      is 255 at left of peak point;
    2. We have enough bytes for this;
    3. NOTE: if ECG R wave is downward, return value is negative;
*/
static int32_t ecg_getPeakPointSlope(u8 *_pbufindex)
{
  int32_t s = 0;
  u8 *p1, *p2;

  if(_pbufindex){
    p1 = _pbufindex + 1;
    p2 = _pbufindex + ECG_R2_MW_SIZE;
    s = (*p1 - *p2) / (ECG_R2_MW_SIZE - 1);
  }

  return s;
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
      ecg_status = ecg_startup_status;
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

  // check the inflection
  if((ecg_R_isUpward && _data < prevdata)
    || (!ecg_R_isUpward && _data > prevdata)){
    /*
      The inflection is apear, _data is the first point at inflection edge
    */
    // prevdata is the Rn peak point???
    // use 80% of R value check it
    if(prevdata >= (ecg_R_value << 2) / 5){
      // update R value
      ecg_R_value = (ecg_R_value + prevdata) >> 1;
      // record detected num
      ecg_Rn_detected++;
      ecg_Rn_continue_escaped = 0;
      // record current R peak point tick value
      ecg_R_tick = tick;

      // Need trim current RRi or NOT???
      if(ecg_RR_interval != ecg_R_detected){
        ecg_RR_interval += ecg_R_detected;
        ecg_RR_interval >>= 1;
      }

      // for detecting next Rn+1 peak point(prevdata is Rn peak point)
      ecg_R_detected = 0;

      // toast ble this tick of R peak point detected
      ble_setPulseShowFlag();

#ifdef LiuJH_DEBUG
      if(ecg_Rn_detected >= ECG_RN_DETECTED_MIN_NUM
        && ecg_Rn_continue_escaped == 0)
        // trigger pulse sending
        pulse_setPulsingFlag();

#endif
    }
  }
  else{
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
  // points num in buffer
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
  u8 *p = ecg_Rpeak_buf + ecg_R_detected;
  int32_t slope;
  bool isfixedR2 = false;

  /*
    1. calculate slope per ECG_R2_MW_SIZE points, detecte R2, step is two points.
    2. if find clue, start compare each byte forward, and make sure R2 point.
  */

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

  // find suspected R peak point
  if(*p >= ecg_R1_valueV){
    /* current point is R2 point? check it */

    // 1. find inflection
    if(ecg_R_isUpward){
      if(*(p + 1) < *p)
        isfixedR2 = true;
    }else{
      if(*(p + 1) > *p)
        isfixedR2 = true;
    }

    // 2. check slope
    if(isfixedR2){
      // get slope of ECG_R2_MW_SIZE points after R2'
      slope = ecg_getPeakPointSlope(p);
      if(!ecg_R_isUpward)
        slope = -slope;

      // slope MUST NOT less than ecg_R_slopeV
      if(slope >= ecg_R_slopeV){
        /*
          NOW:
            got it! ecg_Rpeak_buf[ecg_R_detected] is R2 peak point;
        */

        // record ecg_RR_interval value
        ecg_RR_interval = p - ecg_R1_pindex;

        // record R value
        ecg_R_value = (ecg_R_value + *p) >> 1;

        // go into next status
        ecg_status = ecg_Rnwaiting_status;
      }
    }
  }

  // loop next R1 detect or enter into next status
  ecg_R_detected++;
}

/*
  brief:
    1. The ADC sample callback function is Storing adc u8 value in RpeakBuf
      and record current max and min value index in pRpPointmax and pRpPointmin;
    2. Get average of slope of R1, and judge ECG is upward or downward;
    3. Get R1 index and tick;
    4. Update status to R2;
*/
static void ecg_smR1Detect(void)
{
  int32_t slope, slope1, slope2;
  u32 num1, num2;

//  RwaveUseTick = HAL_GetTick();

  // we need ECG_R2_MW_SIZE data after peak point
  num1 = ecg_R1max_pindex - ecg_Rpeak_buf;
  num2 = ecg_R1min_pindex - ecg_Rpeak_buf;

#ifdef LiuJH_DEBUG
  /*
    NOTICE:
      1. Sometimes there may be ERROR(unknown reasons), deal here
  */
  if(num1 + ECG_R2_MW_SIZE >= ECG_RPEAK_BUF_SIZE
    || num2 + ECG_R2_MW_SIZE >= ECG_RPEAK_BUF_SIZE){

    // redo R detect, restart up ecg detection
    ecg_status = ecg_startup_status;
    return;
  }

#endif

  // We have enough data for slope?
  if(ecg_Rpeak_buf_num < num1 + ECG_R2_MW_SIZE
    || ecg_Rpeak_buf_num < num2 + ECG_R2_MW_SIZE)
    return;

  // calculate R1 slope
  slope1 = ecg_getPeakPointSlope(ecg_R1max_pindex);
  slope2 = -ecg_getPeakPointSlope(ecg_R1min_pindex);

  // judge R wave direction and peak index, peak tick
  if(slope1 < slope2){
    ecg_R1_pindex = ecg_R1min_pindex;
    ecg_R_tick = ecg_R1min_tick;
    ecg_R_isUpward = false;

    slope = slope2;
  }else{
    ecg_R1_pindex = ecg_R1max_pindex;
    ecg_R_tick = ecg_R1max_tick;
    ecg_R_isUpward = true;

    slope = slope1;
  }

  // record R1 value
  ecg_R_value = *ecg_R1_pindex;
  // get R1 value V for detect R2(NOTICE: MUST more than 0.83)
  // 435/512 = 0.85;  (435 = 2^8 + 2^7 + 2^5 + 2^4 + 2 + 1)
  ecg_R1_valueV = ((ecg_R_value << 8) + (ecg_R_value << 7)
    + (ecg_R_value << 5) + (ecg_R_value << 4) + (ecg_R_value << 1)
    + ecg_R_value) >> 9;

  // calculate slope V value
//   ecg_R_slopeV = (slope * ECG_R1_SLOPEV_MUL1) / ECG_R1_SLOPEV_MUL2;
  // 435/512 = 0.85;  (435 = 2^8 + 2^7 + 2^5 + 2^4 + 2 + 1)
  ecg_R_slopeV = ((slope << 8) + (slope << 7) + (slope << 5) + (slope << 4) + (slope << 1) + slope) >> 9;
  // 461 / 512 = 0.9; (461 = 2^8 + 2^7 + 2^6 + 2^3 + 2^2 + 1)
//  ecg_R_slopeV = ((slope << 8) + (slope << 7) + (slope << 6) + (slope << 3) + (slope << 2) + slope) >> 9;

  /*NOW: R1 detect status process finished.*/
  // Ignore points of the left of R1 peak point
  // init for next status(skip 180 ms from R1, and then start find R2)
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
    // Test: init at1 data pointer
    pat1Data = at1dataBuf;
  }

  // update at1data value for test
  _data = *pat1Data;

  // is the peak point? record it
  if(_data >= ecg_max_value){
    ecg_max_value = _data;

    // record index in buf and its tick
    ecg_R1max_pindex = &ecg_Rpeak_buf[ecg_Rpeak_buf_num];
    ecg_R1max_tick = HAL_GetTick();
  }
  // is the peak point? record it(NOTICE: about min value, at least four points before min point for slopeV)
  if(_data <= ecg_min_value && ecg_Rpeak_buf_num >= ECG_R2_MW_SIZE){
    ecg_min_value = _data;

    // record index in buf and its tick
    ecg_R1min_pindex = &ecg_Rpeak_buf[ecg_Rpeak_buf_num];
    ecg_R1min_tick = HAL_GetTick();
  }

  // add data into loop buf
  ecg_addAdcDataToRbuf(_data);

  // the last is 255? continue store data for slopeV
  if(_data == 255) return;

  // check data num(time line), update ecg status
#ifndef LiuJH_DEBUG
  // only test(copy data)
  if(ecg_Rpeak_buf_num >= ECG_RPEAK_BUF_SIZE)
    ecg_status = ecg_R1Detect_status;
#else
  if(ecg_Rpeak_buf_num >= ECG_RPEAK_BUF_NEED_NUM)
    ecg_status = ecg_R1Detect_status;
#endif
}

/*
  brief:
    1. restart R1, R2 detection for Rs RDET channel;
    2. store Rv RDET channel vars detected;
*/
static void ecg_smStartup2(void)
{
  u32 bpm = ecg_Rv_bpm;
  u32 tick = ecg_Rv_R1_tick;
  u32 rri = ecg_Rv_RRi_ms;

  // ecg start working flag
  ecgIsWorking = true;

  // reinit
  ecg_init();

  // restore these vars
  ecg_Rv_bpm = bpm;
  ecg_Rv_R1_tick = tick;
  ecg_Rv_RRi_ms = rri;

  // trim adc tick startup
  ecg_adcTrimPeakTick = HAL_GetTick() + ADC_TRIM_PEAK_TIMEOUT;

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
  // ecg start working flag
  ecgIsWorking = true;

  // reinit
  ecg_init();

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
  ecg_max_value = ECG_MAX_INIT_VALUE;
  ecg_min_value = ECG_MIN_INIT_VALUE;
  ecg_R1_pindex = ecg_R1max_pindex = ecg_R1min_pindex = &ecg_Rpeak_buf[0];
  ecg_R_tick = ecg_R1max_tick = ecg_R1min_tick = ecg_bufLastDataTick = 0;
  ecg_R_isUpward = true;
  ecg_R_slopeV = 0;
  ecg_RR_interval = ecg_PPi_us = ecg_RRi_ms = 0;
  ecg_bpm = 0xFF;
  ecg_R_value = 0;
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






