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
// half point of (bpm - 5) and (bpm + 5)
static u32 ecg_RnHalfWeight;


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
// record redo because too little bpm
static bool ecg_detectAgain = false;
// record ADC no signal weight value and max value gain
static ecg_AdcWeightGain_typeDef ecg_AdcWeightGain;

#ifdef LiuJH_ECG
// record adc data of Rs-RDET and Rv-RDET channel
static u8 ecg_adcBuf[2][1380] = {
  { // Rs-RDET: 32 * 43 + 4 = 1380 data
    138,137,140,139,164,140,179,175,175,178,164,170,153,158,144,148,137,140,131,133,128,129,127,128,126,126,126,126,125,125,124,124,
    124,123,129,126,136,134,133,134,182,145,189,212,158,172, 88,145,105, 88,124,118,127,126,126,127,121,124,115,118,110,110,112,111,
    113,112,114,113,115,115,116,116,117,116,117,117,118,117,118,118,118,118,118,118,117,118,116,117,114,115,111,113,104,108, 92, 98,
    128,123,134,132,138,136,139,139,160,140,181,174,178,181,167,173,155,161,145,150,138,141,132,134,128,129,127,127,125,125,125,125,
    125,125,125,125,124,124,124,124,124,124,126,125,135,130,135,137,149,135,209,187,170,187,127,157, 86, 83,118,104,127,125,128,127,
    124,127,118,121,110,110,110,110,110,110,111,111,112,111,113,113,115,114,116,115,117,116,118,117,118,118,119,118,119,119,119,119,
    116,117,114,115,109,112,100,105, 87, 94,107, 97,122,115,131,127,136,134,138,137,147,139,179,168,181,182,170,176,138,142,133,135,
    128,130,126,126,125,125,125,125,124,124,125,125,126,126,125,126,125,125,125,125,125,125,125,125,127,126,135,130,136,138,145,135,
    216,177,174,193,149,160, 82, 85,116,101,126,124,127,127,125,126,119,122,114,116,112,113,111,111,110,110,111,110,111,111,112,112,
    113,113,114,114,115,115,116,116,117,117,118,117,118,118,119,119,119,119,119,119,118,118,116,117,114,115,109,112,101,106, 89, 96,
    106, 95,121,114,130,126,135,133,138,137,139,139,169,152,179,177,172,177,161,167,150,155,142,146,136,138,131,133,128,130,126,127,
    125,125,125,125,125,125,125,125,126,125,126,126,125,125,125,125,125,125,125,125,125,125,130,127,139,135,135,138,166,139,199,220,
    164,179, 94,152, 99, 83,125,116,127,127,127,128,123,126,118,121,110,111,110,110,110,110,111,111,112,112,113,113,116,116,117,117,
    118,117,118,118,118,118,118,118,118,118,118,118,116,117,114,116,110,112,102,106,118,111,129,124,135,132,138,137,139,139,170,156,
    178,177,170,175,159,165,149,154,141,145,136,138,131,134,128,129,126,126,126,126,126,126,125,126,125,125,125,125,126,125,132,128,
    192,150,191,213,159,173, 80,117,108, 88,127,121,128,128,127,128,121,124,116,119,112,114,110,111,109,110,110,109,110,110,111,110,
    112,112,114,113,115,114,116,115,117,116,117,117,118,118,118,118,118,118,117,117,115,116,111,113,105,109, 95,101, 95, 87,114,106,
    126,121,133,130,137,135,139,138,155,140,179,172,155,161,146,150,139,142,132,135,128,130,127,127,126,127,125,126,125,125,125,125,
    125,125,124,124,123,123,123,123,123,123,125,124,131,127,138,136,133,135,184,146,192,215,160,174, 83,129,107, 87,128,122,128,129,
    126,128,121,124,115,118,112,113,110,111,109,109,109,109,110,110,111,111,113,112,116,116,117,117,117,117,118,118,118,118,118,118,
    118,118,118,118,117,117,115,116,112,113,106,109, 95,102, 92, 88,133,130,137,135,139,138,155,140,179,172,178,181,168,174,156,162,
    146,151,138,141,131,134,129,129,127,128,126,127,125,126,126,126,125,126,125,125,124,124,123,123,129,126,137,134,133,135,176,143,
    197,221,163,178, 89,151,101, 84,124,117,127,126,126,127,120,123,115,117,111,113,109,110,109,109,110,109,111,110,112,111,113,113,
    114,114,115,115,116,116,117,117,118,118,118,118,119,119,119,119,118,119,118,118,116,117,114,115,110,112,104,108, 92, 99,100, 87,
    134,132,138,136,139,138,165,140,182,177,178,181,165,172,153,159,143,148,135,139,130,132,128,129,126,127,125,125,125,125,124,124,
    124,124,123,123,123,123,125,123,132,127,136,136,137,134,207,160,183,203,154,167, 79,104,111, 92,126,122,127,127,114,117,111,113,
    110,111,110,110,112,112,114,113,115,114,116,115,117,117,118,117,118,118,118,118,118,118,119,119,118,118,117,118,116,117,114,115,
    110,112,102,106, 88, 96,105, 93,120,113,130,126,135,133,138,137,139,139,178,166,182,183,172,178,158,165,147,152,128,130,126,127,
    125,126,124,125,124,124,125,125,125,126,125,125,124,124,124,124,131,127,137,136,136,135,191,152,187,209,157,170, 77,115,107, 86,
    127,121,128,128,115,118,112,113,110,111,110,110,110,110,111,111,112,112,114,113,115,114,116,115,117,116,118,117,119,119,119,119,
    119,119,118,119,117,118,115,116,112,114,105,109, 94,100, 97, 86,115,107,127,122,133,131,137,136,139,138,174,158,182,181,173,178,
    160,167,149,154,140,144,134,137,129,132,126,127,125,125,125,125,124,125,125,124,126,125,126,126,125,125,125,125,126,125,130,127,
    139,135,136,137,173,144,195,219,161,176, 84,150, 99, 80,125,117,128,128,127,128,122,125,116,119,112,114,110,111,110,110,110,110,
    113,112,114,114,116,115,117,116,117,117,118,118,118,118,119,118,119,119,118,118,118,118,117,118,116,117,114,115, 88, 94,110,100,
    123,117,131,128,136,134,138,137,159,139,178,172,175,178,164,170,152,158,144,148,137,140,132,134,129,131,127,128,125,126,126,125,
    125,126,125,125,126,127,126,126,125,125,125,125,125,125,127,126,136,131,137,139,142,134,223,173,179,198,151,163, 81, 84,117,101,
    125,127,120,123,116,118,112,114,111,112,110,111,110,110,111,110,112,111,113,112,114,114,115,115,116,116,117,117,119,119,119,119,
    119,119,119,119,118,118,116,117,112,114,107,110, 97,102, 88, 90,110,101,124,118,132,128,136,134,139,138,166,143,179,176,173,178,
    162,168,151,156,142,146,136,139,131,134,128,129,127,127,127,127,126,127,126,126,126,126,126,126,126,126,125,125,124,124,124,124,
    125,124,129,126,138,134,134,137,161,138,206,212,168,185, 95,155, 94, 78,124,113,128,128,128,128,123,126,117,120,113,115,111,112,
    110,110,110,110,111,110,112,111,113,113,114,114,115,115,116,116,118,118,118,118,118,118,119,119,119,119,118,118,116,117,113,115,
    109,112,101,105, 87, 95,105, 93,120,113,130,126,139,139,173,160,178,178,170,175,158,164,148,153,140,144,134,137,130,132,128,129,
    128,128,126,127,126,126,126,126,124,125,124,124,124,124,125,124,129,126,138,134,133,136,166,139,199,220,164,179, 91,152, 98, 80,
    125,116,129,128
  },
  { // Rv-RDET: 32 * 43 + 4 = 1380 data
    111,112,106,109,104,104,103,103,104,104,106,105,110,109,111,110,112,111,113,112,113,113,114,113,114,114,114,114,113,114,112,113,
    111,111,114,112,122,117,132,128,141,136,147,144,138,145,114,127, 94,102, 87, 88,117,111,127,123,135,131,139,137,140,140,141,141,
    142,142,142,142,143,142,143,143,142,143,141,142,139,140,137,138,136,137,135,136,134,135,137,135,147,141,171,162, 42,112, 80, 64,
    103, 93,203,158,234,226,222,232,176,196,147,160,130,138,120,125,115,117,112,113,109,111,104,106,103,104,104,104,105,104,106,106,
    108,107,109,108,110,110,112,111,113,112,113,113,114,114,114,114,114,114,114,114,113,113,112,112,112,111,118,114,129,123,137,133,
    145,141,144,146,126,137,101,113, 86, 92, 86, 85, 96, 90,110,103,122,116,131,127,137,134,140,139,142,141,142,142,142,142,142,142,
    142,142,141,142,140,141,138,139,137,138,136,136,135,135,134,134,137,135,147,141,160,163, 49, 85, 84, 69,105, 96,211,171,235,230,
    215,231,171,190,119,123,114,116,111,113,110,111,107,110,104,105,104,104,105,104,106,105,107,107,109,108,110,109,111,110,112,111,
    114,114,115,114,115,115,115,115,114,115,115,114,120,116,130,124,137,134,143,140,143,144,128,138,102,115, 83, 90, 79, 79, 88, 82,
    103, 95,117,110,128,123,136,132,141,139,144,143,144,144,144,144,144,144,144,144,144,144,144,144,143,143,142,142,140,141,138,139,
    136,137,135,136,134,134,135,134, 58,143, 78, 61,101, 91,184,127,232,217,230,235,134,142,122,127,115,118,112,113,110,111,110,110,
    105,105,106,105,107,106,108,108,109,109,110,110,111,111,112,112,113,113,114,113,114,114,115,115,115,115,115,115,116,115,119,117,
    128,123,137,133,139,138,139,139,131,137,109,121, 86, 97, 77, 80, 82, 78, 96, 89,111,104,124,118,134,130,140,137,143,142,145,144,
    144,144,144,144,143,143,141,142,139,140,138,138,136,137,135,136,135,135,137,135,143,140,164,152, 41,128, 79, 62,102, 92,196,151,
    229,221,220,228,181,202,150,164,132,140,121,126,115,118,112,113,110,111,106,108,104,105,104,104,105,105,106,106,107,106,108,108,
    110,109,111,110,112,111,113,112,114,113,115,114,115,115,114,114,113,113,115,114,123,118,130,127,132,131,135,133,133,135,117,126,
     97,106, 89, 91, 93, 90,104, 98,136,132,140,138,143,141,143,143,143,143,143,144,143,143,142,142,142,142,141,141,140,140,138,139,
    136,137,135,136,135,135,134,134,135,134,140,137,161,146,103,166, 64, 43, 94, 81,171,103,225,207,225,229,193,215,158,173,137,146,
    124,129,117,120,113,114,111,112,106,108,104,104,104,104,105,104,106,105,107,107,108,108,109,109,111,110,112,111,113,112,114,113,
    115,114,115,115,115,115,115,115,113,114,112,112,112,111,118,114,126,122,132,130,138,135,139,140,125,134,104,115,104, 98,115,110,
    126,121,134,130,139,136,141,140,142,142,143,142,143,143,143,143,143,143,142,142,140,141,139,140,135,134,140,137,161,146,121,173,
    161,101,222,202,226,229,198,217,161,177,139,148,113,115,109,112,104,106,103,104,104,103,105,104,106,106,108,107,109,108,110,109,
    111,110,112,111,112,112,113,113,114,114,115,115,115,115,114,115,112,113,110,111,128,123,136,132,145,141,143,147, 92, 95, 95, 92,
    105,100,117,111,138,136,140,139,141,141,142,142,142,142,142,142,142,142,141,142,140,140,138,139,135,135,134,134,140,136,159,145,
    135,175, 58, 41,220,191,232,231,206,225,166,183,141,152,127,133,118,122,114,116,111,112,105,107,103,103,103,103,104,104,106,105,
    107,107,109,108,110,109,111,110,112,111,113,112,114,114,114,114,114,114,112,113,111,111,114,111,123,118,134,129,144,139,149,148,
    139,147,114,127, 92,102, 86, 87,116,110,127,122,134,131,138,136,140,139,141,140,141,141,142,142,143,142,143,143,142,143,141,142,
    139,140,137,138,135,135,138,135,148,142,172,164, 41,110, 80, 63,103, 93,203,159,233,225,221,230,176,196,148,160,130,138,120,125,
    115,117,112,113,107,111,104,105,103,103,104,103,108,107,109,108,110,110,111,111,112,112,113,113,114,113,114,114,114,114,114,114,
    113,113,111,112,112,111,119,115,130,124,140,135,147,144,146,149,125,138,100,112, 86, 91, 87, 85, 97, 91,110,103,122,116,131,127,
    137,134,141,139,142,142,143,143,142,142,142,142,141,141,141,141,137,138,135,136,134,135,134,134,133,133,136,134,145,139,164,161,
     40, 96, 79, 62,102, 92,209,168,234,229,215,231,129,136,120,124,114,117,112,113,111,111,110,110,106,108,105,105,105,105,106,106,
    107,107,109,108,110,109,111,110,112,111,113,112,113,113,114,114,115,114,115,115,115,115,119,116,128,123,137,133,141,139,143,143,
    133,140,108,122, 86, 96, 77, 79, 84, 79, 98, 90,113,105,125,119,144,142,145,145,146,146,146,146,145,145,144,144,140,141,139,140,
    137,138,136,137,136,136,135,135,134,134,133,133,132,132,131,131,130,131,130,130,130,130,132,130,141,136,160,153, 19,108, 65, 44,
    116, 81,211,175,232,228,206,227,166,184,142,153,128,134,119,123,111,112,111,111,112,111,113,112,112,113,109,110,108,108,108,108,
    109,109,110,110,111,111,113,112,114,113,115,114,116,116,116,116,116,116,116,116,117,116,122,119,130,126,135,134,134,135,134,134,
    124,131,102,113, 83, 91, 79, 79, 88, 83,104, 96,121,114,131,127,137,134,140,138,147,147,146,147,145,146,144,145,143,144,142,143,
    141,142,140,141,139,139,137,138,136,136,135,135,134,134,134,134,163,148, 60,144, 73, 55, 98, 87,190,143,226,216,219,226,184,206,
    152,166,133,141,122,127,115,118,112,113,110,111,104,104,105,105,106,105,107,106,108,107,109,109,111,110,112,112,113,112,114,114,
    115,115,116,115,116,116,116,116,116,116,114,115,113,113,115,113,122,118,129,126,134,137,117,126, 98,107, 91, 93, 96, 92,107,101,
    119,113,129,124,137,133,141,139,144,142,145,144,144,145,143,144,141,142,140,141,139,140,139,139,138,138,138,138,137,138,136,137,
    135,135,134,134,133,133,132,132,132,132,136,133,148,140,149,164, 44, 65, 82, 65,134, 94,216,187,230,229,205,224,127,133,119,122,
    114,116,112,113
  }
};
// record current adc data offset
static u32 ecg_adcBufOff[2];
#endif

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

