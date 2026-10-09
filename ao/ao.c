#include "ao.h"

static void *AO_task(void *arg) {
	printf("========%s========\n", __func__);

	RK_U8 *srcData = RK_NULL;
	RK_S32 srcSize = 0;
	RK_S32 pktEos = 0;
	RK_U64 timeStamp = 0;
	RK_S32 count = 0;
	AUDIO_STREAM_S stAudioStream;

	while (!quit) {

		sem_wait( ipc_shm[SHM_RAUD].semid );
		if( quit )
			break;
		/* Send a frame to decoder. */
		srcData = (uint8_t *)( ipc_shm[SHM_RAUD].ptr + 4);
		srcSize = *( (unsigned int*)( ipc_shm[SHM_RAUD].ptr ) );
		if (srcSize == 0 || srcData == RK_NULL) 
			continue;

		if (save_file && ( FILE_AO_G711 == rawFarmeRecType ) ) {
			fwrite(srcData, srcSize, 1, save_file);
			fflush(save_file);
		}	

		if( g_debug && ( FILE_AO_G711 == rawFarmeRecType )  )
			RK_LOGI("AUDIO--ao get size %d", srcSize);

		if (pktEos) {
			RK_MPI_ADEC_SendEndOfStream(0, RK_FALSE);
			break;
		} else {
			stAudioStream.u32Len = srcSize;
			stAudioStream.u64TimeStamp = timeStamp;
			stAudioStream.u32Seq = ++count;
			stAudioStream.bBypassMbBlk = RK_TRUE;
			MB_EXT_CONFIG_S extConfig = {0};
			extConfig.pFreeCB = NULL;
			extConfig.pOpaque = srcData;
			extConfig.pu8VirAddr = srcData;
			extConfig.u64Size = srcSize;
			RK_MPI_SYS_CreateMB(&(stAudioStream.pMbBlk), &extConfig);
		__RETRY:
			int ret = RK_MPI_ADEC_SendStream(0, &stAudioStream, RK_TRUE);
			if (ret != RK_SUCCESS) {
				RK_LOGE("fail to send adec stream.");
				goto __RETRY;
			}
			RK_MPI_MB_ReleaseMB(stAudioStream.pMbBlk);
		}
		timeStamp++;
	}

	if (!quit)
		RK_MPI_AO_WaitEos(0, 0, -1);

	RK_LOGW("audio output task exit");
	return NULL;
}

RK_S32 ao_set_other(RK_S32 s32SetVolume) {
	printf("\n=======%s=======\n", __func__);
	int s32DevId = 0;
	RK_S32 volume = 0;

	RK_MPI_AO_SetVolume(s32DevId, s32SetVolume);
	RK_MPI_AO_GetVolume(s32DevId, &volume);
	RK_LOGI("AO get volume = %d", volume);

	AUDIO_TRACK_MODE_E trackMode;
	RK_MPI_AO_GetTrackMode(s32DevId, &trackMode);
	RK_LOGI("AO get track mode = %d", trackMode);

	return 0;
}

RK_S32 init_ao_vqe( void) {
    RK_S32 ret = RK_SUCCESS;
	AO_VQE_CONFIG_S vqe_config;
	memset(&vqe_config, 0, sizeof(AO_VQE_CONFIG_S));
	const char *pVqeCfgPath = aovqePath ;

	if (pVqeCfgPath != RK_NULL) {
		vqe_config.enCfgMode = AIO_VQE_CONFIG_LOAD_FILE;
		if (strlen(pVqeCfgPath) < sizeof(vqe_config.aCfgFile) - 1)
			memcpy(vqe_config.aCfgFile, pVqeCfgPath, strlen(pVqeCfgPath));
		else {
			RK_LOGE("ao copy vqe json fail, aoChn = %d, strlen json = %d, max len = %d",
				 	0, strlen(pVqeCfgPath), sizeof(vqe_config.aCfgFile));
			return -1;
		}
	}

	RK_LOGD("enCfgMode = %d", vqe_config.enCfgMode);
	ret = RK_MPI_AO_SetVqeAttr(0, 0, &vqe_config);
	if (ret) {
		RK_LOGE("ao set vqe attr fail, aoChn = %d, reason = %X", 0, ret);
		return ret;
	}

	ret = RK_MPI_AO_EnableVqe(0, 0);
	if (ret) {
		RK_LOGE("ao enable vqe fail, aoChn = %d, reason = %X", 0, ret);
		return ret;
	}

    return RK_SUCCESS;
}

