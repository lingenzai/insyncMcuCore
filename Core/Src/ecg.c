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

/* buf[0] is Rs buf; buf[1] is Rv buf...and so on */

// ADC sample data buffer for detecting R peak point
// NOTE: add ONE byte for u16 data
static u8 ecg_buf[ECG_ADC_CH_NUM][ECG_BUF_SIZE + 1];
// ADC sample data num in Rpeak_buffer
static u32 ecg_bufBnum[ECG_ADC_CH_NUM], ecg_bufWnum[ECG_ADC_CH_NUM];
// u16 and u8 data off when converting
static u32 ecg_bufBoff[ECG_ADC_CH_NUM], ecg_bufWoff[ECG_ADC_CH_NUM];

// offset of R1 peak point, max value point in RpeakBuf of 2 second
static u32 ecg_R1off[ECG_ADC_CH_NUM];
// record av of R1 and R peak point value
static u16 ecg_Rvalue[ECG_ADC_CH_NUM], ecg_RvalueV[ECG_ADC_CH_NUM];
// record process finished
static bool ecg_Rok[ECG_ADC_CH_NUM];
// record the first byte tick in buffer
static u32 ecg_bufFirstTick[ECG_ADC_CH_NUM];
// record the last byte tick in buffer
static u32 ecg_bufLastTick[ECG_ADC_CH_NUM];
/// record R wave average of slope
static int32 ecg_slopeV[ECG_ADC_CH_NUM];
// rercord detected point num from R1 to R2, and Rn
static u32 ecg_Rdetected[ECG_ADC_CH_NUM];
// RR interval point number
static u32 ecg_RRiPointNum[ECG_ADC_CH_NUM];
// record R point tick
static u32 ecg_Rtick[ECG_ADC_CH_NUM];

// adc data time per point(us) in same channel
static u32 ecg_RPPiTimeUs[ECG_ADC_CH_NUM];
// RR interval time(point num * sample time per point ECG_RR_TIME_PER_POINT)
static u32 ecg_RRiTimeMs[ECG_ADC_CH_NUM];
// bpm
static u32 ecg_bpm[ECG_ADC_CH_NUM];
// record interval(unit: ms) between Rs and Rv
static u32 ecg_RsviTimeMs;
// record Rn window min point num and max point num
static u32 ecg_RnWmin, ecg_RnWmax;


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


/* private fuction define *****************************************/


