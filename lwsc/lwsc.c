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
static long long hello_sent_ms = 0;
static long long connect_started_ms = 0;
static pthread_mutex_t notice_lock = PTHREAD_MUTEX_INITIALIZER;
static char pending_notice[256] = {0};
static char pending_upload_cmd[32] = {0};
static RK_U64 pending_msgid = 0;
static time_t notice_sent_at = 0;
static bool notice_waiting_ack = false;
static pthread_mutex_t voice_lock = PTHREAD_MUTEX_INITIALIZER;
static char active_voice_id[96] = {0};
static char pending_voice_status[256] = {0};
static bool voice_active = false;
static bool voice_cancelled = false;
static RK_U64 voice_received_bytes = 0;
static RK_U64 voice_played_bytes = 0;
static RK_U64 voice_total_bytes = 0;
static time_t voice_end_at = 0;
static pthread_mutex_t control_lock = PTHREAD_MUTEX_INITIALIZER;
static char control_outbox[8][512];
static unsigned int control_head = 0;
static unsigned int control_count = 0;
static bool platform_recording = false;
static char platform_photo_cmd_id[96] = {0};

#define HELLO_ACK_TIMEOUT_SECONDS 5
#define CONNECTION_TIMEOUT_SECONDS 8
#define UPLOAD_ACK_TIMEOUT_SECONDS 5
#define VOICE_END_TIMEOUT_SECONDS 5
#define DEVICE_PROTOCOL_VERSION "1.0"
#define DEVICE_APP_VERSION "1.1.1"

#define LWSC_RECONNECT_SECONDS 10

int init_lws_create(void);
int lwsc_reconnect(void);

struct platform_upload_job {
    char file_name[128];
    char file_path[256];
    char cmd_id[96];
};

static bool queue_control_json(cJSON *json)
{
    char *payload = cJSON_PrintUnformatted(json);
    if (!payload) return false;
    bool queued = false;
    pthread_mutex_lock(&control_lock);
    if (strlen(payload) < sizeof(control_outbox[0]) &&
        control_count < sizeof(control_outbox) / sizeof(control_outbox[0])) {
        unsigned int tail = (control_head + control_count) %
                            (sizeof(control_outbox) / sizeof(control_outbox[0]));
        strcpy(control_outbox[tail], payload);
        control_count++;
        queued = true;
    }
    pthread_mutex_unlock(&control_lock);
    if (!queued) RK_LOGE("control reply queue full or payload too long");
    free(payload);
    if (queued && context) lws_cancel_service(context);
    return queued;
}

static void queue_command_ack(const char *id, const char *status, const char *error_code)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) return;
    cJSON_AddStringToObject(json, "cmd", "command_ack");
    cJSON_AddStringToObject(json, "cmd_id", id);
    cJSON_AddStringToObject(json, "status", status);
    if (error_code) cJSON_AddStringToObject(json, "error_code", error_code);
    queue_control_json(json);
    cJSON_Delete(json);
}

static void queue_command_result(const char *id, const char *request_cmd,
                                 const char *status, const char *error_code,
                                 cJSON *data)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) { cJSON_Delete(data); return; }
    cJSON_AddStringToObject(json, "cmd", "command_result");
    cJSON_AddStringToObject(json, "cmd_id", id);
    cJSON_AddStringToObject(json, "request_cmd", request_cmd);
    cJSON_AddStringToObject(json, "status", status);
    if (error_code) cJSON_AddStringToObject(json, "error_code", error_code);
    if (data) cJSON_AddItemToObject(json, "data", data);
    queue_control_json(json);
    cJSON_Delete(json);
}

bool lwsc_platform_recording(void)
{
    pthread_mutex_lock(&control_lock);
    bool active = platform_recording;
    pthread_mutex_unlock(&control_lock);
    return active;
}

void lwsc_interrupt_platform_recording(void)
{
    pthread_mutex_lock(&control_lock);
    bool active = platform_recording;
    platform_recording = false;
    pthread_mutex_unlock(&control_lock);
    if (active) discard_file_rec();
}

void lwsc_interrupt_platform_photo(void)
{
    char id[sizeof(platform_photo_cmd_id)] = {0};
    pthread_mutex_lock(&control_lock);
    strcpy(id, platform_photo_cmd_id);
    platform_photo_cmd_id[0] = '\0';
    pthread_mutex_unlock(&control_lock);
    if (id[0]) queue_command_result(id, "snap_jpeg", "failed", "EXECUTION_FAILED", NULL);
}

