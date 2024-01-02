/*
 * simpledef.h
 *
 *  Created on: Jul 24, 2023
 *      Author: Johnny
 */

#ifndef INC_SIMPLEDEF_H_
#define INC_SIMPLEDEF_H_

/* defines -----------------------------------------------------------*/



#define bool	_Bool
#define true	1
#define false	0

// test used
#define LiuJH_DEBUG


// ICM and CCM
#ifndef LiuJH_DEBUG
// for ICM test
#define Insync_ICM

#else
// for CCM
#define Insync_CCM

#endif

/*  types ------------------------------------------------------------*/
typedef uint32_t  u32;
typedef uint16_t  u16;
typedef uint8_t   u8;

typedef int32_t   i32;
typedef int16_t   i16;
typedef int8_t    i8;
typedef int32_t   int32;
typedef int16_t   int16;
typedef int8_t    int8;


/* constants --------------------------------------------------------*/



/* macro ------------------------------------------------------------*/



/* Exported functions prototypes ---------------------------------------------*/




#endif /* INC_SIMPLEDEF_H_ */
