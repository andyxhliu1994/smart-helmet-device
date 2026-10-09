#include "ivs.h"


void *IVS_task(void *arg) {
	struct ivs_info *info = (struct ivs_info *)arg;
	printf("========%s========\n", __func__);
	int loopCount = 0;
	int s32Ret;
	IVS_RESULT_INFO_S stResults;
	RK_U32 u32SquarePct[5] = {50, 30, 25, 20, 20};
	//int aaa = 0;
	bool move_detect = false;

	while (!quit) {
		memset(&stResults, 0, sizeof(IVS_RESULT_INFO_S));
		s32Ret = RK_MPI_IVS_GetResults(0, &stResults, -1);
		if (s32Ret == RK_SUCCESS) {
			if (stResults.s32ResultNum == 1) {
				for (RK_U32 i = 0; i < stResults.pstResults->stMdInfo.u32RectNum; i++) {
					if( !g_debug )
						continue;
					printf("%d: [%d, %d, %d, %d]\n", i,
					       stResults.pstResults->stMdInfo.stRect[i].s32X,
					       stResults.pstResults->stMdInfo.stRect[i].s32Y,
					       stResults.pstResults->stMdInfo.stRect[i].u32Width,
					       stResults.pstResults->stMdInfo.stRect[i].u32Height);
					printf("u32Square %u, u32DetAreaSquare %u, u32DetOutputSquare %u\n",
					       stResults.pstResults->stMdInfo.u32Square,
					       stResults.pstResults->stMdInfo.u32DetAreaSquare,
					       stResults.pstResults->stMdInfo.u32DetOutputSquare);
				}
				if (1000 * stResults.pstResults->stMdInfo.u32Square / info->u32Width / info->u32Height >
					 u32SquarePct[info->u32Sensitivity]){
						if( false == move_detect ){
							move_detect = true;
							RK_LOGI("movement start ...");
						}
						loopCount = 0;
					 }
					
				if (stResults.pstResults->stOdInfo.u32Flag)
					RK_LOGI("Detect occlusion!");
			}
			RK_MPI_IVS_ReleaseResults(0, &stResults);
		} else 
			RK_LOGE("RK_MPI_IVS_GetResults fail %x", s32Ret);

		/************************************* */
		loopCount++;
		if(( move_detect == true )&&( loopCount >= mainStreamFps )){
			move_detect = false;
			loopCount = 0;
			RK_LOGI("movement end");
		}

	}
	RK_LOGW("ivs task exit");
	return NULL;
}

