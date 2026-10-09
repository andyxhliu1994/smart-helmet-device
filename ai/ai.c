#include "ai.h"

static void *AI_task(void *arg) {

	printf("========%s========\n", __func__);

	AUDIO_STREAM_S pstStream;
	RK_S32 eos = 0;
	memset( ipc_shm[SHM_G711].ptr, 0 , 4  );

	while (!quit) {
		int ret = RK_MPI_AENC_GetStream(0, &pstStream, -1);
		if (ret == RK_SUCCESS) {
			MB_BLK bBlk = pstStream.pMbBlk;
			RK_VOID *pstFrame = RK_MPI_MB_Handle2VirAddr(bBlk);
			RK_S32 frameSize = pstStream.u32Len;
			RK_U64 timeStamp = pstStream.u64TimeStamp;
			eos = (frameSize <= 0) ? 1 : 0;

			if (pstFrame) {

				if( g_debug && ( FILE_AI_G711 == rawFarmeRecType )  ){
					RK_LOGI("AUDIO-- get frame data = %p, size = %d, timeStamp = %llu", pstFrame,
							frameSize, timeStamp);
					if( eos )
						RK_LOGW("AUDIO-- get eos");						
				}

				if( frameSize > 0 ){
					memcpy( ipc_shm[SHM_G711].ptr, (void*)&frameSize, 4 ); //[0] = count
					memcpy( ipc_shm[SHM_G711].ptr + 4 , pstFrame, frameSize );
					sem_post( ipc_shm[SHM_G711].semid );
				}

				if ( g_rtsplive && g_rtsp_session ) {
					rtsp_tx_audio(g_rtsp_session, (const uint8_t*)pstFrame, frameSize, timeStamp);
					rtsp_do_event(g_rtsplive);
				}

				if ( save_file && ( FILE_AI_G711 == rawFarmeRecType ) ) {
					fwrite( pstFrame, frameSize, 1, save_file);
					fflush( save_file );
				}	

				RK_MPI_AENC_ReleaseStream(0, &pstStream);
			}
		} else 
			RK_LOGE("fail to get aenc frame.");
		
		if (eos) {
			RK_LOGI("get eos stream.");
			break;
		}
	}

	RK_LOGW("audio input task exit");
	sem_post( ipc_shm[SHM_G711].semid );
	return NULL;
}

static void *AI_PCM_task(void *arg) {

	AUDIO_FRAME_S frame;
    unsigned int i =0 ;
	int sock = -1;

	if( strlen( nngAiPcmTxPath ) >= 3 ){
		sock = nn_socket(AF_SP, NN_PUB);
		if (sock < 0) {
			RK_LOGE("nn socket create error.");
			return NULL;
		}
		if (nn_bind(sock, nngAiPcmTxPath) < 0) {
			RK_LOGE("nn bind error.");
			nn_close(sock);
			return NULL;
		}	
		printf("pcm tx service start ipc:%s\n", nngAiPcmTxPath );
	}else
		printf("pcm tx service start\n" );

    while( !quit ){
		RK_S32 result = RK_MPI_AI_GetFrame(0, 0, &frame, RK_NULL, -1);
		if (result != RK_SUCCESS) 
            continue;

        void *data = RK_MPI_MB_Handle2VirAddr(frame.pMbBlk);
		if( sock > 0)
			nn_send(sock, data, frame.u32Len, 0);
     
		if( g_debug ){
			printf(".");fflush(stdout);
			i++;
			if( i % 50 == 0 )
				printf("\n");
		}

		if ( save_file && ( FILE_AI_PCM == rawFarmeRecType ) ) {
			fwrite( data, frame.u32Len, 1, save_file);
			fflush( save_file );
		}	
        RK_MPI_AI_ReleaseFrame(0, 0, &frame, RK_NULL);    
    }

    printf("pcm tx service exit\n");
	nn_close(sock);
	return NULL;
}


/*************************************************** */
RK_S32 ai_set_other(RK_S32 s32SetVolume) {
	printf("\n=======%s=======\n", __func__);
	int s32DevId = 0;

	RK_MPI_AI_SetVolume(s32DevId, s32SetVolume);

	AUDIO_TRACK_MODE_E trackMode;
	RK_MPI_AI_GetTrackMode(s32DevId, &trackMode);
	RK_LOGI("test info : get track mode = %d", trackMode);
	return 0;
}

