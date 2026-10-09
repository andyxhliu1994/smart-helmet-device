#include "common.h"

FILE *save_file;
bool   quit = false;

char   *confPath = "./conf.json";
RK_U32 g_debug =0;
RK_U64 g_msgid = 0;

char devSn[64] = {0};
RK_U32 mainStreamWidth = 1920;
RK_U32 mainStreamHeight = 1080;
RK_U32 mainStreamFps = 15;	
RK_S32 aiSampleRate = 16000;
RK_S32 aoSampleRate = 16000;
RK_U32 u32Sensitivity = 1;
enum rec_file_t rawFarmeRecType = FILE_NULL;
char rawFarmeRecPath[128] = {0};	
char rawFarmeRecFileName[96] = {0};
RK_S32 aiVolume = 50;
RK_S32 aoVolume = 50;
char osdFontPath[64] = {0} ;

char aivqePath[64] = {0};
char aovqePath[64] = {0};
char pIVAModelPath[64] = {0};
char pRKnnModelPath[64] = {0};
char pRKnnModelLabelPath[64] = {0};
char nngAiPcmTxPath[64] = {0};
char rtspLocPath[64] = {0};
char rtmpServerUrl[128] = {0};
char httpServerUrl[128] = {0};

char wsServerAddr[64] = {0};
RK_U32 wsServerPort=0;
char wsCmdPath[64] = {0};
char wsVoicePath[64] = {0};

char recTmpFileDir[64] = {0};
char sndFileDir[64] = {0};

bool ispRotation = true;
bool keyEvent = false;
/**************************************************** */

pthread_mutex_t obj_det_update_lock;//info update互斥锁

struct shm_set ipc_shm[3];
sem_t* obj_det_sem;//用于目标检测输出Osd的通知
#define OBJ_DET_SEM_NAME "obj-det"

int init_shm_sem(void)
{
	ipc_shm[0].size = 1024*1024;
	strcpy( ipc_shm[0].name, "h264" );
	ipc_shm[1].size = 1024*10;
	strcpy( ipc_shm[1].name, "g711" );
	ipc_shm[2].size = 1024*10;
	strcpy( ipc_shm[2].name, "raud" );

	for( long unsigned int i =0 ;i < sizeof(ipc_shm)/sizeof(struct shm_set) ; i++ ){
		ipc_shm[i].fd = shm_open( ipc_shm[i].name ,
							O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
		if( ipc_shm[i].fd < 0 ){
			RK_LOGE("shm %s open error\n", ipc_shm[i].name );
			return -1;
		}						
		ftruncate(ipc_shm[i].fd, ipc_shm[i].size );

		ipc_shm[i].ptr = mmap(NULL, ipc_shm[i].size, PROT_READ | PROT_WRITE, 
								MAP_SHARED, ipc_shm[i].fd, 0);
		if (ipc_shm[i].ptr == MAP_FAILED) {
			RK_LOGE("shm %s map error\n", ipc_shm[i].name );
			return -1;
		}

		ipc_shm[i].semid = sem_open(ipc_shm[i].name, O_CREAT, 0644, 0); /* 创建信号量对象 */
		if ( ipc_shm[i].semid == SEM_FAILED ) {
			RK_LOGE("open semaphore %s error\n", ipc_shm[i].name );
			return -1;
		}
	}
	//obj det sem
	obj_det_sem = sem_open( OBJ_DET_SEM_NAME, O_CREAT, 0644, 0 );
	if( SEM_FAILED == obj_det_sem ){
		RK_LOGE("init obj det semaphore error\n");
		return -1;
	}
	return 0;
}

int deinit_shm_sem(void)
{
	for( long unsigned int i= 0;i < sizeof(ipc_shm)/sizeof(struct shm_set); i++ ){
		if( NULL != ipc_shm[i].ptr ){
			munmap(ipc_shm[i].ptr, ipc_shm[i].size); /* 取消内存映射 */
			close( ipc_shm[i].fd );
			//shm_unlink( ipc_shm[i].name );
			ipc_shm[i].ptr = NULL;
			sem_close( ipc_shm[i].semid);
			//sem_unlink( ipc_shm[i].name );
		}
	}
	sem_close( obj_det_sem );
	sem_unlink( OBJ_DET_SEM_NAME );

	return 0;
}

/**************************************************************************** */
void sigterm_handler(int sig) {
	fprintf(stderr, "signal %d\n", sig);
	quit = true;
	sem_post( ipc_shm[SHM_RAUD].semid );
}

/**************************************************************************** */
RK_U64 TEST_COMM_GetNowUs() {
	struct timespec time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &time);
    /* microseconds */
	return (RK_U64)time.tv_sec * 1000000 + (RK_U64)time.tv_nsec / 1000; 
}