void lwsc_platform_photo_saved(bool success, const char *file_name,
                               const char *file_path)
{
    char id[sizeof(platform_photo_cmd_id)] = {0};
    pthread_mutex_lock(&control_lock);
    strcpy(id, platform_photo_cmd_id);
    platform_photo_cmd_id[0] = '\0';
    pthread_mutex_unlock(&control_lock);
    if (id[0]) queue_command_result(id, "snap_jpeg",
                                    success ? "success" : "failed",
                                    success ? NULL : "EXECUTION_FAILED", NULL);
    if (success && file_name && file_path)
        upload_lwsc_with_cmd_id("upload_jpeg", file_name, file_path,
                                id[0] ? id : NULL);
}

static void *platform_upload_pcm_task(void *arg)
{
    struct platform_upload_job *job = (struct platform_upload_job *)arg;
    upload_lwsc_with_cmd_id("upload_pcm", job->file_name,
                            job->file_path, job->cmd_id);
    free(job);
    return NULL;
}

static void handle_platform_command(const char *cmd, cJSON *root)
{
    cJSON *id_json = cJSON_GetObjectItem(root, "cmd_id");
    if (!cJSON_IsString(id_json) || !id_json->valuestring ||
        !id_json->valuestring[0] || strlen(id_json->valuestring) >= 96) {
        RK_LOGW("ignore control command without valid cmd_id");
        return;
    }
    const char *id = id_json->valuestring;
    if (!cmd[0]) {
        queue_command_ack(id, "rejected", "INVALID_COMMAND");
        return;
    }
    if (strcmp(cmd, "get_status") == 0) {
        queue_command_ack(id, "accepted", NULL);
        cJSON *data = cJSON_CreateObject();
        if (!data) {
            queue_command_result(id, cmd, "failed", "EXECUTION_FAILED", NULL);
            return;
        }
        bool recording = save_file && rawFarmeRecType == FILE_AI_PCM;
        cJSON_AddBoolToObject(data, "recording", recording);
        cJSON_AddStringToObject(data, "recording_source", recording
                                ? (lwsc_platform_recording() ? "platform" : "button") : "none");
        pthread_mutex_lock(&voice_lock);
        bool playing = voice_active || ao_playing;
        pthread_mutex_unlock(&voice_lock);
        cJSON_AddBoolToObject(data, "playing", playing);
        struct timespec uptime = {0};
        clock_gettime(CLOCK_MONOTONIC, &uptime);
        cJSON_AddNumberToObject(data, "uptime_seconds", (double)uptime.tv_sec);
        cJSON_AddStringToObject(data, "app_version", DEVICE_APP_VERSION);
        queue_command_result(id, cmd, "success", NULL, data);
        return;
    }
    if (strcmp(cmd, "snap_jpeg") == 0) {
        if (save_file || take_photo_one) {
            queue_command_ack(id, "rejected", "DEVICE_BUSY");
            return;
        }
        pthread_mutex_lock(&control_lock);
        strcpy(platform_photo_cmd_id, id);
        pthread_mutex_unlock(&control_lock);
        queue_command_ack(id, "accepted", NULL);
        if (take_photo() != 0) lwsc_interrupt_platform_photo();
        return;
    }
    if (strcmp(cmd, "start_pcm") == 0) {
        pthread_mutex_lock(&voice_lock);
        bool playing = voice_active || ao_playing;
        pthread_mutex_unlock(&voice_lock);
        if (save_file || take_photo_one || playing) {
            queue_command_ack(id, "rejected", "DEVICE_BUSY");
            return;
        }
        queue_command_ack(id, "accepted", NULL);
        if (start_file_rec(FILE_AI_PCM) == 0) {
            pthread_mutex_lock(&control_lock);
            platform_recording = true;
            pthread_mutex_unlock(&control_lock);
            queue_command_result(id, cmd, "success", NULL, NULL);
        } else {
            queue_command_result(id, cmd, "failed", "EXECUTION_FAILED", NULL);
        }
        return;
    }
    if (strcmp(cmd, "stop_pcm") == 0) {
        if (!lwsc_platform_recording()) {
            queue_command_ack(id, "rejected", save_file ? "DEVICE_BUSY" : "NOT_RECORDING");
            return;
        }
        queue_command_ack(id, "accepted", NULL);
        pthread_mutex_lock(&control_lock);
        platform_recording = false;
        pthread_mutex_unlock(&control_lock);
        struct platform_upload_job *job = (struct platform_upload_job *)calloc(1, sizeof(*job));
        if (!job) {
            discard_file_rec();
            queue_command_result(id, cmd, "failed", "EXECUTION_FAILED", NULL);
            return;
        }
        int result = stop_file_rec_local(job->file_name, sizeof(job->file_name),
                                         job->file_path, sizeof(job->file_path));
        if (result != 0) {
            free(job);
            queue_command_result(id, cmd, "failed", "EXECUTION_FAILED", NULL);
            return;
        }
        strcpy(job->cmd_id, id);
        pthread_t upload_thread;
        if (pthread_create(&upload_thread, NULL, platform_upload_pcm_task, job) != 0) {
            free(job);
            queue_command_result(id, cmd, "failed", "EXECUTION_FAILED", NULL);
            return;
        }
        pthread_detach(upload_thread);
        queue_command_result(id, cmd, "success", NULL, NULL);
        return;
    }
    queue_command_ack(id, "rejected", "UNSUPPORTED_COMMAND");
}