static RK_S32 create_ivs(int width, int height, RK_U32 u32Sensitivity) {
	RK_S32 s32Ret;
	IVS_CHN_ATTR_S attr;
	memset(&attr, 0, sizeof(attr));
	attr.enMode = IVS_MODE_MD_OD;
	attr.u32PicWidth = width;
	attr.u32PicHeight = height;
	attr.enPixelFormat = RK_FMT_YUV420SP;
	attr.s32Gop = 30;
	attr.bSmearEnable = RK_FALSE;
	attr.bWeightpEnable = RK_FALSE;
	attr.bMDEnable = RK_TRUE;
	attr.s32MDInterval = 5;
	attr.bMDNightMode = RK_FALSE;
	attr.bODEnable = RK_TRUE;
	attr.s32ODInterval = 1;
	attr.s32ODPercent = 7;
	if (1) {
		attr.stDetAttr.stDetArea.u32AreaNum = 1;
		attr.stDetAttr.stDetArea.areas[0].u32PointNum = 4;
		attr.stDetAttr.stDetArea.areas[0].points[0].s32X = width / 2;
		attr.stDetAttr.stDetArea.areas[0].points[0].s32Y = 0;
		attr.stDetAttr.stDetArea.areas[0].points[1].s32X = width;
		attr.stDetAttr.stDetArea.areas[0].points[1].s32Y = height / 2;
		attr.stDetAttr.stDetArea.areas[0].points[2].s32X = width / 2;
		attr.stDetAttr.stDetArea.areas[0].points[2].s32Y = height;
		attr.stDetAttr.stDetArea.areas[0].points[3].s32X = 0;
		attr.stDetAttr.stDetArea.areas[0].points[3].s32Y = height / 2;
	}

	s32Ret = RK_MPI_IVS_CreateChn(0, &attr);
	if (s32Ret) {
		RK_LOGE("ivs create failed:%x", s32Ret);
		goto __FAILED;
	}

	IVS_MD_ATTR_S stMdAttr;
	memset(&stMdAttr, 0, sizeof(stMdAttr));
	s32Ret = RK_MPI_IVS_GetMdAttr(0, &stMdAttr);
	if (s32Ret) {
		RK_LOGE("ivs get mdattr failed:%x", s32Ret);
		goto __FAILED;
	}
	switch (u32Sensitivity) {
		case 0:
			stMdAttr.s32ThreshSad = 96;stMdAttr.s32ThreshMove = 3;stMdAttr.s32SwitchSad = 2;break;
		case 1:
			stMdAttr.s32ThreshSad = 72;stMdAttr.s32ThreshMove = 2;stMdAttr.s32SwitchSad = 2;break;
		case 2:
			stMdAttr.s32ThreshSad = 64;stMdAttr.s32ThreshMove = 2;stMdAttr.s32SwitchSad = 2;break;
		case 3:
			stMdAttr.s32ThreshSad = 48;stMdAttr.s32ThreshMove = 1;stMdAttr.s32SwitchSad = 2;break;
		case 4:
			stMdAttr.s32ThreshSad = 32;stMdAttr.s32ThreshMove = 1;stMdAttr.s32SwitchSad = 0;break;
		default:
			stMdAttr.s32ThreshSad = 64;stMdAttr.s32ThreshMove = 2;stMdAttr.s32SwitchSad = 2;break;
	}
	stMdAttr.bFlycatkinFlt = RK_TRUE;
	stMdAttr.s32ThresDustMove = 3;
	stMdAttr.s32ThresDustBlk = 3;
	stMdAttr.s32ThresDustChng = 50;
	s32Ret = RK_MPI_IVS_SetMdAttr(0, &stMdAttr);
	if (s32Ret) {
		RK_LOGE("ivs set mdattr failed:%x", s32Ret);
		goto __FAILED;
	}
	return 0;
__FAILED:
	return -1;
}

static MPP_CHN_S vi_stSrcChn, ivs_stIvsChn ;
pthread_t ivs_main_thread;
struct ivs_info ivs_info;

RK_S32 init_ivs( int width, int height, RK_U32 u32Sensitivity ){

    create_ivs( width, height, u32Sensitivity);

	vi_stSrcChn.enModId = RK_ID_VI;
	vi_stSrcChn.s32DevId = 0;
	vi_stSrcChn.s32ChnId = 0;
	ivs_stIvsChn.enModId = RK_ID_IVS;
	ivs_stIvsChn.s32DevId = 0;
	ivs_stIvsChn.s32ChnId = 0;

	printf("====RK_MPI_SYS_Bind vi0 to ivs====\n");
	RK_S32 s32Ret = RK_MPI_SYS_Bind(&vi_stSrcChn, &ivs_stIvsChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("bind 0 ch ivs failed");
		return -1;
	}
	printf("%s ivs initial finish\n", __func__);	

 	ivs_info.u32Width = width;
 	ivs_info.u32Height = height;	
 	ivs_info.u32Sensitivity = u32Sensitivity;		
	//pthread_create(&ivs_main_thread, NULL, IVS_task, &ivs_info);	
    
	return 0;
}

RK_S32 deinit_ivs(){
    //pthread_join(ivs_main_thread, NULL);
	RK_MPI_SYS_UnBind(&vi_stSrcChn, &ivs_stIvsChn);
	RK_MPI_IVS_DestroyChn(0);

	printf("%s ivs deinit finish\n", __func__);

	return 0;
}
