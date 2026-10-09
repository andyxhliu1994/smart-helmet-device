#include "vi.h"
#include "rtmp.h"

extern int vi_rknn_data( uint32_t width, uint32_t height, void* data );
extern int vi_iva_data( VIDEO_FRAME_INFO_S* stViFrame ) ;

bool take_photo_one = false;

void *VI_task(void *arg) {
	(void)arg;
	printf("========%s========\n", __func__);
	void *pData = RK_NULL;
	int loopCount = 0;
	int s32Ret;
	int rtmp_en = ( strlen( rtmpServerUrl ) <=3 )? 0: 1;
	int rtmp_send_fail_t = 0;

	VENC_STREAM_S stFrame;
	stFrame.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S));
	memset( ipc_shm[SHM_H264].ptr, 0, 20 );

	while (!quit) {
		
		s32Ret = RK_MPI_VENC_GetStream( H264_VENC_CHN, &stFrame, -1);
		if (s32Ret == RK_SUCCESS) {
			pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);

			uint32_t d_len = stFrame.pstPack->u32Len;
			memcpy( ipc_shm[SHM_H264].ptr, (void*)&d_len, 4 ); //
			memcpy( ipc_shm[SHM_H264].ptr + 4, pData, d_len );
			sem_post( ipc_shm[SHM_H264].semid );

			if ( g_rtsplive && g_rtsp_session ) {
				rtsp_tx_video(g_rtsp_session, (const uint8_t*)pData, d_len, stFrame.pstPack->u64PTS );
				rtsp_do_event(g_rtsplive);
			}

			if( save_file && ( FILE_VI_H264 == rawFarmeRecType ) ) {
				fwrite( pData, d_len, 1, save_file);
				fflush(save_file);
			}

			if( g_debug && ( FILE_VI_H264 == rawFarmeRecType ) ){
				RK_U64 nowUs = TEST_COMM_GetNowUs();
				RK_LOGI("VIDEO-- chn:0, loopCount:%d enc->seq:%d size:%d pts=%lld delay=%lldus\n",
						loopCount, stFrame.u32Seq, stFrame.pstPack->u32Len,
						stFrame.pstPack->u64PTS, nowUs - stFrame.pstPack->u64PTS);
			}

			if( rtmp_en ){
				if ((stFrame.pstPack->DataType.enH264EType == H264E_NALU_IDRSLICE) ||
					(stFrame.pstPack->DataType.enH264EType == H264E_NALU_ISLICE) ||
					(stFrame.pstPack->DataType.enH265EType == H265E_NALU_IDRSLICE) ||
					(stFrame.pstPack->DataType.enH265EType == H265E_NALU_ISLICE)) {
					// rk_storage_write_video_frame(0, data, stFrame.pstPack->u32Len,
					//                              stFrame.pstPack->u64PTS, 1);
					int ret = rtmp_write_video_frame(0, (unsigned char*)pData, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,1);		
					if( ret != 0 )
						rtmp_send_fail_t ++;
					else
						rtmp_send_fail_t = 0;
				} else {
					// rk_storage_write_video_frame(0, data, stFrame.pstPack->u32Len,
					//                              stFrame.pstPack->u64PTS, 0);
					int ret = rtmp_write_video_frame(0, (unsigned char*)pData, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,0);	
					if( ret != 0 )
						rtmp_send_fail_t ++;
					else
						rtmp_send_fail_t = 0;								
				}

				if( rtmp_send_fail_t >= mainStreamFps * 10 ){
					RK_LOGE("RTMP send frame error ");
					quit = 1;
					break;
				}
			}

			s32Ret = RK_MPI_VENC_ReleaseStream( H264_VENC_CHN, &stFrame);
			if (s32Ret != RK_SUCCESS) 
				RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x", s32Ret);
			loopCount++;
		} else 
			RK_LOGE("RK_MPI_VI_GetChnFrame fail %x", s32Ret);
	}

	free(stFrame.pstPack);
	sem_post( ipc_shm[SHM_H264].semid );
	RK_LOGW("vi task exit");
	
	return NULL;
}