#ifdef LiuJH_ECG
    ecg_adcBufOff[0] = ecg_adcBufOff[1] = 0;
#endif
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
#ifdef LiuJH_ECG
  u8 byte = 0;
  // test ONLY
  if(_needConv){
    byte = (u8)ecg_adcBuf[_chIndex][ecg_adcBufOff[_chIndex]++];

    // store into buf
    ecg_buf[_chIndex][ecg_bufBnum[_chIndex]] = byte;
  }
  ecg_adcMaxValue = 255;
  ecg_adcMinValue = 0;
#else
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
#endif

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
    ecg_adcMaxValue = value + ecg_AdcWeightGain.ecg_AdcMaxValueGain;
  if(value < ecg_adcMinValue)
    ecg_adcMinValue = value;

  // record peak value interval 2.5s
  if(value > ecg_adcPeakMaxValue)
    ecg_adcPeakMaxValue = value + ecg_AdcWeightGain.ecg_AdcMaxValueGain;
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
    1. weight = 60000 * (1/(bpm - ECG_BPM_WEIGHT) - 1/(bpm + ECG_BPM_WEIGHT)) / 2;
    2. So is: weight = 300000 / ((bpm - ECG_BPM_WEIGHT)(bpm + ECG_BPM_WEIGHT));unit is: ms
    3. We want points, So weight = (weight + 2) >> 2;
    4. So is: weight = (300000 / ((bpm - ECG_BPM_WEIGHT)(bpm + ECG_BPM_WEIGHT)) + 2) >> 2;
    5. 60000 is 1minute-->number of ms;
    6. the MIN move window is (ECG_Rn_MW_HALF_SIZE * 2 - 1) points, so half window is ECG_Rn_MW_HALF_SIZE;