static void queue_voice_status_locked(const char *id, const char *status, const char *error_code)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) return;
    cJSON_AddStringToObject(json, "cmd", "voice_status");
    cJSON_AddStringToObject(json, "voice_id", id);
    cJSON_AddStringToObject(json, "status", status);
    if (error_code) cJSON_AddStringToObject(json, "error_code", error_code);
    char *payload = cJSON_PrintUnformatted(json);
    if (payload && strlen(payload) < sizeof(pending_voice_status))
        strcpy(pending_voice_status, payload);
    free(payload);
    cJSON_Delete(json);
    if (context) lws_cancel_service(context);
}

static void complete_voice_if_ready_locked(void)
{
    if (voice_active && voice_end_at && voice_received_bytes == voice_total_bytes &&
        voice_played_bytes >= voice_total_bytes) {
        queue_voice_status_locked(active_voice_id, "completed", NULL);
        voice_active = false;
        active_voice_id[0] = '\0';
    }
}

bool lwsc_voice_is_active(void)
{
    pthread_mutex_lock(&voice_lock);
    bool active = voice_active;
    pthread_mutex_unlock(&voice_lock);
    return active;
}

static void start_voice_player_locked(void)
{
    if (!voice_active || ao_playing) return;
    int bytes = buffered_audio_bytes(&ao_voice_cb);
    if (bytes < FRAME_BYTES * 5 && !(voice_end_at && bytes > 0)) return;
    pthread_t ao_thread;
    ao_playing = true;
    ao_playing_voice = true;
    if (pthread_create(&ao_thread, NULL, ao_play_task, (void *)1) == 0)
        pthread_detach(ao_thread);
    else {
        ao_playing = false;
        ao_playing_voice = false;
        queue_voice_status_locked(active_voice_id, "failed", "INTERNAL_ERROR");
        voice_active = false;
    }
}

void lwsc_voice_start_if_ready(void)
{
    pthread_mutex_lock(&voice_lock);
    start_voice_player_locked();
    pthread_mutex_unlock(&voice_lock);
}