double __get_us(struct timeval t) { return (t.tv_sec * 1000000 + t.tv_usec); }
/**************************************************************************** */

rtsp_demo_handle g_rtsplive = NULL;
rtsp_session_handle g_rtsp_session;

RK_S32 init_rtsp()
{
	if( strlen( rtspLocPath ) == 0 )
		return -1;

	g_rtsplive = create_rtsp_demo(554);
	g_rtsp_session = rtsp_new_session( g_rtsplive, rtspLocPath );
	rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
	rtsp_sync_video_ts(g_rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());
	rtsp_set_audio(g_rtsp_session, RTSP_CODEC_ID_AUDIO_G711A, NULL, 0);
	rtsp_sync_audio_ts(g_rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());
	rtsp_set_audio_sample_rate(g_rtsp_session,8000);
	rtsp_set_audio_channels(g_rtsp_session, 1);

	printf("rtsp init success\n"); 

    return 0;
}

RK_S32 deinit_rtsp()
{
	if (g_rtsplive){
		rtsp_del_demo(g_rtsplive);
		printf("rtsp deinit finish\n"); 
	}
    return 0;
}

RK_S32 upload_lwsc( char* cmd_str, char* file_name, char* file_path )
{
	cJSON *json = cJSON_CreateObject();
	cJSON_AddNumberToObject(json, "msgid", g_msgid++);
	cJSON_AddStringToObject(json, "cmd",  cmd_str );
	cJSON_AddStringToObject(json, "file", file_name );
	CURLcode res = upload_file( httpServerUrl, file_path );
	char res_str[32] = {0};
	if( CURLE_OK == res )
		cJSON_AddStringToObject(json, "msg", "success" );
	else{
		sprintf( res_str, "failed:%d", (int)res );
		cJSON_AddStringToObject(json, "msg", res_str );
	}
	char *json_string = cJSON_PrintUnformatted(json); // 获取格式化的 JSON 字符串
	printf("Generated JSON: %s\n", json_string);
	lws_send_message( wsi_cmd, json_string );

	cJSON_Delete(json); // 释放 json 对象占用的内存
	free(json_string);  // 释放 json_string 的内存
	return 0;
}


RK_S32 start_file_rec( enum rec_file_t rec_file_type )
{
	if( NULL != save_file ){
		printf("rec file busying \n");
		return -1;
	}
	rawFarmeRecType = rec_file_type;
	if( rawFarmeRecType == FILE_NULL ){
		printf("rec file type is null\n");
		return -2;
	}

	time_t timestamp = time(NULL);
	sprintf( rawFarmeRecFileName, "%s-%ld.", devSn, timestamp );
	if( FILE_AI_PCM == rawFarmeRecType )
		strcat( rawFarmeRecFileName, "pcm" );
	else if( FILE_VI_H264 == rawFarmeRecType ) 
		strcat( rawFarmeRecFileName, "h264" );
	sprintf( rawFarmeRecPath, "%s/%s", recTmpFileDir, rawFarmeRecFileName );

	save_file = fopen( rawFarmeRecPath, "wb");
	if ( NULL == save_file) {
		printf("ERROR: open file: %s fail, exit\n", rawFarmeRecPath);
		return -3;
	}
	printf("%s type is %d, recing... \n", rawFarmeRecPath, (int)rawFarmeRecType );

    return 0;
}