*/
static u32 ecg_calRnWinHalfWeight(void)
{
  u32 ret = ECG_Rn_MW_HALF_SIZE;
  u32 m = 300000;
  u32 b = (ecg_bpm[ECG_RS_INDEX] - ECG_BPM_WEIGHT)*(ecg_bpm[ECG_RS_INDEX] + ECG_BPM_WEIGHT);

  // check bpm and b is valid
  if(!ecg_bpm[ECG_RS_INDEX] || b == 0) return ret;

#ifndef LiuJH_DEBUG
  // test only(test it later, we'll use this calculate methlod)

  // calculate time of half weight(unit: ms)
  b = m / b;
  // calculate point num of move window
  b = (b * 1000 + (ecg_RPPiTimeUs >> 1)) / ecg_RPPiTimeUs;
#else
  // calculate move window value
  b = (m / b + 2) >> 2;
#endif
  // use it?
  if(b > ret)
    ret = b;

  return ret;
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

  // update Rn weight using the newest bpm
  ecg_RnHalfWeight = ecg_calRnWinHalfWeight();

	// is escape?
	if(ecg_RnEscaped){
		ecg_Rdetected[chIndex] = ecg_RnHalfWeight;  // ECG_Rn_MW_HALF_SIZE;
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
	ecg_RnWmin = ecg_RRiPointNum[chIndex] - ecg_RnHalfWeight; // ECG_Rn_MW_HALF_SIZE;
	ecg_RnWmax = ecg_RRiPointNum[chIndex] + ecg_RnHalfWeight; // ECG_Rn_MW_HALF_SIZE;

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
  if(ecg_bufWnum[_chIndex] > (ecg_RnHalfWeight << 1)){
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

  // for next loop  // update in ecg_smRnDetect
//  ecg_Rdetected[_chIndex]++;
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

  // We have data?
  if(ecg_bufWnum[chIndex] == 0) return;

	// exist new u16 data in Rs buf? got it and convert byte and store into Rs buf
	if(ecg_bufWnum[chIndex] > ecg_bufBnum[chIndex]){
  	// pull MSB of u16
  	adcValue = ps[ecg_bufBoff[chIndex]++];
  	// pull LSB of u16
  	adcValue = (adcValue << 8) | ps[ecg_bufBoff[chIndex]++];

#ifdef LiuJH_DEBUG
    // trim peak point value??
    ecg_filterPeakValue(adcValue);
#endif

  	// convert u16 to u8 and store into Rs buf
  	ecg_convAdcValueToByte(chIndex, adcValue, true);
	}

	// we have enough byte to detect Rn?
	if(ecg_bufBnum[chIndex] < ECG_RN_DETECT_NUM){
    // loop waiting next data
    return;
  }

  // we have new data for detecting?
  if(ecg_bufBnum[chIndex] + ecg_RnWmin <= ecg_Rdetected[chIndex]) return;

  // the first detection for this Rn peak point?
  if(ecg_Rdetected[chIndex] == ecg_RnWmin){
    // ignore the first two point detected
    ecg_Rdetected[chIndex] += (ECG_RN_DETECT_NUM - 1);
  }

	/*
  	NOW:
  	  1. We have ECG_RN_DETECT_NUM data at least: A B C (D E F);
  	  2. R: is more than RnValueV and more than both prev and next data(cur byte);
  	  3. if Rndetected num more than RnWmax, escape;
	*/

	// get the newest three data for compare
	cOff = ecg_Rdetected[chIndex] - ecg_RnWmin;
	bOff = cOff - 1;
	aOff = bOff - 1;
  // get these points value
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

      // Need update bpm and RRiTimeMs and RRiPointNum?
      offb = ecg_RnWmin + bOff;
      if(ecg_RRiPointNum[chIndex] != offb){
        u32 ms = (offb * ecg_RPPiTimeUs[chIndex]) / 1000;
//        ecg_RRiTimeMs[chIndex] = (offb * ecg_RPPiTimeUs[chIndex]) / 1000;
        if(ms){
          u32 b = (60 * 1000) / ms;
          if(b > ecg_bpm[chIndex] + ECG_BPM_WEIGHT || b + ECG_BPM_WEIGHT < ecg_bpm[chIndex]){
            /* bpm changes too much, redo again */

            // adc stable tick
            ecg_adcStableTick = HAL_GetTick() + ADC_WORKING_STABLE_TIME;
            // startup again
            ecg_status = ecg_startup_status;
            return;
          }else{
            // update bpm and others params and loop Rn detection
            ecg_RRiPointNum[chIndex] = offb;
            ecg_RRiTimeMs[chIndex] = ms;
            // update this bpm
            ecg_bpm[chIndex] = b;
          }
        }
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

  // for next loop detection
  (ecg_Rdetected[chIndex])++;

	// escape?
	if(!found && ecg_Rdetected[chIndex] >= ecg_RnWmax){
		// record Rn detected and escaped num
		ecg_RnDetected++;
		ecg_RnEscaped++;
		ecg_RnEscapedAll++;

#ifdef LiuJH_DEBUG
		// twice continue escaped? restart R1 waiting...
		if(ecg_RnEscaped > ECG_ESCAPED_MAX_NUM){
		  // redo R detect, restart up ecg detection
		  ecg_status = ecg_startup_status;
		}else{
		  // for next Rn detect, go into next status for sync buf vars
		  ecg_status = ecg_RnDetected_status;
		}
#else
    // redo R detect, restart up ecg detection
    ecg_status = ecg_startup_status;
#endif
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

  ecg_RnHalfWeight = ecg_calRnWinHalfWeight();  // ECG_Rn_MW_HALF_SIZE; // 

  // get Rn window min and max point num
  ecg_RnWmin = ecg_RRiPointNum[chIndex] - ecg_RnHalfWeight; // ECG_Rn_MW_HALF_SIZE; // 
  ecg_RnWmax = ecg_RRiPointNum[chIndex] + ecg_RnHalfWeight; // ECG_Rn_MW_HALF_SIZE; //

#ifdef LiuJH_DEBUG
	/*
	  For testing:
		1. Loop record data in buffer;
		2. if full, num set 0, and loop record data in buffer continue;
		3. for each Rn detected, check where is ERROR;
	*/
	ecg_initBuf();
#endif

#ifdef LiuJH_OVBC
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
//  	u32 n1 = ecg_RRiPointNum[ECG_RS_INDEX], n2 = ecg_RRiPointNum[ECG_RV_INDEX];
//    u32 n1 = ecg_bpm[ECG_RS_INDEX], n2 = ecg_bpm[ECG_RV_INDEX];

    // clear static var for next detect
    ecg_Rok[ECG_RS_INDEX] = ecg_Rok[ECG_RV_INDEX] = false;

    // check bpm of Rs and Rv is equal?
//    if(n1 + ECG_BPM_WEIGHT > n2 || n2 + ECG_BPM_WEIGHT > n1){
    	// check RRiPointNum
//    if(n1 == n2 || n1 == n2 + 1 || n1 + 1 == n2){
  		/* NOW: we can calculate Rs_Rv delay time(unit: ms) */
  		ecg_calculateRsvi();

      // for next redo because of little bpm
      ecg_detectAgain = false;

  		// got into next status for Rn detection
  		ecg_status = ecg_Rnwaiting2_status;
/*
    }else{
  		// restart detect both of Rs and Rv channel
  		ecg_status = ecg_startup_status;
    }
*/
    return;
  }

  /*
  	NOW:
  		1. MUST one channel is NOT OK;
  		2. check Rs channel at first;
  */

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

#ifndef LiuJH_DEBUG
    // need redo because of more little bpm value?
    if(!ecg_detectAgain && ecg_bpm[chIndex] < ECG_BPM_MIN){
      ecg_detectAgain = true;
      ecg_status = ecg_startup_status;
    }else{
      // update finished flag
      ecg_Rok[chIndex] = true;
    }
#else
    // update finished flag
    ecg_Rok[chIndex] = true;
#endif

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
  u16 adcValue = 0;
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
          // select max Rvalue of R1 and R2
          if(*p > ecg_Rvalue[chIndex]){
            // average R value
            ecg_Rvalue[chIndex] = (ecg_Rvalue[chIndex] + *p) >> 1;
          }

          // redo R value V for Rn detection
          ecg_RvalueV[chIndex] = ecg_Rvalue[chIndex] * ECG_R_VALUE_WEIGHT;
#else
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
  u16 adcValue = 0;

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
    ecg_RvalueV[i] = ecg_Rvalue[i] * ECG_R_VALUE_WEIGHT1;
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
  u16 adcValue = 0;
  u8 byte = 0;

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
  if(byte && byte >= ecg_Rvalue[chIndex]){
    // record data value
    ecg_Rvalue[chIndex] = byte;
    // record data offset in buf
    ecg_R1off[chIndex] = ecg_bufBnum[chIndex] - 1;
  }

  // collect data finished? update finished flag in current channel
  // check overflow or OK
  if(ecg_bufBnum[chIndex] >= ECG_BUF_R1_NEED_NUM){
    // exceed buffer?
    // check the wave is stable or not(Continuous of max value)
    if((ecg_Rvalue[chIndex] == ecg_buf[chIndex][ecg_R1off[chIndex] - 1])
      // for slope data num
      || (ecg_bufBnum[chIndex] <= ecg_R1off[chIndex] + ECG_R2_MW_SIZE)
      // If no signal now, return
      || (ecg_adcMaxValue <= ecg_adcMinValue + ecg_AdcWeightGain.ecg_AdcMaxValueGain
      + ecg_AdcWeightGain.ecg_AdcNoSignalWeight)){

      // restart collect data
      ecg_status = ecg_startup_status;
    }else{
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
*/
void ecg_calibrateAdcWeightGain(void)
{
  ecg_AdcWeightGain_typeDef *p = &ecg_AdcWeightGain;

  // unvalid? set default
  if(p->isValid ^ MCU_DATA_STRUCT_VALID_VALUE){
    p->ecg_AdcNoSignalWeight = ECG_ADC_NO_SIGNAL_WEIGHT;
    p->ecg_AdcMaxValueGain = ECG_ADC_MAX_VALUE_GAIN;

    p->isValid = MCU_DATA_STRUCT_VALID_VALUE;

    // store this key value into EEPROM
    ee_readOrWriteKeyValue(ee_kv_baseData, false);
  }
}


/*
*/
ecg_AdcWeightGain_typeDef* ecg_getAdcWeightGain(void)
{
  return &ecg_AdcWeightGain;
}

/*
*/
u8 ecg_getStatus(void)
{
  return (u8)(ecg_status);
}

/*
*/
void ecg_getAdcPeakValue(u16 *_pmax, u16 *_pmin)
{
  *_pmax = (u16)ecg_adcMaxValue;
  *_pmin = (u16)ecg_adcMinValue;
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