// 事件回调函数
static int ws_callback_cmd(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("cmd 连接建立成功!\n");// 在这里保存对应的 wsi 句柄
            wsi_cmd = wsi;
            hello_accepted = false;
            hello_sent_ms = 0;
            cmd_hello_pending = true;
            lws_callback_on_writable(wsi);
            break;

        case LWS_CALLBACK_CLIENT_WRITEABLE: {
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
                    hello_sent_ms = rkipc_get_curren_time_ms();
                }
                cJSON_Delete(hello);
                cmd_hello_pending = false;
                break;
            }
            char control_payload[sizeof(control_outbox[0])] = {0};
            bool more_control = false;
            pthread_mutex_lock(&control_lock);
            if (hello_accepted && control_count) {
                strcpy(control_payload, control_outbox[control_head]);
                control_head = (control_head + 1) %
                               (sizeof(control_outbox) / sizeof(control_outbox[0]));
                control_count--;
                more_control = control_count != 0;
            }
            pthread_mutex_unlock(&control_lock);
            if (control_payload[0]) {
                lws_send_message(wsi, control_payload);
                if (more_control) lws_callback_on_writable(wsi);
                break;
            }
            bool notice_sent = false;
            pthread_mutex_lock(&notice_lock);
            if (hello_accepted && pending_notice[0] && !notice_waiting_ack) {
                lws_send_message(wsi, pending_notice);
                notice_waiting_ack = true;
                notice_sent_at = time(NULL);
                notice_sent = true;
                RK_LOGI("upload notice sent: msgid=%llu", (unsigned long long)pending_msgid);
            }
            pthread_mutex_unlock(&notice_lock);
            if (notice_sent) break;
            pthread_mutex_lock(&voice_lock);
            if (hello_accepted && pending_voice_status[0]) {
                lws_send_message(wsi, pending_voice_status);
                pending_voice_status[0] = '\0';
            }
            pthread_mutex_unlock(&voice_lock);
            break;
        }

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
                    hello_sent_ms = 0;
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
                    enum snd_id prompt = SND_NULL;
                    pthread_mutex_lock(&notice_lock);
                    if (notice_waiting_ack && cJSON_IsNumber(id) &&
                        (RK_U64)id->valuedouble == pending_msgid && cJSON_IsString(status) &&
                        (strcmp(status->valuestring, "accepted") == 0 ||
                         strcmp(status->valuestring, "rejected") == 0)) {
                        RK_LOGI("upload ack: msgid=%llu status=%s error=%s",
                                (unsigned long long)pending_msgid, status->valuestring,
                                cJSON_IsString(error_code) ? error_code->valuestring : "");
                        if (strcmp(status->valuestring, "accepted") == 0)
                            prompt = SND_UPLOAD_ACCEPTED;
                        else if (cJSON_IsString(error_code) &&
                                 (strcmp(error_code->valuestring, "SERVICE_BUSY") == 0 ||
                                  strcmp(error_code->valuestring, "INTERNAL_ERROR") == 0))
                            prompt = SND_UPLOAD_SERVICE_BUSY;
                        else if (cJSON_IsString(error_code) &&
                                 (strcmp(error_code->valuestring, "FILE_NOT_FOUND") == 0 ||
                                  strcmp(error_code->valuestring, "INVALID_FILE") == 0))
                            prompt = strcmp(pending_upload_cmd, "upload_jpeg") == 0
                                ? SND_UPLOAD_PHOTO_REJECTED : SND_UPLOAD_AUDIO_REJECTED;
                        else
                            prompt = SND_UPLOAD_GENERIC_REJECTED;
                        pending_notice[0] = '\0';
                        pending_upload_cmd[0] = '\0';
                        notice_waiting_ack = false;
                        notice_sent_at = 0;
                    }
                    pthread_mutex_unlock(&notice_lock);
                    if (prompt != SND_NULL) ao_play_snd(prompt);
                    cJSON_Delete(pJsonRoot);
                    break;
                }

                if (strcmp(cmd, "voice_start") == 0) {
                    cJSON *id = cJSON_GetObjectItem(pJsonRoot, "voice_id");
                    cJSON *format = cJSON_GetObjectItem(pJsonRoot, "format");
                    cJSON *rate = cJSON_GetObjectItem(pJsonRoot, "sample_rate");
                    cJSON *channels = cJSON_GetObjectItem(pJsonRoot, "channels");
                    if (cJSON_IsString(id) && id->valuestring && id->valuestring[0] &&
                        strlen(id->valuestring) < sizeof(active_voice_id)) {
                        pthread_mutex_lock(&voice_lock);
                        if (protocol_only_mode || voice_active || ao_playing_voice) {
                            queue_voice_status_locked(id->valuestring, "failed", "PLAYER_BUSY");
                        } else if (!cJSON_IsString(format) || strcmp(format->valuestring, "pcm_s16le") != 0 ||
                                   !cJSON_IsNumber(rate) || rate->valueint != 16000 ||
                                   !cJSON_IsNumber(channels) || channels->valueint != 1) {
                            queue_voice_status_locked(id->valuestring, "failed", "UNSUPPORTED_FORMAT");
                        } else {
                            init_buffer(&ao_play_cb);
                            init_buffer(&ao_voice_cb);
                            strcpy(active_voice_id, id->valuestring);
                            voice_active = true;
                            voice_cancelled = false;
                            voice_received_bytes = 0;
                            voice_played_bytes = 0;
                            voice_total_bytes = 0;
                            voice_end_at = 0;
                        }
                        pthread_mutex_unlock(&voice_lock);
                    }
                    cJSON_Delete(pJsonRoot);
                    break;
                }
                if (strcmp(cmd, "voice_end") == 0) {
                    cJSON *id = cJSON_GetObjectItem(pJsonRoot, "voice_id");
                    cJSON *total = cJSON_GetObjectItem(pJsonRoot, "total_bytes");
                    pthread_mutex_lock(&voice_lock);
                    if (voice_active && cJSON_IsString(id) && strcmp(id->valuestring, active_voice_id) == 0 &&
                        cJSON_IsNumber(total) && total->valuedouble >= 0) {
                        voice_total_bytes = (RK_U64)total->valuedouble;
                        voice_end_at = time(NULL);
                        if (voice_received_bytes > voice_total_bytes) {
                            queue_voice_status_locked(active_voice_id, "failed", "INCOMPLETE_AUDIO");
                            voice_active = false;
                            voice_cancelled = true;
                            init_buffer(&ao_voice_cb);
                        } else {
                            start_voice_player_locked();
                            complete_voice_if_ready_locked();
                        }
                    }
                    pthread_mutex_unlock(&voice_lock);
                    cJSON_Delete(pJsonRoot);
                    break;
                }
                if (strcmp(cmd, "voice_cancel") == 0) {
                    cJSON *id = cJSON_GetObjectItem(pJsonRoot, "voice_id");
                    if (cJSON_IsString(id)) {
                        pthread_mutex_lock(&voice_lock);
                        if (voice_active && strcmp(id->valuestring, active_voice_id) == 0) {
                            voice_active = false;
                            voice_cancelled = true;
                            init_buffer(&ao_voice_cb);
                            active_voice_id[0] = '\0';
                        }
                        queue_voice_status_locked(id->valuestring, "cancelled", NULL);
                        pthread_mutex_unlock(&voice_lock);
                    }
                    cJSON_Delete(pJsonRoot);
                    break;
                }

                handle_platform_command(cmd, pJsonRoot);
                cJSON_Delete(pJsonRoot);
            }else{
                RK_LOGE("cmd parse error\n");
                break;
            }   

            break;
           }

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            printf("cmd 连接失败\n");
            wsi_cmd = NULL;
            hello_accepted = false;
            lwsc_interrupt_platform_recording();
            lwsc_reconnect();
            break;

        case LWS_CALLBACK_CLOSED:
        case LWS_CALLBACK_CLIENT_CLOSED:
            printf("cmd 连接断开\n");
            wsi_cmd = NULL;
            hello_accepted = false;
            lwsc_interrupt_platform_recording();
            lwsc_reconnect();
            break;

        default:
            break;
    }
    return 0;
}

