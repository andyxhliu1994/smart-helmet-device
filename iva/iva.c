#include "iva.h"

/******************************************************************************* */
IVA_CTX_S iva;

RK_S32 IVA_Create(IVA_CTX_S *ctx) {
	RK_S32 s32Ret = RK_FAILURE;

	snprintf(ctx->commonParams.modelPath, ROCKIVA_PATH_LENGTH, ctx->pModelDataPath);
	ctx->commonParams.coreMask = 0x04;
	ctx->commonParams.logLevel = ROCKIVA_LOG_ERROR;
	ctx->commonParams.detModel = ctx->eModeType; /* Detect type */
	ctx->commonParams.imageInfo.width = ctx->u32ImageWidth;
	ctx->commonParams.imageInfo.height = ctx->u32ImageHeight;
	ctx->commonParams.imageInfo.format = ctx->eImageFormat;
	ctx->commonParams.imageInfo.transformMode = ctx->eImageTransform;

	/* IVA init */
	s32Ret = ROCKIVA_Init(&ctx->ivahandle, ROCKIVA_MODE_VIDEO, &ctx->commonParams, NULL);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("ROCKIVA_Init failure:%X", s32Ret);
		return s32Ret;
	}

	/* Set Detect area */
	ctx->baParams.baRules.areaInBreakRule[0].ruleEnable = RK_TRUE;
	ctx->baParams.baRules.areaInBreakRule[0].sense = 90;
	ctx->baParams.baRules.areaInBreakRule[0].alertTime = 1000; /* 1000 ms */
	ctx->baParams.baRules.areaInBreakRule[0].minObjSize[2].width =
	    ctx->u32ImageWidth / 100;
	ctx->baParams.baRules.areaInBreakRule[0].minObjSize[2].height =
	    ctx->u32ImageHeight / 100;
	ctx->baParams.baRules.areaInBreakRule[0].event = ROCKIVA_BA_TRIP_EVENT_STAY;
	ctx->baParams.baRules.areaInBreakRule[0].ruleID = 0;

	ctx->baParams.baRules.areaInBreakRule[0].objType =
	    ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_PERSON);
	ctx->baParams.baRules.areaInBreakRule[0].objType |=
	    ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_FACE);
	ctx->baParams.baRules.areaInBreakRule[0].objType |=
	    ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_PET);
		
	ctx->baParams.baRules.areaInBreakRule[0].objType |= 
		ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_VEHICLE);
	ctx->baParams.baRules.areaInBreakRule[0].objType |= 
		ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_NON_VEHICLE);
	ctx->baParams.baRules.areaInBreakRule[0].objType |= 
		ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_MOTORCYCLE);		
	ctx->baParams.baRules.areaInBreakRule[0].objType |= 
		ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_BICYCLE);	
	ctx->baParams.baRules.areaInBreakRule[0].objType |= 
		ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_PLATE);				
	ctx->baParams.baRules.areaInBreakRule[0].objType |= 
		ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_BABY);				
	ctx->baParams.baRules.areaInBreakRule[0].objType |= 
		ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_PACKAGE);			

	ctx->baParams.baRules.areaInBreakRule[0].area.pointNum = 4;
	ctx->baParams.baRules.areaInBreakRule[0].area.points[0].x =
	    ROCKIVA_PIXEL_RATION_CONVERT(ctx->u32ImageWidth, ctx->u32DetectStartX);
	ctx->baParams.baRules.areaInBreakRule[0].area.points[0].y =
	    ROCKIVA_PIXEL_RATION_CONVERT(ctx->u32ImageHeight, ctx->u32DetectStartY);
	ctx->baParams.baRules.areaInBreakRule[0].area.points[1].x =
	    ROCKIVA_PIXEL_RATION_CONVERT(ctx->u32ImageWidth,
	                                 ctx->u32DetectStartX + ctx->u32DetectWidth);
	ctx->baParams.baRules.areaInBreakRule[0].area.points[1].y =
	    ROCKIVA_PIXEL_RATION_CONVERT(ctx->u32ImageHeight, ctx->u32DetectStartY);
	ctx->baParams.baRules.areaInBreakRule[0].area.points[2].x =
	    ROCKIVA_PIXEL_RATION_CONVERT(ctx->u32ImageWidth,
	                                 ctx->u32DetectStartX + ctx->u32DetectWidth);
	ctx->baParams.baRules.areaInBreakRule[0].area.points[2].y =
	    ROCKIVA_PIXEL_RATION_CONVERT(ctx->u32ImageHeight,
	                                 ctx->u32DetectStartY + ctx->u32DetectHight);
	ctx->baParams.baRules.areaInBreakRule[0].area.points[3].x =
	    ROCKIVA_PIXEL_RATION_CONVERT(ctx->u32ImageWidth, ctx->u32DetectStartX);
	ctx->baParams.baRules.areaInBreakRule[0].area.points[3].y =
	    ROCKIVA_PIXEL_RATION_CONVERT(ctx->u32ImageHeight,
	                                 ctx->u32DetectStartY + ctx->u32DetectHight);
	ctx->baParams.aiConfig.detectResultMode = 0;

	s32Ret = ROCKIVA_BA_Init(ctx->ivahandle, &ctx->baParams, ctx->resultCallback);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("ROCKIVA_BA_Init failure:%X", s32Ret);
		return s32Ret;
	}

	s32Ret = ROCKIVA_SetFrameReleaseCallback(ctx->ivahandle, ctx->releaseCallback);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("ROCKIVA_SetFrameReleaseCallback failure:%#X", s32Ret);
		return s32Ret;
	}
	return s32Ret;
}