void *VI_det_task(void *arg) {

	printf("==============Start %s thread===============\n", __func__ );
	prctl(PR_SET_NAME, "RkipcGetVi2", 0, 0, 0);
	int ret;
	int32_t loopCount = 0;
	VIDEO_FRAME_INFO_S stViFrame;
	int npu_cycle_time_ms = 1000 / ((mainStreamFps>=15)?15:mainStreamFps) ;

	long long before_time, cost_time;
	while (!quit) {
		before_time = rkipc_get_curren_time_ms();
		
		ret = RK_MPI_VI_GetChnFrame(0, IVA_CHN_ID, &stViFrame, 1000);
		if (ret == RK_SUCCESS) {
			
			void *pData = RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);
			// 1126b 32bit rga only support fd
			uint16_t w = stViFrame.stVFrame.u32Width;
			uint16_t h = stViFrame.stVFrame.u32Height;

			if(      strlen( pRKnnModelPath) >= 3 )	
				vi_rknn_data( w, h, pData );
			else if( strlen( pIVAModelPath ) >= 3 )
				vi_iva_data( &stViFrame );
			
			if (save_file && ( FILE_VI_RAW == rawFarmeRecType ) ) {
				fwrite( pData, (uint32_t)(w*h*1.5), 1, save_file);
				fflush( save_file );
			}

			cost_time = rkipc_get_curren_time_ms() - before_time;
			if ((cost_time > 0) && (cost_time < npu_cycle_time_ms))
				usleep((npu_cycle_time_ms - cost_time) * 1000);			
												 
			ret = RK_MPI_VI_ReleaseChnFrame(0, IVA_CHN_ID, &stViFrame);
			if (ret != RK_SUCCESS)
				RK_LOGE("RK_MPI_VI or VPSS_ReleaseChnFrame fail %x\n", ret);
			loopCount++;

		} else {
			RK_LOGE("RK_MPI_VI or VPSS_GetChnFrame timeout %x\n", ret);
			sleep(1);
		}
	}

	sem_post( obj_det_sem );
	RK_LOGW("iva task exit");
	return NULL;
}

void *VI_jpeg_task(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	VENC_STREAM_S stFrame;
	VI_CHN_STATUS_S stChnStatus;
	stFrame.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S));
	int ret = 0;
	bool first_jpeg = true;

	while ( !quit ) {
		usleep(300 * 1000);
		if( quit )
			break;
		if ((!take_photo_one )&&( first_jpeg == false ))
			continue;
		// 5.get the frame
		ret = RK_MPI_VENC_GetStream(JPEG_VENC_CHN, &stFrame, 1000);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);

			if( first_jpeg ){
				first_jpeg = false;
				RK_MPI_VENC_ReleaseStream(JPEG_VENC_CHN, &stFrame);
				continue;
			}
			// save jpeg file
			if( NULL == save_file ){
				time_t timestamp = time(NULL);
				sprintf( rawFarmeRecFileName, "%s-%ld.jpeg", devSn, timestamp );
				sprintf( rawFarmeRecPath, "%s/%s", recTmpFileDir, rawFarmeRecFileName );
				save_file = fopen( rawFarmeRecPath, "wb");
				if ( NULL == save_file) 
					RK_LOGE("ERROR: open file: %s fail, exit", rawFarmeRecPath);
				else{
					fwrite(data, 1, stFrame.pstPack->u32Len, save_file );
					fflush(save_file);
					fclose(save_file);				
					save_file = NULL;
					upload_lwsc( "upload_jpeg",  rawFarmeRecFileName, rawFarmeRecPath );
				}
			}else
				RK_LOGE("rec file is busying\n");

			take_photo_one = 0;
			// 7.release the frame
			ret = RK_MPI_VENC_ReleaseStream(JPEG_VENC_CHN, &stFrame);
			if (ret != RK_SUCCESS) 
				RK_LOGE("RK_MPI_VENC_ReleaseStream fail %x\n", ret);

		} else 
			RK_LOGE("RK_MPI_VENC_GetStream timeout %x\n", ret);
	}
	if (stFrame.pstPack)
		free(stFrame.pstPack);
	RK_LOGW("jpeg task exit");

	return NULL;
}