/*
  brief:
    1. unit is ms;
*/
static void ecg_calculateRsvi(void)
{
  u32 RRi_ms1, RRi_ms2;
  u32 RsTick = ecg_Rtick[ECG_RS_INDEX], RvTick = ecg_Rtick[ECG_RV_INDEX];

  if(RsTick > RvTick){
    // we must move Rv peak point
    RRi_ms1 = (RsTick - RvTick) % ecg_RRiTimeMs[ECG_RV_INDEX];
    RRi_ms2 = ecg_RRiTimeMs[ECG_RV_INDEX] - RRi_ms1; // is correct
  }else{
    // we must move Rs peak point
    RRi_ms1 = (RvTick - RsTick) % ecg_RRiTimeMs[ECG_RS_INDEX]; // is correct
    RRi_ms2 = ecg_RRiTimeMs[ECG_RS_INDEX] - RRi_ms1;
  }

  // record min value
  ecg_RsviTimeMs = (RRi_ms1 < RRi_ms2) ? (RRi_ms1) : (RRi_ms2);
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

/*
*/
static void ecg_initBuf(void)
{
  // Clear all vars about buf of Rs and Rv
  for(int i = 0; i < ECG_ADC_CH_NUM; i++){
    ecg_bufBnum[i] = 0;
    ecg_bufBoff[i] = 0;
    ecg_bufWnum[i] = 0;
    ecg_bufWoff[i] = 0;
  }

#ifndef LiuJH_DEBUG
	// test only
	memset(ecg_buf[ECG_RS_INDEX], 0, sizeof(ecg_buf[ECG_RS_INDEX]));
	memset(ecg_buf[ECG_RV_INDEX], 0, sizeof(ecg_buf[ECG_RV_INDEX]));


#endif
}

static void ecg_init2(void)
{
  // init R peak point buf
  ecg_initBuf();
  for(int i = 0; i < ECG_ADC_CH_NUM; i++){
    ecg_bufFirstTick[i] = ecg_bufLastTick[i] = 0;

    // init var about R1 and R2
    ecg_R1off[i] = 0;
    ecg_Rtick[i] = 0;
    ecg_Rvalue[i] = 0;
    ecg_RvalueV[i] = 0;
    ecg_slopeV[i] = 0;
    ecg_Rdetected[i] = 0;
    ecg_RRiPointNum[i] = 0;
    ecg_RRiTimeMs[i] = 0;
    ecg_RPPiTimeUs[i] = 0;
    ecg_Rok[i] = false;

    // init param for APP
    ecg_bpm[i] = 0;
    ecg_RsviTimeMs = 0;
  }

  // init var for Rn
  ecg_RnDetected = 0;
  ecg_RnEscaped = 0;
  ecg_RnEscapedAll = 0;
}

/*
  brief:
    1. if need convert, so update buf;
    2. if dont need convet, update bufBnum only;
    3. NOTE: bufBnum++ MUST be the last statment;
    4. 
*/
static u8 ecg_convAdcValueToByte(u8 _chIndex, u16 _adcValue, bool _needConv)
{
  i32 value = _adcValue;
  i32 sub1;
  i32 sub2;
  u8 byte = 0;

  if(_needConv){
    // convert u16 to u8
    if(value > ecg_adcMinValue && value < ecg_adcMaxValue){
      sub1 = value - ecg_adcMinValue;
      sub2 = ecg_adcMaxValue - ecg_adcMinValue;
      byte = (u8)(((sub1 << 8) - sub1) / sub2);
    }else if(value >= ecg_adcMaxValue){
      byte = ECG_MAX_BYTE_VALUE;
    }else{
      byte = ECG_MIN_BYTE_VALUE;
    }

    // store into buf
    ecg_buf[_chIndex][ecg_bufBnum[_chIndex]] = byte;
  }

  // At last: update bufBnum(sync with adc CB)
  (ecg_bufBnum[_chIndex])++;

  return byte;
}

/*
  brief:
    1. update max and min value;
    2. record peak value pair per 2.5s;
    3. 
*/
static void ecg_filterPeakValue(u16 _adcValue)
{
  i32 value = (i32)_adcValue;

  // update max and min data
  if(value > ecg_adcMaxValue)
    ecg_adcMaxValue = value;
  if(value < ecg_adcMinValue)
    ecg_adcMinValue = value;
  
  // record peak value interval 2.5s
  if(value > ecg_adcPeakMaxValue)
    ecg_adcPeakMaxValue = value;
  if(value < ecg_adcPeakMinValue)
    ecg_adcPeakMinValue = value;
}

/*
  brief:
    1. get u16 adc value from Rs and Rv buf;
    2. Prioritize obtaining data from Rs;
    3. if succeed, return true and put value into vars;
    4. NOTE: if secceed, update bufBoff, but NOT update Bnum;
    5. 
*/
static bool ecg_pollAdcValue(u8 *_pchIndex, u16 *_padcValue)
{
  bool ret = false;

  // exist new u16 data in Rs or Rv buf?
  if(ecg_bufWnum[ECG_RS_INDEX] > ecg_bufBnum[ECG_RS_INDEX]){
    // get u16 value from buf
    *_pchIndex = ECG_RS_INDEX;
    // pull MSB of u16
    *_padcValue = ecg_buf[ECG_RS_INDEX][ecg_bufBoff[ECG_RS_INDEX]++];
    // pull LSB of u16
    *_padcValue = (*_padcValue << 8) | ecg_buf[ECG_RS_INDEX][ecg_bufBoff[ECG_RS_INDEX]++];

    ret = true;
  }else if(ecg_bufWnum[ECG_RV_INDEX] > ecg_bufBnum[ECG_RV_INDEX]){
    // get u16 value from buf
    *_pchIndex = ECG_RV_INDEX;
    // pull MSB of u16
    *_padcValue = ecg_buf[ECG_RV_INDEX][ecg_bufBoff[ECG_RV_INDEX]++];
    // pull LSB of u16
    *_padcValue = (*_padcValue << 8) | ecg_buf[ECG_RV_INDEX][ecg_bufBoff[ECG_RV_INDEX]++];

    ret = true;
  }

  return ret;
}

/*
  brief:
    1. try do atom action: store ivalue into Rs or Rv buf;
    2. use big endian mode;
*/
static void ecg_cbStoreWordToBuf(u8 _chIndex, i32 _iValue)
{
  u16 data = _iValue;

  /* store Rs_RDET data into Rs or Rv buf */

  // update buf offset if all u16 data have been processed 
  if(ecg_bufBnum[_chIndex] == ecg_bufWnum[_chIndex]){
    ecg_bufWoff[_chIndex] = ecg_bufBnum[_chIndex];
	ecg_bufBoff[_chIndex] = ecg_bufWoff[_chIndex];
  }

  // if we have no enough space for storing data, return
  if(ecg_bufWoff[_chIndex] + 1 > ECG_BUF_SIZE) return;

  // store u16 data into Rs or Rv buf
  ecg_buf[_chIndex][ecg_bufWoff[_chIndex]++] = (u8)(data >> 8);
  ecg_buf[_chIndex][ecg_bufWoff[_chIndex]++] = (u8)data;
  ecg_bufWnum[_chIndex]++;

  // current value is the last data in buf? record its tick
  if(!(ecg_bufBnum[_chIndex] ^ (ECG_BUF_SIZE - 1))
    && !(ecg_bufWnum[_chIndex] ^ ECG_BUF_SIZE))
    ecg_bufLastTick[_chIndex] = HAL_GetTick();
}


/*
  brief:
    1. We have detected Rn peak point;
    2. calculate vars about Rn detect for sync of Loop and AdcCB;
*/
static void ecg_smRnDetected(void)
{
	u8 chIndex = ECG_RS_INDEX;
#ifdef LiuJH_DEBUG
#else
	u32 tick;
#endif

#ifdef LiuJH_DEBUG
	// calculate Rdetected through using point num
	/*
		if detected Rn, bufBnum - 2 is Rn off in buf;
		if escape, we assum the four is Rn(because of set is (min, max]);
	*/

	// is escape?
	if(ecg_RnEscaped){
		ecg_Rdetected[chIndex] = ECG_Rn_MW_HALF_SIZE;
	}else{
		/*
			1. off of R peak point in buf is: (ecg_bufBnum - 2);
			2. so the last byte(ecg_bufBnum - 1) is the detected point of next Rn;
			3. and dont convert data in buffer is alse detected points;
		*/
		ecg_Rdetected[chIndex] = 1 + (ecg_bufWnum[chIndex] - ecg_bufBnum[chIndex]);
	}
#else
	// calculate Rdetected through using tick
	tick = HAL_GetTick() - ecg_Rtick[chIndex];
	if(ecg_RRiTimeMs[chIndex])
		tick %= ecg_RRiTimeMs[chIndex];
	ecg_Rdetected[chIndex] = tick * 1000 / ecg_RPPiTimeUs[chIndex];
#endif

	// get Rn window min and max point num
	ecg_RnWmin = ecg_RRiPointNum[chIndex] - ECG_Rn_MW_HALF_SIZE;
	ecg_RnWmax = ecg_RRiPointNum[chIndex] + ECG_Rn_MW_HALF_SIZE;

#ifdef LiuJH_DEBUG
	/*
	  For testing:
		1. Loop record data in buffer;
		2. if full, num set 0, and loop record data in buffer continue;
		3. for each Rn detected, check where is ERROR;
	*/
	ecg_initBuf();
#endif

	// start detecting Rn using R move window, so go into next status
	ecg_status = ecg_RnDetect_status;
}

/*
  brief:
    1. Ignore Rv channel data;
    2. Only collect point from RnWmin to RnWmax into Rsbuf;
    3. step Rdetected for each data and ignore it;
    4. rocord tick each point into Rvbuf;
*/
static void ecg_cbSmRnDetect(u8 _chIndex, i32 _adcValue)
{
  u32 tick;

  // ignore Rv channel
  if(_chIndex) return;

  // record enough data?
  if(ecg_Rdetected[_chIndex] >= ecg_RnWmax){
  	return;
  }

  // is [RnWmin, RnWmax) data?
  if(ecg_Rdetected[_chIndex] < ecg_RnWmin){
    // ignore cur data, loop waiting next data
    ecg_Rdetected[_chIndex]++;
    return;
  }

  // store u16 data into buf;
  ecg_cbStoreWordToBuf(_chIndex, _adcValue);
  // store tick into Rv buf;
  tick = HAL_GetTick();
  ecg_cbStoreWordToBuf(ECG_RV_INDEX, (u16)(tick >> 16));
  ecg_cbStoreWordToBuf(ECG_RV_INDEX, (u16)tick);

  // for next loop
  ecg_Rdetected[_chIndex]++;
}

/*
  brief:
    1. have no data(bufWnum == 0), return;
    3. convert u16 adc data to byte;
    4. Check inflection in R window;
    5. if fixed Rn peak point, update this data to opposite and return;
*/
static void ecg_smRnDetect(void)
{
	u8 chIndex = ECG_RS_INDEX;
	u16 adcValue = 0;
	u32 aOff, bOff, cOff;
	u8 aPoint, bPoint, cPoint;
	u8 *ps = ecg_buf[ECG_RS_INDEX], *pv = ecg_buf[ECG_RV_INDEX];
	u32 offb = 0, tick1, tick2, tick3, tick4;
	bool found = false;

	// exist new u16 data in Rs buf? got it and convert byte and store into Rs buf
	if(ecg_bufWnum[chIndex] > ecg_bufBnum[chIndex]){
	// pull MSB of u16
	adcValue = ps[ecg_bufBoff[chIndex]++];
	// pull LSB of u16
	adcValue = (adcValue << 8) | ps[ecg_bufBoff[chIndex]++];

	// convert u16 to u8 and store into Rs buf
	ecg_convAdcValueToByte(chIndex, adcValue, true);
	}

	// we have enough byte to detect Rn?
	if(ecg_bufBnum[chIndex] < ECG_RN_DETECT_NUM) return;

	/*
	NOW:
	  1. We have ECG_RN_DETECT_NUM data at least: A B C D (E F);
	  2. R: is more than RnValueV and more than both prev and next data(cur byte);
	  3. if Rndetected num more than RnWmax, escape;
	  4. if B == C, B must bigger than D;
	*/

	// get the newest three data for compare
	aOff = ecg_bufBnum[chIndex] - ECG_RN_DETECT_NUM;
	bOff = ecg_bufBnum[chIndex] - ECG_RN_DETECT_NUM + 1;
	cOff = ecg_bufBnum[chIndex] - ECG_RN_DETECT_NUM + 2;
	aPoint = ps[aOff];
	bPoint = ps[bOff];
	cPoint = ps[cOff];

	// check point value
	if(bPoint > ecg_RvalueV[chIndex]){
		// check inflection
		if(bPoint > aPoint && bPoint > cPoint){
			found = true;
		}

		// two Rn peak point value???
		if(!found && aPoint == bPoint && bPoint > cPoint){
			// we have already more than four bytes?(check inflection again)
			if(aOff && bPoint > ps[aOff - 1]){
				found = true;
			}
		}

		if(found){
			// update R value and R value V
			ecg_Rvalue[chIndex] = (ecg_Rvalue[chIndex] + bPoint) >> 1;
			ecg_RvalueV[chIndex] = ecg_Rvalue[chIndex] * ECG_R_VALUE_WEIGHT;

			// get and record current R peak point tick value for pulse
			offb = bOff * sizeof(u32);
			tick1 = pv[offb++]; tick2 = pv[offb++]; tick3 = pv[offb++]; tick4 = pv[offb];
			ecg_Rtick[chIndex] = (tick1 << 24) | (tick2 << 16) | (tick3 << 8) | tick4;

			// record detected num
			ecg_RnDetected++;
			// clear escaped continued
			ecg_RnEscaped = 0;

			// Need trim current RRi or NOT???
			offb = ecg_RnWmin + bOff;
			if(ecg_RRiPointNum[chIndex] != offb){
				offb += ecg_RRiPointNum[chIndex];
				ecg_RRiPointNum[chIndex] = offb >> 1;
			}

#ifndef LiuJH_DEBUG
			// toast ble this tick of R peak point detected
			ble_setPulseShowFlag();
#endif

			// trigger pulse sending
			if(ecg_RnDetected > ECG_RN_DETECTED_MIN_NUM){
#ifndef LiuJH_DEBUG
				// test only
				static int i = 0;
				if(i++ == 15)
					i = 0;
#endif
				pulse_setEcgPulsingFlag();
			}
			// for next Rn detect, go into next status for sync buf vars
			ecg_status = ecg_RnDetected_status;
		}
	}

	/*
	1. bpoint is NOT Rn peak point;
	2. continue for waiting next byte;
	3. escape?
	*/

	// escape?
	if(!found && ecg_Rdetected[chIndex] >= ecg_RnWmax && ecg_bufBnum[chIndex] >= ecg_bufWnum[chIndex]){
		// record Rn detected and escaped num
		ecg_RnDetected++;
		ecg_RnEscaped++;
		ecg_RnEscapedAll++;

		// twice continue escaped? restart R1 waiting...
		if(ecg_RnEscaped >= ECG_ESCAPED_MAX_NUM){
		  // redo R detect, restart up ecg detection
		  ecg_status = ecg_startup_status;
		}else{
		  // for next Rn detect, go into next status for sync buf vars
		  ecg_status = ecg_RnDetected_status;
		}
	}
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
  u8 chIndex = ECG_RS_INDEX;

  /*
    NOW:
      1. we get RR_internal, RRi_ms and ecg_bpm;
      2. Base current tick, we can know next R peak point appear time;
      3. we only detect Rs-RDET;
  */

	// get Rn window min and max point num
	ecg_RnWmin = ecg_RRiPointNum[chIndex] - ECG_Rn_MW_HALF_SIZE;
	ecg_RnWmax = ecg_RRiPointNum[chIndex] + ECG_Rn_MW_HALF_SIZE;

#ifdef LiuJH_DEBUG
	/*
	  For testing:
		1. Loop record data in buffer;
		2. if full, num set 0, and loop record data in buffer continue;
		3. for each Rn detected, check where is ERROR;
	*/
	ecg_initBuf();
#endif

#ifndef LiuJH_OVBC
	// test only
	pulse_bleConfigPulseOn(true);
#endif

  // calculate R2 tick
  ecg_Rtick[chIndex] = ecg_bufFirstTick[chIndex] + ((ecg_Rdetected[chIndex] - 1) * ecg_RPPiTimeUs[chIndex]) / 1000;

  // calculate Rdetected
  tick = HAL_GetTick() - ecg_Rtick[chIndex];
  if(ecg_RRiTimeMs[chIndex])
    tick %= ecg_RRiTimeMs[chIndex];
  ecg_Rdetected[chIndex] = (tick * 1000) / ecg_RPPiTimeUs[chIndex];

  // start detecting Rn using R move window, so go into next status
  ecg_status = ecg_RnDetect_status;
}

/*
  brief:
    1. get current tick or buf last data tick;
    2. ONLY once time for tick and store word to buf of Rs and Rv;
    3. In this status, we need data num, but need NOT data vlue;
    4. 
*/
static void ecg_cbSmRnWaiting(u8 _chIndex, i32 _adcValue)
{
  // we have buf last data tick or current tick
  if(ecg_bufLastTick[_chIndex] || ecg_Rtick[_chIndex]) return;

  // ONLY update data num, dont store data into buf
  ecg_bufWnum[_chIndex]++;
//  ecg_cbStoreWordToBuf(_chIndex, _adcValue);

  // get current data tick of bufWnum in buf
  ecg_Rtick[_chIndex] = HAL_GetTick();
}

/*
  brief:
    1. Now: we got RR_internal point number;
    2. We fixed RRi value, skip all missed R peak point in
      loop buffer, and collect new data to loop buffer for
      fresh Rn peak point for deal it;
    3. Got PPi_us, RRi_ms, bpm KeyValues;
*/
static void ecg_smRnWaiting(void)
{
  u32 period = 0, num;
  u8 chIndex = ECG_RS_INDEX;
//  u16 adcValue;

  // Both of Rs and Rv channel finished? go into next status
  if(ecg_Rok[ECG_RS_INDEX] && ecg_Rok[ECG_RV_INDEX]){
  	u32 n1 = ecg_RRiPointNum[ECG_RS_INDEX], n2 = ecg_RRiPointNum[ECG_RV_INDEX];

    // clear static var for next detect
    ecg_Rok[ECG_RS_INDEX] = ecg_Rok[ECG_RV_INDEX] = false;

    // check bpm of Rs and Rv is equal?
//    if(ecg_bpm[ECG_RS_INDEX] == ecg_bpm[ECG_RV_INDEX]){
	// check RRiPointNum
	if(n1 == n2 || n1 == n2 + 1 || n1 + 1 == n2){
		/* NOW: we can calculate Rs_Rv delay time(unit: ms) */
		ecg_calculateRsvi();

		// got into next status for Rn detection
		ecg_status = ecg_Rnwaiting2_status;
    }else{
		// restart detect both of Rs and Rv channel
		ecg_status = ecg_startup_status;
    }

    return;
  }

  /*
  	NOW:
  		1. MUST one channel is NOT OK;
  		2. check Rs channel at first;
  */

#ifndef LiuJH_DEBUG
	/* need NOT update data */

  // pull u16 adc value from Rs and Rv buf and convert to byte and store into buf again
  if(ecg_pollAdcValue(&chIndex, &adcValue)){
    /* ignore channel num, only compare */
    ecg_filterPeakValue(adcValue);

  // convert u16 to u8 and store into Rs buf
    ecg_convAdcValueToByte(chIndex, adcValue, true);
  }
#endif

  // process Rs at first
  if(ecg_Rok[chIndex])
	  // toggle channel num, check next channel
	  chIndex ^= ECG_RV_INDEX;

  // waiting end tick about Rs or Rv channel
  if(ecg_bufLastTick[chIndex] || ecg_Rtick[chIndex]){
    /* calculate Rs channel keyvalues */

    // check buf last tick at first
    if(ecg_bufLastTick[chIndex]){
      period = ecg_bufLastTick[chIndex] - ecg_bufFirstTick[chIndex];
    }else{
      period = ecg_Rtick[chIndex] - ecg_bufFirstTick[chIndex];
    }
    // get point num of period time(unit: ms)
    num = ecg_bufWnum[chIndex];

    // calculate time(Unit: us) per point interrupt
    ecg_RPPiTimeUs[chIndex] = (period * 1000 ) / (num - 1);
    // get RRi_ms and ecg_bpm for Rs
    ecg_RRiTimeMs[chIndex] = (ecg_RRiPointNum[chIndex] * ecg_RPPiTimeUs[chIndex]) / 1000;
    if(ecg_RRiTimeMs[chIndex])
      ecg_bpm[chIndex] = (60 * 1000) / ecg_RRiTimeMs[chIndex];

    // update finished flag
    ecg_Rok[chIndex] = true;
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
    4. NOTE: If null loop, Rdetected is NOT add 1;
    5. 
*/
static void ecg_smR2Detect(void)
{
  u8 chIndex = ECG_RS_INDEX;
  u16 adcValue;
  u8 *p, *pn;
  int i;
  int32_t slope;

  // exceed? redo again
  if(ecg_Rdetected[ECG_RS_INDEX] + ECG_R2_MW_SIZE + 1 > ECG_BUF_SIZE
    || ecg_Rdetected[ECG_RV_INDEX] + ECG_R2_MW_SIZE + 1 > ECG_BUF_SIZE){
    // redo R detect, restart up ecg detection
    ecg_status = ecg_startup_status;
    return;
  }

  // Both of Rs and Rv channel finished? go into next status
  if(ecg_Rok[ECG_RS_INDEX] && ecg_Rok[ECG_RV_INDEX]){
    // clear static var for next detect
    ecg_Rok[ECG_RS_INDEX] = ecg_Rok[ECG_RV_INDEX] = false;

    // got into next status for R1 detecting
    ecg_status = ecg_Rnwaiting_status;
    return;
  }

  // pull u16 adc value from Rs and Rv buf and convert to byte and store into buf again
  if(ecg_pollAdcValue(&chIndex, &adcValue)){
    /* ignore channel num, only compare */
    ecg_filterPeakValue(adcValue);

  // convert u16 to u8 and store into Rs buf
    ecg_convAdcValueToByte(chIndex, adcValue, true);
  }

  // current channel has finished?
  if(ecg_Rok[chIndex])
//	  return;
  	// toggle channel num
  	chIndex ^= ECG_RV_INDEX;

  // We have enough bytes for detecting in current channel byte buf?
  if(ecg_bufBnum[chIndex] > ecg_Rdetected[chIndex] + ECG_R2_MW_SIZE){
    // pointer current point(will check this point)
    p = ecg_buf[chIndex] + ecg_Rdetected[chIndex];
    // pointer next point for comparing with *p
    pn = p + 1;

    // this point is suspected R peak point?
    if(*p >= ecg_RvalueV[chIndex]){
      /*
        NOW:
          1. *p is suspected R peak point;
          2. we will check other param for confirming this point;
      */

      // check this point is the max value point in next six point
      for(i = 0; i < ECG_R2_MW_SIZE; i++){
        // current point is not max value point? continue next detect
        if(pn[i] > *p) break;
      }

      // if *p is the max value, then check slope
      if(i == ECG_R2_MW_SIZE){
        // get slope
        slope = ecg_calculateSlope(p);
        // check it
        if(slope > ecg_slopeV[chIndex]){
          /*
            NOW:
              1. we found R2 point(*p, ie: ecg_Rdetected is offset in buf);
              2. record important params for us;
          */

          // record RR interval point number
          ecg_RRiPointNum[chIndex] = ecg_Rdetected[chIndex] - ecg_R1off[chIndex];

#ifdef LiuJH_DEBUG
          // average R value
          ecg_Rvalue[chIndex] = (ecg_Rvalue[chIndex] + *p) >> 1;
          // update R value V
          ecg_RvalueV[chIndex] = ecg_Rvalue[chIndex] * ECG_R_VALUE_WEIGHT;
#endif

          // flag cur channel detecting finished
          ecg_Rok[chIndex] = true;
        }
      }

    }

	// current point has bee detected, loop next point
	(ecg_Rdetected[chIndex])++;
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
  u8 chIndex = ECG_RS_INDEX;
  u16 adcValue;

  // pull u16 adc value from Rs and Rv buf and convert to byte and store into buf again
  if(ecg_pollAdcValue(&chIndex, &adcValue)){
    /* ignore channel num, only compare */
    ecg_filterPeakValue(adcValue);

  // convert u16 to u8 and store into Rs buf
    ecg_convAdcValueToByte(chIndex, adcValue, true);
  }

  // calculate R peak point slope and slope V value(0.80?)
  for(int i = 0; i < ECG_ADC_CH_NUM; i++){
    ecg_slopeV[i] = ecg_calculateSlope(ecg_buf[i] + ecg_R1off[i]) * ECG_SLOPE_WEIGHT;
  }

  /* NOW: redo R1 detect if the slopeV < ECG_SLOPEV_DEFAULT */
  if(ecg_slopeV[ECG_RS_INDEX] < ECG_SLOPEV_DEFAULT
    || ecg_slopeV[ECG_RV_INDEX] < ECG_SLOPEV_DEFAULT){
    // redo R detect, restart up ecg detection
    ecg_status = ecg_startup_status;
    return;
  }

  // get R1 value V for detect R2(NOTICE: MUST more than 0.83?)
  for(int i = 0; i < ECG_ADC_CH_NUM; i++){
    ecg_RvalueV[i] = ecg_Rvalue[i] * ECG_R_VALUE_WEIGHT;
  }

  /*
    NOW:
      1. R1 detect status process finished.
      2. Ignore points of the left of R1 peak point;
      3. init for next status(skip 180 ms from R1, and then start find R2);
  */
  for(int i = 0; i < ECG_ADC_CH_NUM; i++){
    ecg_Rdetected[i] = ecg_R1off[i] + ECG_R2_SKIP_POINT_NUM;
  }

  // we can detect R2
  ecg_status = ecg_R2Detect_status;
}

/*
*/
static void ecg_cbSmR1Waiting(u8 _chIndex, i32 _adcValue)
{
  // store u16 data into buf;
  ecg_cbStoreWordToBuf(_chIndex, _adcValue);

  // record the tick of the first value in buf
  if(!(ecg_bufWnum[_chIndex] ^ 1))
    ecg_bufFirstTick[_chIndex] = HAL_GetTick();
}

/*
  brief:
    1. pull u16 adc data, filter peak value, convert to u8 and store into buf;
    2. 
*/
static void ecg_smR1Waiting(void)
{
  u8 chIndex = ECG_RS_INDEX;
  u16 adcValue;
  u8 byte;

  // Both of Rs and Rv channel finished? go into next status
  if(ecg_Rok[ECG_RS_INDEX] && ecg_Rok[ECG_RV_INDEX]){
    // clear static var for next detect
    ecg_Rok[ECG_RS_INDEX] = ecg_Rok[ECG_RV_INDEX] = false;

    // got into next status for R1 detecting
    ecg_status = ecg_R1Detect_status;
    return;
  }

  // pull u16 adc value from Rs and Rv buf and convert to byte and store into buf again
  if(ecg_pollAdcValue(&chIndex, &adcValue)){
    /* ignore channel num, only compare */
    ecg_filterPeakValue(adcValue);

  // convert u16 to u8 and store into Rs buf
    byte = ecg_convAdcValueToByte(chIndex, adcValue, true);
  }

  // current channel has finished?
  if(ecg_Rok[chIndex]) return;

  /*
    1. ignore some data in the starting of buf(for slope);
    2. is the peak point? record it;
    3. NOTICE: make sure ecg_R_value is the last max value;
  */

  // have enough data for check?
  if(ecg_bufBnum[chIndex] <= ECG_R2_MW_SIZE) return;

  // check and record max value(for R1 peak point) after some data(for slope)
  // we want max value at last
  if(byte >= ecg_Rvalue[chIndex]){
    // record data value
    ecg_Rvalue[chIndex] = byte;
    // record data offset in buf
    ecg_R1off[chIndex] = ecg_bufBnum[chIndex] - 1;
  }

  // exceed buffer?
  if(ecg_bufBnum[chIndex] > ECG_BUF_R1_NEED_NUM
     // check the wave is stable or not(Continuous of max value)
     && ecg_Rvalue[chIndex] == ECG_MAX_BYTE_VALUE
     && ecg_buf[chIndex][ecg_bufBnum[chIndex] - 1] == ECG_MAX_BYTE_VALUE){

    // restart collect data
    ecg_status = ecg_startup_status;
  }else{
    // collect data finished? update finished flag in current channel
    if(ecg_bufBnum[chIndex] >= ECG_BUF_R1_NEED_NUM
      // for slope data num
      && ecg_bufBnum[chIndex] > ecg_R1off[chIndex] + ECG_R2_MW_SIZE){
      // update finished flag
      ecg_Rok[chIndex] = true;
    }
  }
}

/*
  brief:
    1. only synchronous Loop stateMachine and ADC callback;
    2. update all vars of buf in Loop stateMachine, and AdcCB do nothing;
    3. go into next status;
*/
static void ecg_smSync1(void)
{
  ecg_initBuf();

  // go into R1Waiting status for R1 detection
  ecg_status = ecg_R1waiting_status;
}

/*
  brief:
    1. get u16 adc value from Rs and Rv buf;
    2. compare and update cur max/min and peak max/min value;
    3. time period, go into sync status for reinit all vars of buf for R1Waiting status;
    4. 
*/
static void ecg_smStableWaiting(void)
{
  u8 chIndex;
  u16 adcValue;

  // pull u16 adc value from Rs and Rv buf
  if(ecg_pollAdcValue(&chIndex, &adcValue)){
    /* ignore channel num, only compare */
    ecg_filterPeakValue(adcValue);

    // We dont need convert u16 to u8, BUT need update bufBnum;
    ecg_convAdcValueToByte(chIndex, adcValue, false);
  }

  // time is over? YES: go into next status.
  if(HAL_GetTick() > ecg_adcStableTick){
    ecg_status = ecg_sync1_status;
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

  // start collect ADC sample u16 data into buffer
  ecg_status = ecg_stableWaiting_status;
}

/*
  brief:
    1. Running in adc CB, MUST process quickly;
    2. store two bytes into buf of Rs or Rv if no overflow;
    3. if byte num == word num, so update offset of store buf;
    4. _chIndex: 0, is Rs_RDET; 1, is Rv_RDET;
    5. 
*/
static void ecg_cbStateMachine(u8 _chIndex, i32 _adcValue)
{
  switch(ecg_status){
    case ecg_stableWaiting_status:
    case ecg_R1Detect_status:
    case ecg_R2Detect_status:
      // store u16 data into buf;
      ecg_cbStoreWordToBuf(_chIndex, _adcValue);
      break;
    case ecg_R1waiting_status:
      ecg_cbSmR1Waiting(_chIndex, _adcValue);
      break;
    case ecg_Rnwaiting_status:
      ecg_cbSmRnWaiting(_chIndex, _adcValue);
      break;
    case ecg_RnDetect_status:
      ecg_cbSmRnDetect(_chIndex, _adcValue);
      break;


    default:
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
    case ecg_stableWaiting_status:
      ecg_smStableWaiting();
      break;
    case ecg_sync1_status:
      ecg_smSync1();
      break;
    case ecg_R1waiting_status:
      ecg_smR1Waiting();
      break;
    case ecg_R1Detect_status:
      ecg_smR1Detect();
      break;
    case ecg_R2Detect_status:
      ecg_smR2Detect();
      break;
    case ecg_Rnwaiting_status:
      ecg_smRnWaiting();
      break;
    case ecg_Rnwaiting2_status:
      ecg_smRnWaiting2();
      break;
    case ecg_RnDetect_status:
      ecg_smRnDetect();
      break;
    case ecg_RnDetected_status:
      ecg_smRnDetected();
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
//  u8 chIndex = ECG_RS_INDEX;
  i32 adcValue;

  // it is not channels we want
  if((_curCh ^ ADC_CH_NUM_RS_RDET) && (_curCh ^ ADC_CH_NUM_RV_RDET))
    return;

  // get vaule
  adcValue = (int32_t)(HAL_ADC_GetValue(&hadc) & 0x0FFF);

/*
  // get channel num
  if(_curCh ^ ADC_CH_NUM_RS_RDET)
    chIndex = ECG_RV_INDEX;
*/

  // start ecg real-time state machine process
  ecg_cbStateMachine(_curCh >> 2, adcValue);
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

  *p++ = (u8)ecg_RsviTimeMs;
  *p++ = (u8)(ecg_Rtick[ECG_RS_INDEX] >> 24);
  *p++ = (u8)(ecg_Rtick[ECG_RS_INDEX] >> 16);
  *p++ = (u8)(ecg_Rtick[ECG_RS_INDEX] >> 8);
  *p++ = (u8)(ecg_Rtick[ECG_RS_INDEX]);
  *p++ = (u8)(ecg_RRiTimeMs[ECG_RS_INDEX] >> 8);
  *p++ = (u8)(ecg_RRiTimeMs[ECG_RS_INDEX]);
  *p++ = (u8)(ecg_Rtick[ECG_RV_INDEX] >> 24);
  *p++ = (u8)(ecg_Rtick[ECG_RV_INDEX] >> 16);
  *p++ = (u8)(ecg_Rtick[ECG_RV_INDEX] >> 8);
  *p++ = (u8)(ecg_Rtick[ECG_RV_INDEX]);
  *p++ = (u8)(ecg_RRiTimeMs[ECG_RS_INDEX] >> 8);
  *p++ = (u8)(ecg_RRiTimeMs[ECG_RS_INDEX]);

  return true;
}

u8 ecg_getRsvi(void)
{
  return (u8)ecg_RsviTimeMs;
}

/*
  brief:
    1. return current Rn peak point tick vlaue;
*/
u32 ecg_getRnTick(void)
{
  return ecg_Rtick[ECG_RS_INDEX];
}

/*
*/
u8 ecg_getBpm(void)
{
  return (u8)ecg_bpm[ECG_RS_INDEX];
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