RK_S32 open_device_ao(	RK_S32 s32SampleRate ) {
						
	printf("\n=======%s=======\n", __func__);
	RK_S32 result = 0;
	RK_S32 channel = 1;
	AUDIO_DEV aoDevId = 0;
	AO_CHN aoChn = 0;
	AIO_ATTR_S aoAttr;
	AO_CHN_PARAM_S pstParams;
	RK_S32 u32FrameCnt = s32SampleRate * 2 / 50;

	memset(&pstParams, 0, sizeof(AO_CHN_PARAM_S));
	memset(&aoAttr, 0, sizeof(AIO_ATTR_S));
	sprintf((char *)aoAttr.u8CardName, "%s", "default");
	//sprintf((char *)aoAttr.u8CardName, "%s", "hw:0,0");
	//sprintf((char *)aoAttr.u8CardName, "%s", "default:CARD=rockchiprv1126b");

	aoAttr.soundCard.channels = 2;
	aoAttr.soundCard.sampleRate = s32SampleRate;
	aoAttr.soundCard.bitWidth = AUDIO_BIT_WIDTH_16;
	aoAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
	aoAttr.enSamplerate = (AUDIO_SAMPLE_RATE_E)s32SampleRate;

	if (channel == 1)
		aoAttr.enSoundmode = AUDIO_SOUND_MODE_MONO;
	else if (channel == 2)
		aoAttr.enSoundmode = AUDIO_SOUND_MODE_STEREO;
	else {
		RK_LOGE("unsupport = %d", channel);
		return RK_FAILURE;
	}

	aoAttr.u32PtNumPerFrm = u32FrameCnt;
	//以下参数没有特殊需要，无需修改
	aoAttr.u32FrmNum = 4;
	aoAttr.u32EXFlag = 0;
	aoAttr.u32ChnCnt = 2;

	RK_MPI_AO_SetPubAttr(aoDevId, &aoAttr);
	RK_MPI_AO_Enable(aoDevId);
	/*==============================================================================*/
	pstParams.enLoopbackMode = AUDIO_LOOPBACK_NONE;
	result = RK_MPI_AO_SetChnParams(aoDevId, aoChn, &pstParams);
	if (result != RK_SUCCESS) {
		RK_LOGE("ao set channel params, aoChn = %d", aoChn);
		return RK_FAILURE;
	}

	if (channel == 1)
		RK_MPI_AO_SetTrackMode(aoDevId, AUDIO_TRACK_OUT_STEREO);
	else
		RK_MPI_AO_SetTrackMode(aoDevId, AUDIO_TRACK_NORMAL);

	if (init_ao_vqe()) {
		RK_LOGE("ao enable vqe fail, aoChn = %d, reason = %x", aoChn, result);
	}		
	/*==============================================================================*/
	result = RK_MPI_AO_EnableChn(aoDevId, aoChn);
	if (result != 0) {
		RK_LOGE("ao enable channel fail, aoChn = %d, reason = %x", aoChn, result);
		return RK_FAILURE;
	}
	/*==============================================================================*/
	// set sample rate of input data
	result = RK_MPI_AO_EnableReSmp(aoDevId, aoChn, (AUDIO_SAMPLE_RATE_E)s32SampleRate);
	if (result != 0) {
		RK_LOGE("ao enable channel fail, reason = %x, aoChn = %d", result, aoChn);
		return RK_FAILURE;
	}

	ao_set_other( aoVolume );

	return RK_SUCCESS;
}

RK_S32 init_mpi_adec(void) {
	printf("\n=======%s=======\n", __func__);
	RK_S32 s32ret = 0;
	ADEC_CHN AdChn = 0;
	ADEC_CHN_ATTR_S pstChnAttr;
	memset(&pstChnAttr, 0, sizeof(ADEC_CHN_ATTR_S));

	pstChnAttr.stCodecAttr.enType = (RK_CODEC_ID_E)RK_AUDIO_ID_PCM_ALAW;
	pstChnAttr.stCodecAttr.u32Channels = 1; // default 1
	pstChnAttr.stCodecAttr.u32SampleRate = 8000;
	pstChnAttr.stCodecAttr.u32BitPerCodedSample = 4;

	pstChnAttr.enType = (RK_CODEC_ID_E)RK_AUDIO_ID_PCM_ALAW;
	pstChnAttr.enMode = ADEC_MODE_STREAM; // ADEC_MODE_PACK
	pstChnAttr.u32BufCount = 4;
	pstChnAttr.u32BufSize = 50 * 1024;
	s32ret = RK_MPI_ADEC_CreateChn(AdChn, &pstChnAttr);
	if (s32ret) {
		RK_LOGE("create adec chn %d err:0x%x\n", AdChn, s32ret);
		s32ret = RK_FAILURE;
	}
	return s32ret;
}


static MPP_CHN_S ao_stSrcChn, ao_stDestChn;
pthread_t ao_main_thread;

RK_S32 init_ao( )
{
    if (open_device_ao( aoSampleRate )){
		RK_LOGE("open ao device failed\n");
		return -1;
	}
	if (init_mpi_adec()){
		RK_LOGE("init mpi audio dec device failed\n");
		return -2;
	}
	// adec bind ao
	ao_stSrcChn.enModId = RK_ID_ADEC;
	ao_stSrcChn.s32DevId = 0;
	ao_stSrcChn.s32ChnId = 0;
	ao_stDestChn.enModId = RK_ID_AO;
	ao_stDestChn.s32DevId = 0;
	ao_stDestChn.s32ChnId = 0;
	// 3. bind ADEC-AO
	RK_S32 ret = RK_MPI_SYS_Bind(&ao_stSrcChn, &ao_stDestChn);
	if (ret) {
		RK_LOGE("Bind ADEC[0] to AO[0] failed! ret=%d\n", ret);
		return -1;
	}
	printf("%s ao initial finish\n", __func__);

    pthread_create(&ao_main_thread, NULL, AO_task, NULL);

    return 0;
}