static RK_S32 venc_h264_init(int chnId, int width, int height, int fps ) {
						
	printf("========%s========\n", __func__);
	VENC_RECV_PIC_PARAM_S stRecvParam;
	VENC_CHN_ATTR_S stAttr;
	VENC_CHN_PARAM_S stParam;
	memset(&stAttr, 0, sizeof(VENC_CHN_ATTR_S));
	memset(&stParam, 0, sizeof(VENC_CHN_PARAM_S));

	stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
	stAttr.stRcAttr.stH264Cbr.u32BitRate = 10 * 1024;
	stAttr.stRcAttr.stH264Cbr.u32Gop = fps*2;
	stAttr.stVencAttr.enType = (RK_CODEC_ID_E)RK_VIDEO_ID_AVC;
	stAttr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	stAttr.stVencAttr.u32Profile = H264E_PROFILE_HIGH;
	stAttr.stVencAttr.u32PicWidth = width;
	stAttr.stVencAttr.u32PicHeight = height;
	stAttr.stVencAttr.u32VirWidth = width;
	stAttr.stVencAttr.u32VirHeight = height;
	stAttr.stVencAttr.u32StreamBufCnt = 2;
	stAttr.stVencAttr.u32BufSize = width * height * 3 / 2;
	stAttr.stVencAttr.enMirror = ispRotation ? MIRROR_HORIZONTAL: MIRROR_NONE;
	RK_MPI_VENC_CreateChn(chnId, &stAttr);

	stParam.stFrameRate.bEnable = RK_TRUE;
	stParam.stFrameRate.s32SrcFrmRateNum = 30;
	stParam.stFrameRate.s32SrcFrmRateDen = 1;
	stParam.stFrameRate.s32DstFrmRateNum = fps;
	stParam.stFrameRate.s32DstFrmRateDen = 1;
	RK_MPI_VENC_SetChnParam(chnId, &stParam);	

	if( ispRotation )
		RK_MPI_VENC_SetChnRotation( chnId, ROTATION_180 );

	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = -1;
	RK_MPI_VENC_StartRecvFrame(chnId, &stRecvParam);

	return 0;
}

int vi_dev_init() {
	printf("%s\n", __func__);
	int ret = 0;
	int devId = 0;
	int pipeId = devId;

	VI_DEV_ATTR_S stDevAttr;
	VI_DEV_BIND_PIPE_S stBindPipe;
	memset(&stDevAttr, 0, sizeof(stDevAttr));
	memset(&stBindPipe, 0, sizeof(stBindPipe));
	// 0. get dev config status
	ret = RK_MPI_VI_GetDevAttr(devId, &stDevAttr);
	if (ret == RK_ERR_VI_NOT_CONFIG) {
		ret = RK_MPI_VI_SetDevAttr(devId, &stDevAttr);
		if (ret != RK_SUCCESS) {
			printf("RK_MPI_VI_SetDevAttr %x\n", ret);
			return -1;
		}
	} else 
		printf("RK_MPI_VI_SetDevAttr already\n");

	// 1.get dev enable status
	ret = RK_MPI_VI_GetDevIsEnable(devId);
	if (ret != RK_SUCCESS) {
		// 1-2.enable dev
		ret = RK_MPI_VI_EnableDev(devId);
		if (ret != RK_SUCCESS) {
			printf("RK_MPI_VI_EnableDev %x\n", ret);
			return -1;
		}
		// 1-3.bind dev/pipe
		stBindPipe.u32Num = 1;
		stBindPipe.PipeId[0] = pipeId;
		ret = RK_MPI_VI_SetDevBindPipe(devId, &stBindPipe);
		if (ret != RK_SUCCESS) {
			printf("RK_MPI_VI_SetDevBindPipe %x\n", ret);
			return -1;
		}
	} else 
		printf("RK_MPI_VI_EnableDev already\n");

	// VI_ISP_MIRROR_FLIP_S stMirrFlip;
	// stMirrFlip.flip=1;
	// stMirrFlip.mirror=1;
	// RK_MPI_VI_SetChnMirrorFlip(0, 0, stMirrFlip);

	return 0;
}

