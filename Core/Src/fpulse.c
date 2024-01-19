/*
 * fpulse.c
 *
 *  Created on: 2024年1月5日
 *      Author: Johnny
 */
 
#include "main.h"
 
 
/* variable define **************************************************/

static fpulse_status_typeDef fpulse_status;
static bool fpulseIsWorking;
static u32 fpulse_fovbcTick;
static bool fpulse_startupFovbcFlag;



/* private function define ******************************************/


/*
  brief:
    1. check tick;
    2. shuddown fovbc if tick is timeout;
    3. reinit, and enter into inited status for next action;
*/
static void fpulse_smPulsing(void)
{
  // timeout? shuddown fovbc
  if(HAL_GetTick() > fpulse_fovbcTick){
//  if(!fovbc_isWorking()){
    fovbc_shutdown();
    // enter into inited status
    fpulse_init();
  }
}

/*
  brief:
    1. check flag;
    2. set timeout tick(2 seconds) for fovbc;
    3. startup fovbc if flag is true;
    4. enter into pulsing status;
*/
static void fpulse_smInited(void)
{
  // check flag
  if(!fpulse_startupFovbcFlag) return;

#ifdef LiuJH_NOTE
  // stop working of pulse and ovbc
  pulse_bleConfigPulseOn(false);
  if(ovbc_isWorking())
    ovbc_shutdown();
#endif

  /* NOW: we can startup fovbc */

  // flag set, so startup fovbc and set tick
  fpulseIsWorking = true;
  fpulse_status = fpulse_pulsing_status;
  fpulse_fovbcTick = HAL_GetTick() + FPULSE_FOVBC_TICK;
  // startup fovbc
  fovbc_startup();
}


/* public function define *******************************************/


void fpulse_stateMachine(void)
{
  switch(fpulse_status){
    case fpulse_inited_status:
      fpulse_smInited();
      break;
    case fpulse_pulsing_status:
      fpulse_smPulsing();
      break;

    default:
      break;
  }
}

/*
*/
bool fpulse_isWorking(void)
{
  return fpulseIsWorking;
}

/*
  brief:
    1. _width is pulse width, unit is 100us, that is:
      1   0.1ms
      2   0.2ms
      10  1ms   this is the max value;
    2. 
*/
void fpulse_startupPulsing(u8 _width)
{
  if(fpulseIsWorking) return;

  // set fovbc pulse width
  fovbc_setPulseWidth(_width);

  // set start up flag
  fpulse_startupFovbcFlag = true;
}

/*
  brief:
    1. init fpulse and fovbc init;
    2. 
*/
void fpulse_init(void)
{
  fpulseIsWorking = false;
  fpulse_fovbcTick = 0;
  fpulse_startupFovbcFlag = false;

  fpulse_status = fpulse_inited_status;

  fovbc_init();
}