RK_S32 SAMPLE_COMM_IVA_Destroy(IVA_CTX_S *ctx) {

	RK_S32 s32Ret = RK_FAILURE;
	s32Ret = ROCKIVA_BA_Release(ctx->ivahandle);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("ROCKIVA_BA_Release failure:%X", s32Ret);
		return s32Ret;
	}
	s32Ret = ROCKIVA_Release(ctx->ivahandle);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("ROCKIVA_Release failure:%X", s32Ret);
	}

	return s32Ret;
}

static struct timeval start_time, stop_time;

static const char* type_name[]={
        "未知"/*ROCKIVA_OBJECT_TYPE_NONE = 0  */
    ,   "行人"/* ROCKIVA_OBJECT_TYPE_PERSON = 1 */
    ,   "机动车"/* ROCKIVA_OBJECT_TYPE_VEHICLE = 2 */
    ,   "非机动车"/* ROCKIVA_OBJECT_TYPE_NON_VEHICLE = 3 */
    ,   "人脸"/* ROCKIVA_OBJECT_TYPE_FACE = 4 */
    ,   "人头"/*ROCKIVA_OBJECT_TYPE_HEAD = 5  */
    ,   "宠物(猫/狗)"/* ROCKIVA_OBJECT_TYPE_PET = 6 */
    ,   "电瓶车 "/* ROCKIVA_OBJECT_TYPE_MOTORCYCLE = 7*/
    ,   "自行车"/* ROCKIVA_OBJECT_TYPE_BICYCLE = 8 */
    ,   "车牌"/* ROCKIVA_OBJECT_TYPE_PLATE = 9 */
    ,   "婴幼儿"/* ROCKIVA_OBJECT_TYPE_BABY = 10 */
    ,   "快递包裹"/* ROCKIVA_OBJECT_TYPE_PACKAGE = 11 */
};

osd_obj_det_info obj_det_info;
const uint32_t type_color[] = {
	ARGB_gray, 	ARGB_red, ARGB_blue,ARGB_green,ARGB_pink,
	ARGB_pink, 	ARGB_red, ARGB_green,ARGB_green,ARGB_beige,
	ARGB_red, ARGB_burlywood
};