RK_S32 deinit_ao()
{
    pthread_join(ao_main_thread, NULL);	

	RK_MPI_SYS_UnBind(&ao_stSrcChn, &ao_stDestChn);
	RK_MPI_AO_DisableVqe(0, 0);
	RK_MPI_AO_DisableReSmp(0, 0);
	RK_MPI_AO_DisableChn(0, 0);
	RK_MPI_AO_Disable(0);
	RK_MPI_ADEC_DestroyChn(0);

	printf("%s ao deinit finish\n", __func__);

    return 0;
}


CircularBuffer ao_play_cb;
bool ao_playing = false;

void *ao_play_task(void *arg) {
    printf("ao pcm play task start\n");
    uint8_t read_data[FIXED_READ_LENGTH] ={0};
    uint8_t read_data_bk[FIXED_READ_LENGTH] ={0};    
    RK_U64 timeStamp = 0;
    AUDIO_FRAME_S frame;
	RK_S32 s32MilliSec = -1;
	RK_U64 i = 0;

    while( !quit ){
		ao_playing = true ;

		if( i > 30 ){
			printf("ao play task no data timeout\n");
			lws_audio_recv_t = 0;
			break;
		}
        RK_S32 ret = read_from_buffer(&ao_play_cb, read_data, FIXED_READ_LENGTH );
        if( ret == -1 ){
			i++;
            memset(read_data, 0, FIXED_READ_LENGTH );
        }else if( ret == -2 ){
			i++;
            memcpy(read_data, read_data_bk, FIXED_READ_LENGTH );
            if( g_debug )
                printf("repeat frame\n");
        }else{
			i=0;
			memcpy(read_data_bk, read_data, FIXED_READ_LENGTH );
		}
            
        frame.u32Len = FIXED_READ_LENGTH;
        frame.u64TimeStamp = timeStamp++;
        frame.enBitWidth = AUDIO_BIT_WIDTH_16;
        frame.enSoundMode = AUDIO_SOUND_MODE_MONO;
        frame.bBypassMbBlk = RK_FALSE;

        MB_EXT_CONFIG_S extConfig= {0};
        extConfig.pOpaque = read_data;
        extConfig.pu8VirAddr = read_data;
        extConfig.u64Size = FIXED_READ_LENGTH;
        RK_MPI_SYS_CreateMB(&(frame.pMbBlk), &extConfig);

        RK_S32 result = RK_MPI_AO_SendFrame(0, 0, &frame, s32MilliSec);
        if (result < 0) 
           RK_LOGE("send frame fail, result = %d", result);   
        RK_MPI_MB_ReleaseMB(frame.pMbBlk);
        if( g_debug ){
            printf( "#" );
            fflush(stdout); 
        }

        usleep( 1000 * 20 /2 );        
    }
	ao_playing = false;
    printf("ao pcm play task end\n");
 
    return NULL;
}

int ao_play_snd( enum snd_id snd )
{
	char conf_file_path[128] = {0};
	const char* file_name = NULL;
	switch( (int)snd ){
		case 0: break;
		case 1: file_name = "poweron.pcm";break;
		case 2: file_name = "poweroff.pcm";break;
		case 5: file_name = "ka.pcm";break;	
		case 10:file_name = "connectfailed.pcm";break;	
		case 11:file_name = "connected.pcm";break;	
		case 12:file_name = "connectfailed.pcm";break;	
		default: file_name = "di.pcm";break;			
	}
	if( file_name == NULL )
		return -1;

	sprintf(conf_file_path, "%s/%s", sndFileDir ,file_name );

	FILE* f = fopen( conf_file_path , "rb");
	if (f == NULL) {
   	 	RK_LOGE( "conf Failed to open %s", conf_file_path);
    	return -1;
	}
	#define BUFFER_SIZE 1024
    char buf[BUFFER_SIZE] = {0};
    int bytes_read = 0;
    while ((bytes_read = fread(buf, 1, BUFFER_SIZE, f)) > 0) {
        write_to_buffer( &ao_play_cb, (uint8_t*)buf, bytes_read ); 
    }
    // 3. 检查循环结束的原因
    if (ferror(f)) {
        RK_LOGE("read snd file %s, error", conf_file_path );
		return -2;
    }
	fclose(f);

	if( false == ao_playing ){
		pthread_t ao_thread;
		pthread_create(&ao_thread, NULL, ao_play_task, NULL);
		pthread_detach(ao_thread);  
	}
	return 0;
}

