#ifndef _IVA_H_
#define _IVA_H_

#include "common.h"

#include "rockiva/rockiva_ba_api.h"
#include "rockiva/rockiva_common.h"
#include "rockiva/rockiva_det_api.h"
#include "rockiva/rockiva_face_api.h"
#include "rockiva/rockiva_image.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROCKIVA_PATH_LENGTH (128)
typedef struct _rkMpiIvaCtx {
	RK_CHAR *pModelDataPath;
	RK_U32 u32ImageWidth;
	RK_U32 u32ImageHeight;
	RK_U32 u32DetectStartX;
	RK_U32 u32DetectStartY;
	RK_U32 u32DetectWidth;
	RK_U32 u32DetectHight;
	RK_U32 u32IvaDetectFrameRate;
	RockIvaHandle ivahandle;
	RockIvaImageTransform eImageTransform;
	RockIvaImageFormat eImageFormat;
	RockIvaDetModel eModeType;
	RockIvaBaTaskParams baParams;
	RockIvaFaceTaskParams faceParams;
	RockIvaInitParam commonParams;
	ROCKIVA_BA_ResultCallback resultCallback;
	ROCKIVA_FrameReleaseCallback releaseCallback;
} IVA_CTX_S;
extern IVA_CTX_S iva;

int init_iva(void);
int deinit_iva(void);

#ifdef __cplusplus
}
#endif

#endif