RK_S32 init_ai_vqe(RK_S32 s32SampleRate) {

	AI_VQE_CONFIG_S stAiVqeConfig, stAiVqeConfig2;
	RK_S32 result;
	RK_S32 s32VqeGapMs = 16;
	int s32DevId = 0;
	int s32ChnIndex = 0;
	const char *pVqeCfgPath = aivqePath;

	// Need to config enCfgMode to VQE attr even the VQE is not enabled
	memset(&stAiVqeConfig, 0, sizeof(AI_VQE_CONFIG_S));
	if (pVqeCfgPath != RK_NULL) {
		stAiVqeConfig.enCfgMode = AIO_VQE_CONFIG_LOAD_FILE;
		memcpy(stAiVqeConfig.aCfgFile, pVqeCfgPath, strlen(pVqeCfgPath));
	}

	if (s32VqeGapMs != 16 && s32VqeGapMs != 10) {
		RK_LOGE("Invalid gap: %d, just supports 16ms or 10ms for AI VQE", s32VqeGapMs);
		return RK_FAILURE;
	}

	stAiVqeConfig.s32WorkSampleRate = s32SampleRate;
	stAiVqeConfig.s32FrameSample = s32SampleRate * s32VqeGapMs / 1000;
	result = RK_MPI_AI_SetVqeAttr(s32DevId, s32ChnIndex, 0, 0, &stAiVqeConfig);
	if (result != RK_SUCCESS) {
		RK_LOGE("%s: SetVqeAttr(%d,%d) failed with %#x", __FUNCTION__, s32DevId,
		        s32ChnIndex, result);
		return result;
	}

	result = RK_MPI_AI_GetVqeAttr(s32DevId, s32ChnIndex, &stAiVqeConfig2);
	if (result != RK_SUCCESS) {
		RK_LOGE("%s: SetVqeAttr(%d,%d) failed with %#x", __FUNCTION__, s32DevId,
		        s32ChnIndex, result);
		return result;
	}

	result = memcmp(&stAiVqeConfig, &stAiVqeConfig2, sizeof(AI_VQE_CONFIG_S));
	if (result != RK_SUCCESS) {
		RK_LOGE("%s: set/get vqe config is different: %d", __FUNCTION__, result);
		return result;
	}

	result = RK_MPI_AI_EnableVqe(s32DevId, s32ChnIndex);
	if (result != RK_SUCCESS) {
		RK_LOGE("%s: EnableVqe(%d,%d) failed with %#x", __FUNCTION__, s32DevId,
		        s32ChnIndex, result);
		return result;
	}

	return RK_SUCCESS;
}


RK_S32 open_device_ai(RK_S32 InputSampleRate, RK_S32 OutputSampleRate ) {
	printf("\n=======%s=======\n", __func__);
	AIO_ATTR_S aiAttr;
	AI_CHN_PARAM_S pstParams;
	RK_S32 result;
	int aiDevId = 0;
	int aiChn = 0;
	memset(&aiAttr, 0, sizeof(AIO_ATTR_S));

	RK_BOOL needResample = (InputSampleRate != OutputSampleRate) ? RK_TRUE : RK_FALSE;
	sprintf((char *)aiAttr.u8CardName, "%s", "hw:0,0");
	//sprintf((char *)aiAttr.u8CardName, "%s", "default");
	//sprintf((char *)aiAttr.u8CardName, "%s", "default:CARD=rockchiprv1126b");

	// s32DeviceSampleRate 和 OutputSampleRate,OutputSampleRate
	// 可以使用其他采样率，需要调用重采样函数。默认一样采样率。
	aiAttr.soundCard.channels = 2;
	aiAttr.soundCard.sampleRate = InputSampleRate;
	aiAttr.soundCard.bitWidth = AUDIO_BIT_WIDTH_16;
	aiAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
	aiAttr.enSamplerate = (AUDIO_SAMPLE_RATE_E)OutputSampleRate;
	aiAttr.enSoundmode = AUDIO_SOUND_MODE_MONO;
	aiAttr.u32PtNumPerFrm = OutputSampleRate * 2 /50;
	//以下参数无特殊需求，无需变动，保持默认值即可
	aiAttr.u32FrmNum = 4;
	aiAttr.u32EXFlag = 0;
	aiAttr.u32ChnCnt = 2;

	result = RK_MPI_AI_SetPubAttr(aiDevId, &aiAttr);
	if (result != 0) {
		RK_LOGE("ai set attr fail, reason = %d", result);
		goto __FAILED;
	}

	result = RK_MPI_AI_Enable(aiDevId);
	if (result != 0) {
		RK_LOGE("ai enable fail, reason = %d", result);
		goto __FAILED;
	}

	memset(&pstParams, 0, sizeof(AI_CHN_PARAM_S));
	pstParams.enLoopbackMode = AUDIO_LOOPBACK_NONE;
	pstParams.s32UsrFrmDepth = 1;
	result = RK_MPI_AI_SetChnParam(aiDevId, aiChn, &pstParams);
	if (result != RK_SUCCESS) {
		RK_LOGE("ai set channel params, aiChn = %d", aiChn);
		return RK_FAILURE;
	}

	init_ai_vqe(OutputSampleRate);

	//左声道，无需修改
	RK_MPI_AI_SetTrackMode(aiDevId, AUDIO_TRACK_FRONT_LEFT);
	result = RK_MPI_AI_EnableChn(aiDevId, aiChn);
	if (result != 0) {
		RK_LOGE("ai enable channel fail, aiChn = %d, reason = %x", aiChn, result);
		return RK_FAILURE;
	}

	if (needResample == RK_TRUE) {
		RK_LOGI("need to resample %d -> %d", InputSampleRate, OutputSampleRate);
		result = RK_MPI_AI_EnableReSmp(aiDevId, aiChn, (AUDIO_SAMPLE_RATE_E)OutputSampleRate);
		if (result != 0) {
			RK_LOGE("ai enable channel fail, reason = %x, aiChn = %d", result, aiChn);
			return RK_FAILURE;
		}
	}

	ai_set_other( aiVolume );

	return RK_SUCCESS;
__FAILED:
	return RK_FAILURE;
}