RK_S32 stop_file_rec(void)
{
	if( NULL == save_file ){
		printf("rec file is not work\n");
		return -1;		
	}
	fclose(save_file);
	save_file = NULL;

	struct stat file_info;
    if (stat(rawFarmeRecPath, &file_info) == 0) {
        printf("文件 '%s' 的大小为: %ld 字节\n", rawFarmeRecPath, file_info.st_size);
    } else {
        perror("无法获取文件信息");
    }

	if( FILE_AI_PCM == rawFarmeRecType ){
		if( file_info.st_size >= 10 *1024 )
			upload_lwsc( "upload_pcm",  rawFarmeRecFileName, rawFarmeRecPath );
		else
			RK_LOGW( "small file, do not upload");
	}else if( FILE_VI_H264 == rawFarmeRecType ){
		if( file_info.st_size >= 50 *1024 )
			upload_lwsc( "upload_h264", rawFarmeRecFileName, rawFarmeRecPath );
		else
			RK_LOGW( "small file, do not upload");
	}
	
	return 0;
}

RK_S32 take_photo(void) {
	printf("start\n");
	VENC_RECV_PIC_PARAM_S stRecvParam;
	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = 1;
	RK_MPI_VENC_StartRecvFrame(JPEG_VENC_CHN, &stRecvParam);
	take_photo_one = 1;

	return 0;
}


long long rkipc_get_curren_time_ms() {
	struct timespec current_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &current_time);

	return ((long long)current_time.tv_sec * 1000) + (current_time.tv_nsec / 1000000);
}


static RK_CHAR optstr[] = "?::c:d:";
static void print_usage(const RK_CHAR *name) {
	printf("usage example:\n");
	printf("\t%s -c ./conf.json -d 1\n", name);
	printf("\t-c | --confpath: VI width, Default:./conf.json\n");
	printf("\t-d | --debug: output the debug info:0\n");	
}