static void rkIvaEvent_callback(const RockIvaBaResult *result,
                                const RockIvaExecuteStatus status, void *userData) {
	gettimeofday(&stop_time, NULL);	
	
	pthread_mutex_lock(&obj_det_update_lock);
	if (result->objNum == 0){
		obj_det_info.num = 0;
		pthread_mutex_unlock(&obj_det_update_lock);
		sem_post( obj_det_sem );
		return;
	}
		
	int x_min =1000000; int y_min = 100000; 	
	int x_max =0;       int y_max = 0;

	printf("\n\nIVA size:%d once run use %.1f ms\n", 
			result->objNum,  (__get_us(stop_time) - __get_us(start_time)) / 1000 );	

	for (int i = 0; i < result->objNum; i++) {
		printf("topLeft:[%d,%d], bottomRight:[%d,%d],"
		        "objId is %d, ti is %d, score is %d, type is %d = %s\n",
		        result->triggerObjects[i].objInfo.rect.topLeft.x,
		        result->triggerObjects[i].objInfo.rect.topLeft.y,
		        result->triggerObjects[i].objInfo.rect.bottomRight.x,
		        result->triggerObjects[i].objInfo.rect.bottomRight.y,
		        result->triggerObjects[i].objInfo.objId,
		        result->triggerObjects[i].objInfo.frameCount,
		        result->triggerObjects[i].objInfo.score,
		        result->triggerObjects[i].objInfo.type,
				type_name[ result->triggerObjects[i].objInfo.type]
			);

		if( i < OBJ_DET_OSD_MAX ){
			obj_det_info.obj_det[i].enable = 1;

			int tp = (int)( result->triggerObjects[i].objInfo.type );
			obj_det_info.obj_det[i].type = tp;
#if 0			
			if( (tp >= ROCKIVA_OBJECT_TYPE_NONE) && (tp < ROCKIVA_OBJECT_TYPE_MAX) )
				obj_det_info.obj_det[i].color = type_color[tp];
			else
				obj_det_info.obj_det[i].color = type_color[0];
#else
			obj_det_info.obj_det[i].color = 0xffff00ff;
#endif

			obj_det_info.obj_det[i].score = (float)( result->triggerObjects[i].objInfo.score   /100.0f );
			obj_det_info.obj_det[i].start_x = result->triggerObjects[i].objInfo.rect.topLeft.x /10000.0f;
			obj_det_info.obj_det[i].start_y = result->triggerObjects[i].objInfo.rect.topLeft.y /10000.0f;
			int w = result->triggerObjects[i].objInfo.rect.bottomRight.x - result->triggerObjects[i].objInfo.rect.topLeft.x ;
			int h = result->triggerObjects[i].objInfo.rect.bottomRight.y - result->triggerObjects[i].objInfo.rect.topLeft.y ;
			obj_det_info.obj_det[i].width = w / 10000.0f;
			obj_det_info.obj_det[i].hight = h / 10000.0f;

			x_min = mmin( x_min,  result->triggerObjects[i].objInfo.rect.topLeft.x );
			y_min = mmin( y_min,  result->triggerObjects[i].objInfo.rect.topLeft.y );
			x_max = mmax( x_max,  result->triggerObjects[i].objInfo.rect.bottomRight.x );
			y_max = mmax( y_max,  result->triggerObjects[i].objInfo.rect.bottomRight.y );
		}
		// LOG_INFO("triggerRules is %d, ruleID is %d, triggerType is %d\n",
		//          result->triggerObjects[i].triggerRules,
		//          result->triggerObjects[i].firstTrigger.ruleID,
		//          result->triggerObjects[i].firstTrigger.triggerType);
	}

	obj_det_info.x_start = x_min /10000.0f;
	obj_det_info.y_start = y_min /10000.0f;
	obj_det_info.w_whole = ( x_max - x_min  ) /10000.0f;
	obj_det_info.h_whole = ( y_max - y_min  ) /10000.0f;	
	obj_det_info.num = result->objNum;

	pthread_mutex_unlock(&obj_det_update_lock);
	sem_post( obj_det_sem );

#if 0
	if (status == ROCKIVA_SUCCESS) {
	    CachedImageMem *cached_image_mem = get_image_from_cache(result->frameId);
	    if (cached_image_mem != nullptr) {
	        RockIvaImage *image = cached_image_mem->img;
	        for (int i = 0; i < result->objNum; i++) {
	            if (result->objInfo[i].firstTrigger.ruleID != -1) {
	                capture_object(image, &(result->objInfo[i]));
	            }
	        }
	        for (int i = 0; i < result->objNum; i++) {
	            int color_r=0, color_g=0, color_b=0;
	            int text_color_r=0, text_color_g=0, text_color_b=0;
	            char text[32];
	            snprintf(text, 32, "%d - %d", result->objInfo[i].objId,
	            result->objInfo[i].confidence); get_object_color(&(result->objInfo[i]), &color_r,
	            &color_g, &color_b); get_object_trigger_color(&(result->objInfo[i]),
	            &text_color_r, &text_color_g, &text_color_b); draw_rect(image,
	            result->objInfo[i].objRect, color_b, color_g, color_r); draw_text(image, text,
	            result->objInfo[i].objRect, text_color_b, text_color_g, text_color_r);
	        }

	        draw_rule(image, &initParams);

	        char out_img_path[PATH_MAX] = {0};
	        snprintf(out_img_path, PATH_MAX, "%s/%d.jpg", OUT_FRAMES_PATH, result->frameId);
	        printf("write img to %s\n", out_img_path);
	        write_image(image, out_img_path);
	        release_image(cached_image_mem);
	    }
	}
#endif		
}

