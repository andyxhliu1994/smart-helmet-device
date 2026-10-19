#include "lwsc.h"

struct lws_context *context = NULL;
struct lws *wsi_cmd = NULL;
struct lws *wsi_voice = NULL;
static pthread_t lws_thread;
static bool lws_thread_started = false;
static volatile bool lws_stop = false;
static volatile bool lws_reconnect_requested = false;
static bool cmd_hello_pending = false;
static bool hello_accepted = false;
static bool voice_connected = false;
static time_t hello_sent_at = 0;
static pthread_mutex_t notice_lock = PTHREAD_MUTEX_INITIALIZER;
static char pending_notice[256] = {0};
static RK_U64 pending_msgid = 0;
static time_t notice_sent_at = 0;
static bool notice_waiting_ack = false;

#define HELLO_ACK_TIMEOUT_SECONDS 5
#define UPLOAD_ACK_TIMEOUT_SECONDS 5
#define DEVICE_PROTOCOL_VERSION "1.0"
#define DEVICE_APP_VERSION "1.0.0"

#define LWSC_RECONNECT_SECONDS 10

int init_lws_create(void);
int lwsc_reconnect(void);

// 事件回调函数
static int ws_callback_cmd(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("cmd 连接建立成功!\n");// 在这里保存对应的 wsi 句柄
            wsi_cmd = wsi;
            hello_accepted = false;
            hello_sent_at = 0;
            cmd_hello_pending = true;
            lws_callback_on_writable(wsi);
            break;

        case LWS_CALLBACK_CLIENT_WRITEABLE:
            if (cmd_hello_pending) {
                cJSON *hello = cJSON_CreateObject();
                cJSON_AddStringToObject(hello, "cmd", "hello");
                cJSON_AddStringToObject(hello, "sn", devSn);
                cJSON_AddStringToObject(hello, "protocol_version", DEVICE_PROTOCOL_VERSION);
                cJSON_AddStringToObject(hello, "app_version", DEVICE_APP_VERSION);
                char *payload = cJSON_PrintUnformatted(hello);
                if (payload) {
                    lws_send_message(wsi, payload);
                    free(payload);
                    hello_sent_at = time(NULL);
                }
                cJSON_Delete(hello);
                cmd_hello_pending = false;
            }
            pthread_mutex_lock(&notice_lock);
            if (hello_accepted && pending_notice[0] && !notice_waiting_ack) {
                lws_send_message(wsi, pending_notice);
                notice_waiting_ack = true;
                notice_sent_at = time(NULL);
                RK_LOGI("upload notice sent: msgid=%llu", (unsigned long long)pending_msgid);
            }
            pthread_mutex_unlock(&notice_lock);
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:{
            printf("CMD 收到消息: %.*s\n", (int)len, (char *)in);
            char cmd_str[1024*2] = {0};
            char cmd[32] = {0};
            size_t copy_len = len < sizeof(cmd_str) - 1 ? len : sizeof(cmd_str) - 1;
            memcpy(cmd_str, in, copy_len);

            cJSON *pJsonRoot = cJSON_Parse( cmd_str );
            if(pJsonRoot !=NULL){
		        cJSON *pValue = cJSON_GetObjectItem(pJsonRoot, "cmd");
                if((pValue != NULL) && cJSON_IsString(pValue))
                    snprintf(cmd, sizeof(cmd), "%s", pValue->valuestring);

                if (strcmp(cmd, "hello_ack") == 0) {
                    cJSON *status = cJSON_GetObjectItem(pJsonRoot, "status");
                    cJSON *error_code = cJSON_GetObjectItem(pJsonRoot, "error_code");
                    hello_accepted = status && cJSON_IsString(status) &&
                                     strcmp(status->valuestring, "accepted") == 0;
                    hello_sent_at = 0;
                    if (!hello_accepted) {
                        RK_LOGE("hello rejected: %s", error_code && cJSON_IsString(error_code)
                                ? error_code->valuestring : "unknown");
                        lwsc_reconnect();
                    }
                    cJSON_Delete(pJsonRoot);
                    break;
                }

                if (!hello_accepted) {
                    RK_LOGW("ignore command before hello_ack: %s", cmd);
                    cJSON_Delete(pJsonRoot);
                    break;
                }

                if (strcmp(cmd, "ack") == 0) {
                    cJSON *id = cJSON_GetObjectItem(pJsonRoot, "msgid");
                    cJSON *status = cJSON_GetObjectItem(pJsonRoot, "status");
                    cJSON *error_code = cJSON_GetObjectItem(pJsonRoot, "error_code");
                    pthread_mutex_lock(&notice_lock);
                    if (notice_waiting_ack && cJSON_IsNumber(id) &&
                        (RK_U64)id->valuedouble == pending_msgid && cJSON_IsString(status)) {
                        RK_LOGI("upload ack: msgid=%llu status=%s error=%s",
                                (unsigned long long)pending_msgid, status->valuestring,
                                cJSON_IsString(error_code) ? error_code->valuestring : "");
                        pending_notice[0] = '\0';
                        notice_waiting_ack = false;
                        notice_sent_at = 0;
                    }
                    pthread_mutex_unlock(&notice_lock);
                    cJSON_Delete(pJsonRoot);
                    break;
                }

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
            wsi_cmd = NULL;
            hello_accepted = false;
            lwsc_reconnect();
            break;

        case LWS_CALLBACK_CLOSED:
        case LWS_CALLBACK_CLIENT_CLOSED:
            printf("cmd 连接断开\n");
            wsi_cmd = NULL;
            hello_accepted = false;
            lwsc_reconnect();
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
            wsi_voice = wsi;
            voice_connected = true;
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
            wsi_voice = NULL;
            voice_connected = false;
            lwsc_reconnect();
            break;

        case LWS_CALLBACK_CLOSED:
        case LWS_CALLBACK_CLIENT_CLOSED:
            printf("voice 连接断开\n");
            wsi_voice = NULL;
            voice_connected = false;
            lwsc_reconnect();
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
    if (!wsi || !message) return;
    // 为 lws_write 预留 LWS_PRE 字节
    size_t msg_len = strlen(message);
    unsigned char *buf = (unsigned char *)malloc(LWS_PRE + msg_len);
    if (!buf) return;
    // 拷贝消息并发送
    memcpy(&buf[LWS_PRE], message, msg_len);
    lws_write(wsi, &buf[LWS_PRE], msg_len, LWS_WRITE_TEXT);
    free(buf);
}

static void wait_before_reconnect(void)
{
    int i;
    printf("websocket 将在 %d 秒后重连\n", LWSC_RECONNECT_SECONDS);
    for (i = 0; i < LWSC_RECONNECT_SECONDS * 10; ++i) {
        if (quit || lws_stop)
            return;
        usleep(100000);
    }
}

void *lws_task(void *arg) {
    printf("lws task start\n");
    while (!quit && !lws_stop) {
        lws_reconnect_requested = false;

        if (init_lws_create() != 0) {
            RK_LOGE("create lws link fail!");
            wait_before_reconnect();
            continue;
        }

        while (!quit && !lws_stop && !lws_reconnect_requested) {
            if (lws_service(context, 100) < 0) {
                printf("websocket service error\n");
                lws_reconnect_requested = true;
            }
            if (hello_sent_at && !hello_accepted &&
                time(NULL) - hello_sent_at >= HELLO_ACK_TIMEOUT_SECONDS) {
                RK_LOGE("hello_ack timeout");
                lws_reconnect_requested = true;
            }
            pthread_mutex_lock(&notice_lock);
            if (notice_waiting_ack && notice_sent_at &&
                time(NULL) - notice_sent_at >= UPLOAD_ACK_TIMEOUT_SECONDS) {
                RK_LOGE("upload ack timeout: msgid=%llu", (unsigned long long)pending_msgid);
                pending_notice[0] = '\0';
                notice_waiting_ack = false;
                notice_sent_at = 0;
            }
            bool notice_ready = pending_notice[0] && !notice_waiting_ack;
            pthread_mutex_unlock(&notice_lock);
            if (notice_ready && hello_accepted && wsi_cmd)
                lws_callback_on_writable(wsi_cmd);
        }

        if (context)
            lws_context_destroy(context);
        context = NULL;
        wsi_cmd = NULL;
        wsi_voice = NULL;
        cmd_hello_pending = false;
        hello_accepted = false;
        voice_connected = false;
        hello_sent_at = 0;
        pthread_mutex_lock(&notice_lock);
        pending_notice[0] = '\0';
        notice_waiting_ack = false;
        notice_sent_at = 0;
        pthread_mutex_unlock(&notice_lock);

        if (!quit && !lws_stop)
            wait_before_reconnect();
    }
    printf("lws task end\n");
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
    if (!lws_client_connect_via_info(&i1)) {
        printf("cmd websocket connection start failed\n");
        lws_context_destroy(context);
        context = NULL;
        return -1;
    }
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
    if (!lws_client_connect_via_info(&i2)) {
        printf("voice websocket connection start failed\n");
        lws_context_destroy(context);
        context = NULL;
        wsi_cmd = NULL;
        return -1;
    }

    return 0;
}

int init_lwsc(void) {
    if (lws_thread_started)
        return 0;

    lws_stop = false;
    lws_reconnect_requested = false;
    if (pthread_create(&lws_thread, NULL, lws_task, NULL) != 0) {
        RK_LOGE("create lws task fail!");
        return -1;
    }
    lws_thread_started = true;

    return 0;
}

void deinit_lwsc(void)
{
    lws_stop = true;
    if (context)
        lws_cancel_service(context);
    if (lws_thread_started) {
        pthread_join(lws_thread, NULL);
        lws_thread_started = false;
    }
}

int lwsc_reconnect(void)
{
    if (lws_stop)
        return 0;
    lws_reconnect_requested = true;
    if (context)
        lws_cancel_service(context);
    return 0;
}

bool lwsc_is_ready(void)
{
    return hello_accepted && voice_connected && wsi_cmd && wsi_voice &&
           !lws_reconnect_requested;
}

int lwsc_send_upload_notice(const char *cmd, const char *file, RK_U64 msgid)
{
    if (!cmd || !file || !lwsc_is_ready()) return -1;
    cJSON *json = cJSON_CreateObject();
    if (!json) return -1;
    cJSON_AddNumberToObject(json, "msgid", (double)msgid);
    cJSON_AddStringToObject(json, "cmd", cmd);
    cJSON_AddStringToObject(json, "file", file);
    char *payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!payload) return -1;
    int result = -1;
    pthread_mutex_lock(&notice_lock);
    if (!pending_notice[0] && strlen(payload) < sizeof(pending_notice)) {
        strcpy(pending_notice, payload);
        pending_msgid = msgid;
        result = 0;
    }
    pthread_mutex_unlock(&notice_lock);
    free(payload);
    if (result == 0 && context) lws_cancel_service(context);
    return result;
}