int init_conf( char* conf_file_path )
{
	FILE* f = fopen( conf_file_path , "r");
    if (f == NULL) {
        RK_LOGE( "conf Failed to open %s", conf_file_path);
        return -1;
    }

    char buf[1024*2] = {0};
    fread(buf, 1, sizeof(buf), f);

    cJSON *pJsonRoot = cJSON_Parse(buf);
    if(pJsonRoot !=NULL){
        
		cJSON *pValue = cJSON_GetObjectItem(pJsonRoot, "devSn");
        if((pValue != NULL) &&( cJSON_IsString(pValue)))        strcpy( devSn ,pValue->valuestring );  
        /**********************************/
        unsigned int tmp_int = 0;
        pValue = cJSON_GetObjectItem(pJsonRoot, "mainStreamWidth");
        if((pValue != NULL) &&( cJSON_IsNumber(pValue))) tmp_int=pValue->valueint;  
        mainStreamWidth = ( 0 == tmp_int) ? 1280 :tmp_int;

		tmp_int = 0;
        pValue = cJSON_GetObjectItem(pJsonRoot, "mainStreamHeight");
        if((pValue != NULL) &&( cJSON_IsNumber(pValue))) tmp_int=pValue->valueint;  
        mainStreamHeight = ( 0 == tmp_int) ? 720 :tmp_int;
		
		tmp_int = 0;
        pValue = cJSON_GetObjectItem(pJsonRoot, "mainStreamFps");
        if((pValue != NULL) &&( cJSON_IsNumber(pValue))) tmp_int=pValue->valueint;  
        mainStreamFps = ( 0 == tmp_int) ? 10 :tmp_int;		

		///////////////////////////////////////////////////////////////////////////
		tmp_int = 0;
        pValue = cJSON_GetObjectItem(pJsonRoot, "aiSampleRate");
        if((pValue != NULL) &&( cJSON_IsNumber(pValue))) tmp_int=pValue->valueint;  
        aiSampleRate = ( 0 == tmp_int) ? 16000 :tmp_int;
		
		tmp_int = 0;
        pValue = cJSON_GetObjectItem(pJsonRoot, "aoSampleRate");
        if((pValue != NULL) &&( cJSON_IsNumber(pValue))) tmp_int=pValue->valueint;  
        aoSampleRate = ( 0 == tmp_int) ? 16000 :tmp_int;		
		
		tmp_int = 0;
        pValue = cJSON_GetObjectItem(pJsonRoot, "aiVolume");
        if((pValue != NULL) &&( cJSON_IsNumber(pValue))) tmp_int=pValue->valueint;  
		else aiVolume = 50;	
		if(  tmp_int > 100 ) aiVolume = 100;		

		tmp_int = 0;
        pValue = cJSON_GetObjectItem(pJsonRoot, "aoVolume");
        if((pValue != NULL) &&( cJSON_IsNumber(pValue))) tmp_int=pValue->valueint;  
		else aoVolume = 50;	
		if(  tmp_int > 100 ) aoVolume = 100;			
		
		tmp_int = 0;
        pValue = cJSON_GetObjectItem(pJsonRoot, "ivsSensitivity");  //[0,4]
        if((pValue != NULL) &&( cJSON_IsNumber(pValue))) tmp_int=pValue->valueint;  
        u32Sensitivity = ( 0 == tmp_int) ? 2 :tmp_int;		

		// ////////////////////////////////////////////////////////////////////////////////////////////
        pValue = cJSON_GetObjectItem(pJsonRoot, "recTmpFileDir");
        if((pValue != NULL) &&( cJSON_IsString(pValue)))        strcpy( recTmpFileDir ,pValue->valuestring );   
		if( strlen( recTmpFileDir ) == 0 )						strcpy( recTmpFileDir ,"/tmp" ); 
		
		pValue = cJSON_GetObjectItem(pJsonRoot, "sndFileDir");
		if((pValue != NULL) &&( cJSON_IsString(pValue)))        strcpy( sndFileDir ,pValue->valuestring );   
		if( strlen( sndFileDir ) == 0 )						    strcpy( sndFileDir ,"/oem/helmet/snd" ); 
		
		////////////////////////////////////////////////////////////////////////////////////////////
        pValue = cJSON_GetObjectItem(pJsonRoot, "aivqeConfigPath");
        if((pValue != NULL) &&( cJSON_IsString(pValue)))        strcpy( aivqePath ,pValue->valuestring );   

        pValue = cJSON_GetObjectItem(pJsonRoot, "aovqeConfigPath");
        if((pValue != NULL) &&( cJSON_IsString(pValue)))        strcpy( aovqePath ,pValue->valuestring );  	
		
        pValue = cJSON_GetObjectItem(pJsonRoot, "ivaModelDir");
        if((pValue != NULL) &&( cJSON_IsString(pValue)))        strcpy( pIVAModelPath ,pValue->valuestring );  	
		
        pValue = cJSON_GetObjectItem(pJsonRoot, "rknnModelPath");
        if((pValue != NULL) &&( cJSON_IsString(pValue)))        strcpy( pRKnnModelPath ,pValue->valuestring );  			

        pValue = cJSON_GetObjectItem(pJsonRoot, "rknnModelLabelPath");
        if((pValue != NULL) &&( cJSON_IsString(pValue)))        strcpy( pRKnnModelLabelPath ,pValue->valuestring );  	     

        pValue = cJSON_GetObjectItem(pJsonRoot, "osdFontPath");
        if((pValue != NULL) &&( cJSON_IsString(pValue)))        strcpy( osdFontPath ,pValue->valuestring ); 
		
        pValue = cJSON_GetObjectItem(pJsonRoot, "nngAiPcmTxPath");
        if((pValue != NULL) &&( cJSON_IsString(pValue)))        strcpy( nngAiPcmTxPath ,pValue->valuestring ); 

        pValue = cJSON_GetObjectItem(pJsonRoot, "rtspLocPath");
        if((pValue != NULL) &&( cJSON_IsString(pValue)))        strcpy( rtspLocPath ,pValue->valuestring ); 
		
        pValue = cJSON_GetObjectItem(pJsonRoot, "rtmpServerUrl");
        if((pValue != NULL) &&( cJSON_IsString(pValue)))        strcpy( rtmpServerUrl ,pValue->valuestring ); 		
		
        pValue = cJSON_GetObjectItem(pJsonRoot, "httpServerUrl");
        if((pValue != NULL) &&( cJSON_IsString(pValue)))        strcpy( httpServerUrl ,pValue->valuestring );

		/**********************************/
		pValue = cJSON_GetObjectItem(pJsonRoot, "wsServerAddr");
        if((pValue != NULL) &&( cJSON_IsString(pValue)))        strcpy( wsServerAddr ,pValue->valuestring );

        pValue = cJSON_GetObjectItem(pJsonRoot, "wsServerPort");
        if((pValue != NULL) &&( cJSON_IsNumber(pValue))) 		wsServerPort=pValue->valueint;  

		pValue = cJSON_GetObjectItem(pJsonRoot, "wsCmdPath");
        if((pValue != NULL) &&( cJSON_IsString(pValue)))        strcpy( wsCmdPath ,pValue->valuestring );

		pValue = cJSON_GetObjectItem(pJsonRoot, "wsVoicePath");
        if((pValue != NULL) &&( cJSON_IsString(pValue)))        strcpy( wsVoicePath ,pValue->valuestring );

		/******************************** */
		pValue = cJSON_GetObjectItem(pJsonRoot, "keyEvent");
        if((pValue != NULL) &&( cJSON_IsBool(pValue))) 			keyEvent = ( cJSON_IsTrue(pValue) == 1 ) ? true : false; 

		pValue = cJSON_GetObjectItem(pJsonRoot, "ispRotation");
		if((pValue != NULL) &&( cJSON_IsBool(pValue))) 			ispRotation = ( cJSON_IsTrue(pValue) == 1 ) ? true : false; 
             
        /**********************************/        
        cJSON_Delete(pJsonRoot);
    }else{
        fclose(f);
        RK_LOGE( "JSON parse error in %s", conf_file_path );        
        return -2;
    }
    fclose(f);


	return 0;
}

