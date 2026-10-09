#include "lwsc.h"

struct lws_context *context = NULL;
struct lws *wsi_cmd = NULL;
struct lws *wsi_voice = NULL;
static bool lws_exit = false;
static bool lws_retry = false;

int init_lws_create(void);
int lwsc_reconnect(void);

// 事件回调函数
static int ws_callback_cmd(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("cmd 连接建立成功!\n");// 在这里保存对应的 wsi 句柄
            if (wsi_cmd == NULL) 
                wsi_cmd = wsi;
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:{
            printf("CMD 收到消息: %.*s\n", len, (char *)in);
            char cmd_str[1024*2] = {0};
            char cmd[32] = {0};
            strncpy( cmd_str, (const char *)in, len );

            cJSON *pJsonRoot = cJSON_Parse( cmd_str );
            if(pJsonRoot !=NULL){
		        cJSON *pValue = cJSON_GetObjectItem(pJsonRoot, "cmd");
                if((pValue != NULL) &&( cJSON_IsString(pValue)))     strcpy( cmd ,pValue->valuestring );  

                pValue = cJSON_GetObjectItem(pJsonRoot, "msgid");
                if((pValue != NULL) &&( cJSON_IsNumber(pValue)))     g_msgid=pValue->valueint;  

                cJSON_Delete(pJsonRoot);  
            }else{
                RK_LOGE("cmd parse error\n");
                break;
            }   

            RK_LOGI("cmd:%s", cmd );
            if ( 0 == strcmp( cmd, "start_pcm") ){
                start_file_rec( FILE_AI_PCM );
            }else if( 0 == strcmp( cmd, "stop_pcm") ){
                stop_file_rec();
            }else if( 0 == strcmp( cmd, "start_h264") ){
                start_file_rec( FILE_VI_H264 );
            }else if( 0 == strcmp( cmd, "stop_h264") ){
                stop_file_rec();
            }else if( 0 == strcmp( cmd, "snap_jpeg") ){
                take_photo();
            }else if( 0 == strcmp( cmd, "reboot") ){
            
            }else if( 0 == strcmp( cmd, "uptime") ){
  
            }
            break;
           }

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            printf("cmd 连接失败\n");
            break;

        case LWS_CALLBACK_CLOSED:
        case LWS_CALLBACK_CLIENT_CLOSED:
            printf("cmd 连接断开\n");
            if( false ==  lws_retry ){
                lws_retry = true;
                lwsc_reconnect();
            }
            break;

        default:
            break;
    }
    return 0;
}

// 事件回调函数
int lws_audio_recv_t = 0;
static int ws_callback_voice(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("voice 连接建立成功!\n");// 在这里保存对应的 wsi 句柄
            if (wsi_voice == NULL) 
                wsi_voice = wsi;
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
            lws_audio_recv_t++;
            write_to_buffer( &ao_play_cb, (uint8_t*)in, len); 
            if( g_debug ){
                struct timeval tv;
                gettimeofday(&tv, NULL);  // 获取当前时间
                printf( "voice recv len:%ld time:%ld\n", len, tv.tv_sec );  
            }
            if( ( lws_audio_recv_t == AUDIO_FRAME_FREQ_HZ/2 ) && ( !ao_playing )) {
                pthread_t ao_thread;
                pthread_create(&ao_thread, NULL, ao_play_task, NULL);
                pthread_detach(ao_thread);  
            }
            break;
           
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            printf("voice 连接失败\n");
            break;

        case LWS_CALLBACK_CLOSED:
        case LWS_CALLBACK_CLIENT_CLOSED:
            printf("voice 连接断开\n");
            if( false ==  lws_retry ){
                lws_retry = true;
                lwsc_reconnect();
            }
            break;           

        default:
            break;
    }
    return 0;
}
// 协议列表
static struct lws_protocols protocols[] = {
    {"cmd", ws_callback_cmd, 0,1024*5,},
    {"voice", ws_callback_voice, 0,FRAME_BYTES*3,},   
    { NULL, NULL, 0, 0 } /* 结尾 */
};


void lws_send_message(struct lws *wsi, const char *message) {
    if (!wsi) return;
    // 为 lws_write 预留 LWS_PRE 字节
    unsigned char buf[LWS_PRE + 128];
    int msg_len = strlen(message);
    // 拷贝消息并发送
    memcpy(&buf[LWS_PRE], message, msg_len);
    lws_write(wsi, &buf[LWS_PRE], msg_len, LWS_WRITE_TEXT);
}


void *lws_task(void *arg) {
    printf("lws task start\n");
    while ( (!quit) && (!lws_exit) ) // 运行事件循环
        lws_service(context, 50); // 轮询50ms
    printf("lws task end\n");
    lws_exit = false;
    return NULL;
}

int init_lws_create(void)
{
    if( ( strlen( wsServerAddr ) <= 3 )||( wsServerPort == 0 ) || 
        ( strlen( wsCmdPath ) == 0 )||( strlen( wsVoicePath ) == 0 )){
        printf("websocket server set error\n");
        return -1;
    }

    static const lws_retry_bo_t retry = {
        .secs_since_valid_ping = 30,  // 闲置超过 30 秒没有双向流量时，触发 PING
        .secs_since_valid_hangup = 60, // 发送 PING 后，超过 60 秒未收到 PONG 则断开连接
    };
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN; // 客户端模式
    info.protocols = protocols;
    info.ka_time = 30;
    info.ka_probes = 5;
    info.ka_interval = 10;
    info.retry_and_idle_policy = &retry; // 绑定重试和闲置策略
    context = lws_create_context(&info);
    if( !context ){
        printf("lws create context error\n");
        return -1;
    }
    // 1. 连接到 URL 1 cmd
    struct lws_client_connect_info i1;
    memset(&i1, 0, sizeof(i1));
    i1.context = context;
    i1.address = wsServerAddr;
    i1.port = wsServerPort; // wss 端口
    i1.path = wsCmdPath;
    i1.host = lws_canonical_hostname( context );
    i1.origin = "origin";
    i1.protocol = protocols[0].name;
    i1.pwsi = &wsi_cmd;
    lws_client_connect_via_info(&i1);
    // 2. 连接到 URL 2
    struct lws_client_connect_info i2;
    memset(&i2, 0, sizeof(i2));
    i2.context = context;
    i2.address = wsServerAddr;
    i2.port = wsServerPort;  // ws 端口
    i2.path = wsVoicePath;
    i2.host = lws_canonical_hostname( context );
    i2.origin = "origin";
    i2.protocol = protocols[1].name;
    i2.pwsi = &wsi_voice;
    lws_client_connect_via_info(&i2);

    lws_retry = false;

    return 0;
}

int init_lwsc(void) {

    if( 0 != init_lws_create() ){
        RK_LOGE("create lws link fail!");
        wsi_cmd = NULL;
        wsi_voice = NULL;       
        return -1;
    }
    
    pthread_t lws_thread;
    pthread_create(&lws_thread, NULL, lws_task, NULL);
    pthread_detach(lws_thread);

    sleep(2);
    lws_send_message( wsi_cmd, "{\"msgid\":0,\"cmd\":\"hello\"}" );

    return 0;
}

void deinit_lwsc(void)
{
    if( context )
        lws_context_destroy(context);
    context = NULL;
}

int lwsc_reconnect(void)
{
    wsi_cmd = NULL;
    wsi_voice = NULL;   
    lws_exit = true;  
    sleep(2);
    deinit_lwsc();
    init_lwsc();
    return 0;
}