static void program_handle_error(const char *func, RK_U32 line) {
	RK_LOGE("func: <%s> line: <%d> error exit!", func, line);
	quit = 1;
}

static void rkIvaFrame_releaseCallBack(const RockIvaReleaseFrames *releaseFrames,
                                       void *userdata) {
	return;
}

int init_iva(void)
{
    if( strlen( pIVAModelPath) <= 3  ){
        RK_LOGI("iva model path is null, return\n");
        return -1;  
    }
	RK_LOGI("========iva init begin========\n");
	/* Init iva */
	iva.pModelDataPath = pIVAModelPath;
	iva.u32ImageHeight = 640;
	iva.u32ImageWidth  = 640;
	iva.u32DetectStartX = 0;
	iva.u32DetectStartY = 0;
	iva.u32DetectWidth = 640;
	iva.u32DetectHight = 640;
	iva.eImageTransform = ROCKIVA_IMAGE_TRANSFORM_NONE;
	iva.eImageFormat = ROCKIVA_IMAGE_FORMAT_YUV420SP_NV12;
	iva.eModeType = ROCKIVA_DET_MODEL_CLS8;//ROCKIVA_DET_MODEL_PFP;
	iva.u32IvaDetectFrameRate = mainStreamFps;
	iva.resultCallback = rkIvaEvent_callback;
	iva.releaseCallback = rkIvaFrame_releaseCallBack;
 	int s32Ret = IVA_Create(&iva);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("IVA_Create failure:%#X", s32Ret);
		return -1;
	}
	RK_LOGI("========iva init success========\n");
	return 0;
}

int deinit_iva(void)
{
    if( strlen( pIVAModelPath ) <= 3 ){
        RK_LOGI("iva not work, return\n");
        return -1;  
    }
	RK_LOGI("========begin deinit iva========\n");
	SAMPLE_COMM_IVA_Destroy(&iva);
	return 0;
}


int vi_iva_data( VIDEO_FRAME_INFO_S* stViFrame ) 
{
	static int u32Loopcount = 0;
	RK_S32 s32Ret = RK_FAILURE;
	RockIvaImage ivaImage;
	int s32Fd = 0;

	gettimeofday(&start_time, NULL);

	u32Loopcount++;
	s32Fd = RK_MPI_MB_Handle2Fd(stViFrame->stVFrame.pMbBlk);
	memset(&ivaImage, 0, sizeof(RockIvaImage));
	ivaImage.info.transformMode = iva.eImageTransform;
	ivaImage.info.width = stViFrame->stVFrame.u32Width;
	ivaImage.info.height = stViFrame->stVFrame.u32Height;
	ivaImage.info.format = iva.eImageFormat;
	ivaImage.frameId = u32Loopcount;
	ivaImage.dataAddr = NULL;
	ivaImage.dataPhyAddr = NULL;
	ivaImage.dataFd = s32Fd;
	ivaImage.extData = stViFrame;
	s32Ret = ROCKIVA_PushFrame( iva.ivahandle, &ivaImage, NULL);
	return 0;
}