/*
	0:rkisp_mainpath 1:rkisp_selfpath 2:rkisp_bypasspath
*/
RK_S32 init_common( int argc, char *argv[] )
{
    int c;
    while ((c = getopt(argc, argv, optstr)) != -1) {
		switch (c) {
		case 'c':
			confPath = optarg;break;
		case 'd':
			g_debug = atoi(optarg);break;					
		case '?':
		default:
			print_usage(argv[0]);
			return -1;
		}
	}

	if( init_conf( confPath ) < 0 ){
		RK_LOGE("parse conf file error:%s\n", confPath );
		return -1;
	}

	printf("#Video Resolution Channel 0 H264: %dx%d FPS:%d\n", mainStreamWidth, mainStreamHeight, mainStreamFps);   
	printf("#SampleRate input: %d CodecName: G711A, vqe path:%s\n", aiSampleRate, aivqePath);
	printf("#SampleRate output: 8000, vqe path is:%s\n", aovqePath);
	printf("#AI nng pcm tx path: %s\n", nngAiPcmTxPath);
	if( (int)rawFarmeRecType )
		printf("#Output raw frame type:%d  to Path: %s\n", (int)rawFarmeRecType, rawFarmeRecPath);
	printf("#IVS-Sensitivity: %d\n", u32Sensitivity);	
	printf("#IVA model dir Path: %s \n", pIVAModelPath);	
	printf("#rknn model Path: %s, label path is %s\n", pRKnnModelPath, pRKnnModelLabelPath);
	printf("#OSD font library path is %s\n", osdFontPath);	

	signal(SIGINT, sigterm_handler);

	if( init_shm_sem() < 0){
		RK_LOGE("init shm and sem fail!");
		return -1;
	}
	if (pthread_mutex_init(&obj_det_update_lock, NULL) != 0){ /* 动态初始化互斥量 */
		RK_LOGE( "obj_det_update_lock Mutex init failed");
		return -1;
	}	

    return 0;
}

RK_S32 deinit_common()
{
	deinit_shm_sem();
	stop_file_rec();
	pthread_mutex_unlock(&obj_det_update_lock);//将锁释放
	pthread_mutex_destroy(&obj_det_update_lock);//销毁互斥锁

    return 0;
}