// 事件回调函数
static int ws_callback_voice(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("voice 连接建立成功!\n");// 在这里保存对应的 wsi 句柄
            wsi_voice = wsi;
            voice_connected = true;
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
            pthread_mutex_lock(&voice_lock);
            if (voice_active && len > 0) {
                if (write_to_buffer(&ao_voice_cb, (uint8_t *)in, (int)len) != 0) {
                    queue_voice_status_locked(active_voice_id, "failed", "PLAYBACK_ERROR");
                    voice_active = false;
                    voice_cancelled = true;
                    init_buffer(&ao_voice_cb);
                } else {
                    voice_received_bytes += len;
                    start_voice_player_locked();
                    complete_voice_if_ready_locked();
                }
            }
            pthread_mutex_unlock(&voice_lock);
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
        connect_started_ms = rkipc_get_curren_time_ms();

        while (!quit && !lws_stop && !lws_reconnect_requested) {
            if (lws_service(context, 100) < 0) {
                printf("websocket service error\n");
                lws_reconnect_requested = true;
            }
            long long now_ms = rkipc_get_curren_time_ms();
            if (hello_sent_ms && !hello_accepted &&
                now_ms - hello_sent_ms >= HELLO_ACK_TIMEOUT_SECONDS * 1000LL) {
                RK_LOGE("hello_ack timeout");
                lws_reconnect_requested = true;
            }
            if (connect_started_ms && (!hello_accepted || !voice_connected) &&
                now_ms - connect_started_ms >= CONNECTION_TIMEOUT_SECONDS * 1000LL) {
                RK_LOGE("websocket connection timeout: cmd=%d voice=%d hello=%d",
                        wsi_cmd != NULL, voice_connected, hello_accepted);
                lws_reconnect_requested = true;
            }
            pthread_mutex_lock(&notice_lock);
            bool ack_timed_out = false;
            if (notice_waiting_ack && notice_sent_at &&
                time(NULL) - notice_sent_at >= UPLOAD_ACK_TIMEOUT_SECONDS) {
                RK_LOGE("upload ack timeout: msgid=%llu", (unsigned long long)pending_msgid);
                pending_notice[0] = '\0';
                pending_upload_cmd[0] = '\0';
                notice_waiting_ack = false;
                notice_sent_at = 0;
                ack_timed_out = true;
            }
            bool notice_ready = pending_notice[0] && !notice_waiting_ack;
            pthread_mutex_unlock(&notice_lock);
            if (ack_timed_out) ao_play_snd(SND_UPLOAD_ACK_TIMEOUT);
            if (notice_ready && hello_accepted && wsi_cmd)
                lws_callback_on_writable(wsi_cmd);
            pthread_mutex_lock(&control_lock);
            bool control_ready = control_count != 0;
            pthread_mutex_unlock(&control_lock);
            if (control_ready && hello_accepted && wsi_cmd)
                lws_callback_on_writable(wsi_cmd);
            pthread_mutex_lock(&voice_lock);
            if (voice_active && voice_end_at &&
                time(NULL) - voice_end_at >= VOICE_END_TIMEOUT_SECONDS &&
                voice_received_bytes < voice_total_bytes) {
                queue_voice_status_locked(active_voice_id, "failed", "INCOMPLETE_AUDIO");
                voice_active = false;
                voice_cancelled = true;
                init_buffer(&ao_voice_cb);
            }
            bool voice_status_ready = pending_voice_status[0] != '\0';
            pthread_mutex_unlock(&voice_lock);
            if (voice_status_ready && hello_accepted && wsi_cmd)
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
        hello_sent_ms = 0;
        connect_started_ms = 0;
        lwsc_interrupt_platform_recording();
        pthread_mutex_lock(&control_lock);
        platform_photo_cmd_id[0] = '\0';
        control_head = control_count = 0;
        pthread_mutex_unlock(&control_lock);
        pthread_mutex_lock(&notice_lock);
        pending_notice[0] = '\0';
        pending_upload_cmd[0] = '\0';
        notice_waiting_ack = false;
        notice_sent_at = 0;
        pthread_mutex_unlock(&notice_lock);
        pthread_mutex_lock(&voice_lock);
        voice_active = false;
        voice_cancelled = true;
        active_voice_id[0] = '\0';
        pending_voice_status[0] = '\0';
        voice_received_bytes = voice_played_bytes = voice_total_bytes = 0;
        voice_end_at = 0;
        init_buffer(&ao_voice_cb);
        pthread_mutex_unlock(&voice_lock);

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

int lwsc_send_upload_notice(const char *cmd, const char *file, RK_U64 msgid,
                            const char *cmd_id)
{
    if (!cmd || !file || !lwsc_is_ready()) return -1;
    cJSON *json = cJSON_CreateObject();
    if (!json) return -1;
    cJSON_AddNumberToObject(json, "msgid", (double)msgid);
    cJSON_AddStringToObject(json, "cmd", cmd);
    cJSON_AddStringToObject(json, "file", file);
    if (cmd_id && cmd_id[0]) cJSON_AddStringToObject(json, "cmd_id", cmd_id);
    char *payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!payload) return -1;
    int result = -1;
    pthread_mutex_lock(&notice_lock);
    if (!pending_notice[0] && strlen(payload) < sizeof(pending_notice)) {
        strcpy(pending_notice, payload);
        snprintf(pending_upload_cmd, sizeof(pending_upload_cmd), "%s", cmd);
        pending_msgid = msgid;
        result = 0;
    }
    pthread_mutex_unlock(&notice_lock);
    free(payload);
    if (result == 0 && context) lws_cancel_service(context);
    return result;
}

bool lwsc_voice_was_cancelled(void)
{
    pthread_mutex_lock(&voice_lock);
    bool cancelled = voice_cancelled;
    pthread_mutex_unlock(&voice_lock);
    return cancelled;
}

void lwsc_voice_playback_progress(int bytes, bool failed)
{
    pthread_mutex_lock(&voice_lock);
    if (voice_active) {
        if (failed) {
            queue_voice_status_locked(active_voice_id, "failed", "PLAYBACK_ERROR");
            voice_active = false;
            voice_cancelled = true;
            init_buffer(&ao_voice_cb);
        } else {
            voice_played_bytes += bytes;
            complete_voice_if_ready_locked();
        }
    }
    pthread_mutex_unlock(&voice_lock);
}