int vi_chn_h264_init(int channelId, int width, int height) {
	int ret;
	// VI init
	VI_CHN_ATTR_S vi_chn_attr;
	memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
	//vi_chn_attr.bFlip = RK_TRUE;
	//vi_chn_attr.bMirror = RK_TRUE;
	vi_chn_attr.stIspOpt.u32BufCount = 2;
	vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
	vi_chn_attr.stSize.u32Width = width;
	vi_chn_attr.stSize.u32Height = height;
	vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
	vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE; // COMPRESS_AFBC_16x16;
	vi_chn_attr.u32Depth = 0; //0, get fail, 1 - u32BufCount, can get, if bind to other device, must be < u32BufCount
	ret = RK_MPI_VI_SetChnAttr(0, channelId, &vi_chn_attr);
	ret |= RK_MPI_VI_EnableChn(0, channelId);
	if (ret) {
		printf("ERROR: create VI error! ret=%d\n", ret);
		return ret;
	}
	return ret;
}

int vi_chn_iva_init(int channelId, int width, int height) {
	int ret;
	VI_CHN_ATTR_S vi_chn_attr;
	memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
	vi_chn_attr.stIspOpt.u32BufCount = 3;
	vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF; // VI_V4L2_MEMORY_TYPE_MMAP;
	vi_chn_attr.stIspOpt.stMaxSize.u32Width = 1920;
	vi_chn_attr.stIspOpt.stMaxSize.u32Height = 1080;
	vi_chn_attr.stSize.u32Width  = width;
	vi_chn_attr.stSize.u32Height = height;
	vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
	vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE; // COMPRESS_AFBC_16x16;
	vi_chn_attr.u32Depth = 1; //0, get fail, 1 - u32BufCount, can get, if bind to other device, must be < u32BufCount
	ret = RK_MPI_VI_SetChnAttr(0, channelId, &vi_chn_attr);
	ret |= RK_MPI_VI_EnableChn(0, channelId);
	if (ret) {
		printf("ERROR: create VI error! ret=%d\n", ret);
		return ret;
	}
	return ret;
}

int venc_jpeg_init( int width, int height ) {
	int ret;
	int video_width = width;
	int video_height = height;
	int rotation = ispRotation ? 180:0 ;
	// VENC[3] init
	VENC_CHN_ATTR_S jpeg_chn_attr;
	memset(&jpeg_chn_attr, 0, sizeof(jpeg_chn_attr));
	jpeg_chn_attr.stVencAttr.enType = RK_VIDEO_ID_JPEG;
	jpeg_chn_attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	jpeg_chn_attr.stVencAttr.u32MaxPicWidth = 2560;
	jpeg_chn_attr.stVencAttr.u32MaxPicHeight = 1440;
	jpeg_chn_attr.stVencAttr.u32PicWidth = video_width;
	jpeg_chn_attr.stVencAttr.u32PicHeight = video_height;
	jpeg_chn_attr.stVencAttr.u32VirWidth = video_width;
	jpeg_chn_attr.stVencAttr.u32VirHeight = video_height;
	jpeg_chn_attr.stVencAttr.u32StreamBufCnt = 2;
	jpeg_chn_attr.stVencAttr.u32BufSize = video_width*video_height*3/2;
	jpeg_chn_attr.stVencAttr.enMirror = ispRotation ? MIRROR_HORIZONTAL: MIRROR_NONE ;
	jpeg_chn_attr.stVencAttr.stAttrJpege.bSupportDCF = RK_FALSE;
	jpeg_chn_attr.stVencAttr.stAttrJpege.stMPFCfg.u8LargeThumbNailNum = 0;
	jpeg_chn_attr.stVencAttr.stAttrJpege.enReceiveMode = VENC_PIC_RECEIVE_SINGLE;
	// jpeg_chn_attr.stVencAttr.u32Depth = 1;
	ret = RK_MPI_VENC_CreateChn(JPEG_VENC_CHN, &jpeg_chn_attr);
	if (ret) {
		RK_LOGE("ERROR: create VENC error! ret=%d\n", ret);
		return -1;
	}
	VENC_JPEG_PARAM_S stJpegParam;
	memset(&stJpegParam, 0, sizeof(stJpegParam));
	stJpegParam.u32Qfactor = 70;
	RK_MPI_VENC_SetJpegParam(JPEG_VENC_CHN, &stJpegParam);
	if (rotation == 0) {
		RK_MPI_VENC_SetChnRotation(JPEG_VENC_CHN, ROTATION_0);
	} else if (rotation == 90) {
		RK_MPI_VENC_SetChnRotation(JPEG_VENC_CHN, ROTATION_90);
	} else if (rotation == 180) {
		RK_MPI_VENC_SetChnRotation(JPEG_VENC_CHN, ROTATION_180);
	} else if (rotation == 270) {
		RK_MPI_VENC_SetChnRotation(JPEG_VENC_CHN, ROTATION_270);
	}

	VENC_CHN_PARAM_S stParam;
	memset(&stParam, 0, sizeof(VENC_CHN_PARAM_S));
	stParam.stFrameRate.bEnable = RK_FALSE;
	stParam.stFrameRate.s32SrcFrmRateNum = mainStreamFps;
	stParam.stFrameRate.s32SrcFrmRateDen = 1;
	stParam.stFrameRate.s32DstFrmRateNum = 5;
	stParam.stFrameRate.s32DstFrmRateDen = 1;
	RK_MPI_VENC_SetChnParam(JPEG_VENC_CHN, &stParam);

	VENC_RECV_PIC_PARAM_S stRecvParam;
	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = 1;
	RK_MPI_VENC_StartRecvFrame(JPEG_VENC_CHN, &stRecvParam);
	// must, for no streams callback running failed

	return ret;
}