int init_app_sh(void)
{
	system( "/usr/bin/amixer sset \"ACodec PGA Gain\" 30" );
	printf("set acodec pga gain:30\n");

	system( "echo 0 > /sys/devices/platform/pwm-light/hwmon/hwmon2/pwm1" );
	printf("set led off\n");

	const char *rkaiq3a_proc = "/oem/usr/bin/rkaiq_3A_server";
 	if (is_process_running(rkaiq3a_proc)) {
     	printf("进程 %s 已经启动。\n", rkaiq3a_proc);
 	} else {
     	printf("进程 %s 未运行, 将启动此进程 ...\n", rkaiq3a_proc);
		//system( "/oem/helmet/rkaip_3A start" );
		system("/usr/bin/nohup /oem/usr/bin/rkaiq_3A_server 2>&1 >/dev/null &");
		sleep(3);
 	}
	return 0;
}

int check_internet_connectivity() {
    int sock_fd;
    struct sockaddr_in server_addr;
    struct timeval timeout = {3, 0}; // 设置超时时间为3秒

    // 1. 创建TCP Socket
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket creation failed");
        return -1;
    }
    // 2. 设置连接超时，避免长时间阻塞
    //    默认connect()超时可能长达75秒以上[citation:3]
    if (setsockopt(sock_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt failed");
        close(sock_fd);
        return -1;
    }
    // 3. 配置目标服务器（114.114.114.114:80）
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons( 53 ); // 使用HTTP端口
    if (inet_pton(AF_INET, "114.114.114.114", &server_addr.sin_addr) <= 0) {
        perror("inet_pton failed");
        close(sock_fd);
        return -1;
    }
    // 4. 尝试连接
    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        // 连接失败，无法访问公网
        perror("connect failed");
        close(sock_fd);
        return -1;
    }
    // 5. 连接成功
    close(sock_fd);
    return 0;
}

int check_network(void)
{
	int i = 0;
	do{
		sleep(2);
		if (check_internet_connectivity() == 0) {
       	 	printf("网络连接正常\n");
			ao_play_snd( SND_CONNECTED );
			i = 0;
			break;
    	} else {
        	printf("无法连接到网络。\n");
			i++;
			if( i % 5 ==0 )
				ao_play_snd( SND_CONNECTING );
    	}
		if( quit )
			return -1;

	}while( 1 );
	return 0;
}


/******************************************************************************************* */
extern struct ivs_info ivs_info;
extern pthread_t ivs_main_thread;
extern void *IVS_task(void *arg);
extern pthread_t vi_main_thread;
extern void *VI_task(void *arg);
extern pthread_t iva_main_thread;
extern void *VI_det_task(void *arg);
extern pthread_t jpeg_venc_thread;
extern void *VI_jpeg_task(void *arg);

void start_vi_vis_iav_rknn_jpeg_thread()
{
	pthread_create(&vi_main_thread, NULL, VI_task, NULL );
	pthread_create(&ivs_main_thread, NULL, IVS_task, &ivs_info );
	if( ( strlen( pIVAModelPath) >= 3 ) || ( strlen( pRKnnModelPath) >= 3 )  )
		pthread_create(&iva_main_thread, NULL, VI_det_task, NULL );	
	pthread_create(&jpeg_venc_thread, NULL, VI_jpeg_task, NULL);
}

void join_vi_vis_iav_rknn_jpeg_thread()
{
	pthread_join(vi_main_thread, NULL);
	pthread_join(ivs_main_thread, NULL);
	if( ( strlen( pIVAModelPath) >= 3 ) || ( strlen( pRKnnModelPath) >= 3 ) )
		pthread_join(iva_main_thread, NULL);	
	pthread_join(jpeg_venc_thread, NULL);
}

/******************************************************************************************* */

typedef struct rk_signal_t {
	sem_t sem;
	int max_val;
} rk_signal_t;

void *rk_signal_create(int defval, int maxval) {
	rk_signal_t *h = (rk_signal_t *)malloc(sizeof(rk_signal_t));
	if (h == NULL) 
		return NULL;
	if (sem_init(&(h->sem), 0, defval) == -1) { /* 初始化信号量失败,失败原因见error */
		perror("sem_init: ");
		free(h);
		h = NULL;
	} else
		h->max_val = maxval;
	return h;
}