RK_S32 init_mpi_aenc(RK_S32 s32SampleRate ) {
	printf("\n=======%s=======\n", __func__);
	RK_S32 s32ret = 0;
	AENC_CHN_ATTR_S pstChnAttr;
	RK_CODEC_ID_E enCodecId = (RK_CODEC_ID_E)RK_AUDIO_ID_PCM_ALAW;
	AUDIO_BIT_WIDTH_E enBitwidth = AUDIO_BIT_WIDTH_16;
	printf("codecId=%d\n", (int)enCodecId);

	memset(&pstChnAttr, 0, sizeof(AENC_CHN_ATTR_S));
	pstChnAttr.stCodecAttr.enType = enCodecId;
	pstChnAttr.stCodecAttr.u32Channels = 1; // default MONO
	pstChnAttr.stCodecAttr.u32SampleRate = s32SampleRate;
	pstChnAttr.stCodecAttr.enBitwidth = enBitwidth;
	pstChnAttr.stCodecAttr.pstResv = RK_NULL;
	pstChnAttr.enType = enCodecId;
	pstChnAttr.u32BufCount = 4;
	pstChnAttr.u32Depth    = 4;

	s32ret = RK_MPI_AENC_CreateChn(0, &pstChnAttr);
	if (s32ret) {
		RK_LOGE("create aenc chn %d err:0x%x\n", enCodecId, s32ret);
	}
	return s32ret;
}


static MPP_CHN_S ai_stSrcChn, ai_stDestChn;  
pthread_t ai_main_thread;
pthread_t ai_pcm_thread;

RK_S32 init_ai( RK_S32 u32SampleRate_in )
{
	if (open_device_ai( u32SampleRate_in, u32SampleRate_in )){
		RK_LOGE("open ai device failed\n");
		return -1;
	}
	if (init_mpi_aenc( 8000 )){
		RK_LOGE("init mpi ai enc failed\n");
		return -2;
	}
	// ai bind aenc
	ai_stSrcChn.enModId = RK_ID_AI;
	ai_stSrcChn.s32DevId = 0;
	ai_stSrcChn.s32ChnId = 0;
	ai_stDestChn.enModId = RK_ID_AENC;
	ai_stDestChn.s32DevId = 0;
	ai_stDestChn.s32ChnId = 0;
	// 3. bind AI-AENC
	RK_S32 ret = RK_MPI_SYS_Bind(&ai_stSrcChn, &ai_stDestChn);
	if (ret) {
		RK_LOGE("Bind AI[0] to AENC[0] failed! ret=%d\n", ret);
		return -1;
	}
	RK_LOGI("%s ai initial finish\n", __func__);

    pthread_create(&ai_main_thread, NULL, AI_task, NULL);
	/******* */
	pthread_create(&ai_pcm_thread,  NULL, AI_PCM_task, NULL);
	pthread_detach(ai_pcm_thread);

    return 0;
}

RK_S32 deinit_ai()
{
	pthread_join(ai_main_thread, NULL);
	sem_post( ipc_shm[SHM_RAUD].semid );

    RK_MPI_SYS_UnBind(&ai_stSrcChn, &ai_stDestChn);
	RK_MPI_AI_DisableVqe(0, 0);
	RK_MPI_AI_DisableChn(0, 0);
	RK_MPI_AI_Disable(0);
	RK_MPI_AENC_DestroyChn(0);

	printf("%s ai deinit finish\n", __func__);
    return 0;
}