int venc_jpeg_deinit() {
	int ret = RK_MPI_VENC_StopRecvFrame(JPEG_VENC_CHN);
	   ret |= RK_MPI_VENC_DestroyChn(JPEG_VENC_CHN);
	if (ret)
		RK_LOGE("ERROR: Destroy VENC error! ret=%#x\n", ret);

	return ret;
}

static MPP_CHN_S vi_stSrcChn, venc_h264Chn, venc_JpegChn;
pthread_t vi_main_thread;
pthread_t iva_main_thread;
pthread_t jpeg_venc_thread;

RK_S32 init_vi( RK_U32 u32Width, RK_U32 u32Height, RK_U32 u32Fps )
{
	vi_dev_init();
	vi_chn_h264_init( 0, u32Width, u32Height);
	if ( strlen( pIVAModelPath) >= 3 )
		vi_chn_iva_init( IVA_CHN_ID, 640, 640 );	
	else if	( strlen( pRKnnModelPath) >= 3 ) 
		vi_chn_iva_init( IVA_CHN_ID, 640, 640 );	
	venc_h264_init( H264_VENC_CHN, u32Width, u32Height, u32Fps );   
	venc_jpeg_init( u32Width, u32Height ); 

	// bind vi to venc
	vi_stSrcChn.enModId = RK_ID_VI;
	vi_stSrcChn.s32DevId = 0;
	vi_stSrcChn.s32ChnId = 0;

	venc_h264Chn.enModId = RK_ID_VENC;
	venc_h264Chn.s32DevId = 0;
	venc_h264Chn.s32ChnId = H264_VENC_CHN;
	printf("====RK_MPI_SYS_Bind vi0 to venc0====\n");
	RK_S32 s32Ret = RK_MPI_SYS_Bind(&vi_stSrcChn, &venc_h264Chn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("bind 0 ch venc failed");
		return -1;
	}
	
	venc_JpegChn.enModId = RK_ID_VENC;
	venc_JpegChn.s32DevId = 0;
	venc_JpegChn.s32ChnId = JPEG_VENC_CHN;
	printf("====RK_MPI_SYS_Bind vi0 to venc1====\n");
	s32Ret = RK_MPI_SYS_Bind(&vi_stSrcChn, &venc_JpegChn);
	if (s32Ret != RK_SUCCESS) {
		RK_LOGE("bind 0 ch venc failed");
		return -1;
	}
	
	printf("%s vi initial finish\n", __func__);

    return 0;
}

RK_S32 deinit_vi()
{
	RK_MPI_SYS_UnBind(&vi_stSrcChn, &venc_h264Chn);
	RK_MPI_SYS_UnBind(&vi_stSrcChn, &venc_JpegChn);
	RK_MPI_VI_DisableChn(0, 0 );
	if( ( strlen( pIVAModelPath) >= 3 ) || ( strlen( pRKnnModelPath) >= 3 ) )
		RK_MPI_VI_DisableChn(0, IVA_CHN_ID );
	RK_MPI_VENC_StopRecvFrame(0);
	RK_MPI_VENC_DestroyChn(0);
	venc_jpeg_deinit();
	RK_MPI_VI_DisableDev(0);

	printf("%s vi deinit finish\n", __func__);
    return 0;
}