void rk_signal_destroy(void *sem) {
	if (sem == NULL) {
		return;
	}
	sem_destroy((sem_t *)sem);
	free(sem);
}

int rk_signal_wait(void *sem, int timeout) {
	struct timespec tv;
	if (sem == NULL) 
		return 0;
	if (timeout < 0) { /* 需要判断返回值,因为如果信号量被destroy了也会返回的 */
		return sem_wait((sem_t *)sem) == 0 ? 0 : -1;
	} else {
		clock_gettime(CLOCK_REALTIME, &tv);
		tv.tv_nsec += (timeout % 1000) * 1000000;
		if (tv.tv_nsec >= 1000000000) {
			tv.tv_sec += 1;
			tv.tv_nsec -= 1000000000;
		}
		tv.tv_sec += timeout / 1000;
		if (sem_timedwait((sem_t *)sem, (const struct timespec *)&tv)) {
			return -1;
		}
		return 0;
	}
}

void rk_signal_give(void *sem) {
	int val;
	if (sem == NULL) 
		return;
	sem_getvalue((sem_t *)sem, &val);
	if (val < ((rk_signal_t *)sem)->max_val) {
		sem_post((sem_t *)sem);
	}
}

void rk_signal_reset(void *sem) { rk_signal_give(sem); }


/******************************************************* */
// 初始化缓冲区
void init_buffer(CircularBuffer* cb) {
    cb->read_pos = 0;
    cb->write_pos = 0;
    cb->size = 0;
}
// 检查缓冲区是否为空
int is_empty(CircularBuffer* cb) {
    return cb->size <= 0;
}
// 检查缓冲区是否为满
int is_full(CircularBuffer* cb) {
    return cb->size == RING_BUFFER_SIZE;
}
// 写数据到缓冲区
int write_to_buffer(CircularBuffer* cb, uint8_t* data, int data_len) {
    if (data_len <= 0) 
        return -1;  // 数据长度必须大于 0
    for (int i = 0; i < data_len; i++) {
        if (is_full(cb)) 
            return -1;  // 缓冲区已满，无法继续写入
        cb->buffer[cb->write_pos] = data[i];// 写数据
        cb->write_pos = (cb->write_pos + 1) % RING_BUFFER_SIZE;  // 循环写指针
        cb->size++;
    }
    return 0;  // 成功写入
}
// 从缓冲区读取数据
int read_from_buffer(CircularBuffer* cb, uint8_t* data, int data_len) {
    if (is_empty(cb)) {
        return -1;  // 缓冲区为空，无法读取
    }
    if (data_len > cb->size) {
        cb->size -= data_len;
        cb->read_pos = ( cb->read_pos + data_len ) % RING_BUFFER_SIZE; 
        return -2;
    }
    for (int i = 0; i < data_len; i++) {// 按照固定长度读取数据
        data[i] = cb->buffer[cb->read_pos];
        cb->read_pos = (cb->read_pos + 1) % RING_BUFFER_SIZE;  // 循环读指针
        cb->size--;        
    }
    return 0;  // 成功读取
}

/*********************************************************** */
int is_process_running(const char *proc_path) {
    DIR *dir;
    struct dirent *entry;
    char path[256];
    char exe_path[256];
    int is_running = 0;

    dir = opendir("/proc");
    if (dir == NULL) {
        perror("opendir /proc failed");
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        // 判断目录名是否全为数字（即PID）
        int is_digit = 1;
        for (char *p = entry->d_name; *p != '\0'; p++) {
            if (*p < '0' || *p > '9') {
                is_digit = 0;
                break;
            }
        }

        if (!is_digit) continue;

        // 构造 /proc/{pid}/exe 路径
        snprintf(path, sizeof(path), "/proc/%s/exe", entry->d_name);

        // 读取符号链接指向的目标路径（即实际运行的程序）
        ssize_t len = readlink(path, exe_path, sizeof(exe_path) - 1);
        if (len != -1) {
            exe_path[len] = '\0';
            // 对比路径是否一致
            if (strcmp(exe_path, proc_path) == 0) {
                is_running = 1;
                break;
            }
        }
    }

    closedir(dir);
    return is_running;
}
