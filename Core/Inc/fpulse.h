/*
 * fpulse.h
 *
 *  Created on: 2024年1月5日
 *      Author: Johnny
 */

#ifndef INC_FPULSE_H_
#define INC_FPULSE_H_

// fovbc working time (unit: ms) 2 seconds
#define FPULSE_FOVBC_TICK           2000



typedef enum{
  /*
    1. check flag;
    2. set timeout tick(2 seconds) for fovbc;
    3. startup fovbc if flag is true;
    4. enter into pulsing status;
  */
  fpulse_inited_status,
  /*
    1. check tick;
    2. shuddown fovbc if tick is timeout;
    3. reinit, and enter into inited status for next action;
  */
  fpulse_pulsing_status,



  fpulse_max_status
} fpulse_status_typeDef;



extern void fpulse_stateMachine(void);
extern void fpulse_init(void);
extern void fpulse_startupPulsing(u8 _width);
extern bool fpulse_isWorking(void);



#endif /* INC_FPULSE_H_ */
