#ifndef _IVS_H_
#define _IVS_H_

#include "common.h"

struct ivs_info {
	RK_U32 u32Width;
	RK_U32 u32Height;
	RK_U32 u32Sensitivity;
};


RK_S32 init_ivs( int width, int height, RK_U32 u32Sensitivity );
RK_S32 deinit_ivs();

#endif