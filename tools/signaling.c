#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include <pthread.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <fcntl.h>

#include <libwebsockets.h>

#include "common/common.h"
#include "common/config.h"
#include "httpfile.h"
#include "httpclient.h"
#include "network.h"

#define SIGNALING_SERVICE_API_CALL_TIMEOUT_IN_SECONDS 2
#define SIGNALING_SERVICE_TCP_KEEPALIVE_IN_SECONDS 3
#define SIGNALING_SERVICE_TCP_KEEPALIVE_PROBE_COUNT 3
#define SIGNALING_SERVICE_TCP_KEEPALIVE_PROBE_INTERVAL_IN_SECONDS 1
#define SIGNALING_SERVICE_WSS_PING_PONG_INTERVAL_IN_SECONDS 10
#define SIGNALING_SERVICE_WSS_HANGUP_IN_SECONDS 60

#define MAX_SIGNALING_MESSAGE_LEN (80 * 1024)
#define LWS_MESSAGE_BUFFER_SIZE (MAX_SIGNALING_MESSAGE_LEN + LWS_PRE + LWS_SEND_BUFFER_POST_PADDING)

#define SIGNAL_MSG_TEMPLATE(CMD, DATA) "{\"role\":\"master\",\"msgid\":\"%s\",\"cmd\":\"" CMD "\"" DATA "}"

enum signal_state
{
  MYSIGNAL_STATE_DISCONN = 0,
  MYSIGNAL_STATE_ESTABLISHED,
  MYSIGNAL_STATE_REGISTERED,
};

static pthread_t tidMySignaling;
static volatile int terminateFlag;

typedef struct mysignal_req
{
  struct lejp_ctx lejpctx;
  lws_map_t *m;
} mysignal_req_t;

struct my_conn
{
  struct lws_context *context;
  lws_sorted_usec_list_t sul; /* schedule connection retry */
  struct lws *lws;            /* related lws if any */
  uint16_t retry_count;       /* count of consequetive retries */
  mysignal_req_t req;
  enum signal_state state;

  // Size of the data in the buffer
  volatile size_t sendBufferSize;
  char sendBuffer[LWS_MESSAGE_BUFFER_SIZE];
};

typedef struct mysignal_cmd
{
  const char *action_str;
  size_t (*cmd)(lws_map_t *m, char *buf, size_t bufsize);
} mysignal_cmd_t;

static const mysignal_cmd_t cmd_list[];

static const char *const tok[] = {
    "dummy___",
};

static signed char json_parse_cb(struct lejp_ctx *ctx, char reason)
{
  mysignal_req_t *req = (mysignal_req_t *)ctx->user;
  lws_map_info_t info;
  memset(&info, 0, sizeof(info));

  switch (reason)
  {
  case LEJPCB_CONSTRUCTED:
    req->m = lws_map_create(&info);
    return 0;
  case LEJPCB_DESTRUCTED:
    lws_map_destroy(&req->m);
    return 0;
  default:
    break;
  }

  if (reason & (LEJPCB_VAL_STR_END | LEJPCB_VAL_NUM_INT | LEJPCB_VAL_NUM_FLOAT))
  {
    lws_map_item_create_ks(req->m, ctx->path, ctx->buf, strlen(ctx->buf) + 1); // 字符串要带结尾符号
  }

  return 0;
}

const lws_retry_bo_t retryPolicy = {
    .conceal_count = LWS_RETRY_CONCEAL_ALWAYS,
    .secs_since_valid_ping = SIGNALING_SERVICE_WSS_PING_PONG_INTERVAL_IN_SECONDS,
    .secs_since_valid_hangup = SIGNALING_SERVICE_WSS_HANGUP_IN_SECONDS,
};

static int has_default_route(void)
{
  FILE *f = fopen("/proc/net/route", "r");
  if (!f)
    return 0;

  char line[256];
  if (!fgets(line, sizeof(line), f))
  {
    fclose(f);
    return 0;
  }

  while (fgets(line, sizeof(line), f))
  {
    char iface[64];
    unsigned long dest = 0;
    unsigned long flags = 0;
    if (sscanf(line, "%63s %lx %*lx %lx", iface, &dest, &flags) >= 3)
    {
      if (dest == 0 && (flags & 0x1))
      {
        fclose(f);
        return 1;
      }
    }
  }

  fclose(f);
  return 0;
}

static void connect_client(lws_sorted_usec_list_t *sul)
{
  extern char gDeviceIdStr[];
  char url_path[200] = "/gateway/websocket/cszxcamera/";
  strcat(url_path, gDeviceIdStr);

  struct lws *lws;
  struct my_conn *mco = lws_container_of(sul, struct my_conn, sul);
  struct lws_client_connect_info i;
  memset(&i, 0, sizeof(i));

  i.context = mco->context;
  i.address = SIGNALING_SERVER_HOST;
  i.port = SIGNALING_SERVER_PORT;
  i.path = url_path;
  i.host = i.address;
  i.origin = i.address;
  i.protocol = "ws";
  if (!strcmp(i.protocol, "wss"))
  {
    i.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_EXPIRED;
  }
  i.local_protocol_name = "lws-minimal-client";
  i.pwsi = &mco->lws;
  i.retry_and_idle_policy = &retryPolicy;
  i.userdata = mco;

  while (!has_default_route() && !terminateFlag)
  {
    lwsl_notice("No default route, waiting for network...\n");
    sleep(1);
  }
  if (terminateFlag)
    return;

  lws = lws_client_connect_via_info(&i);
  if (!lws)
  {
    lwsl_notice("lws_client_connect failed\n");
    /*
     * Failed... schedule a retry... we can't use the _retry_wsi()
     * convenience wrapper api here because no valid wsi at this
     * point.
     */
    /* If connection couldn't be created (e.g. timeout), retry immediately */
    lws_sul_schedule(i.context, 0, sul, connect_client, 1000000);
  }
}

// WebSocket 客户端回调函数
static int lwsWsCallbackRoutine(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *pDataIn, size_t dataSize)
{
  struct my_conn *mco = (struct my_conn *)user;
  mysignal_req_t *req = &(mco->req);
  int m;

  lwsl_debug("WSS callback with reason %d\n", reason);

  switch (reason)
  {
  case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
  {
    lwsl_notice("Client connection failed. Connection error string: %s\n", (const char *)pDataIn);
    if (strstr((const char *)pDataIn, "mbedtls"))
    {
      // 尝试对时
      do_ntp_sync_time();
    }
    mco->state = MYSIGNAL_STATE_DISCONN;
    /* reconnect immediately when a connection error (including timeout) occurs */
    lws_sul_schedule(mco->context, 0, &mco->sul, connect_client, 1000000);
    // lws_retry_sul_schedule(mco->context, 0, &mco->sul, &retryPolicy,
    //                        connect_client, &mco->retry_count);
  }
  break;

  case LWS_CALLBACK_CLIENT_ESTABLISHED:
  {
    lwsl_notice("Connection established.\n");
    MYLOG("Connection established.\n");
    mco->state = MYSIGNAL_STATE_ESTABLISHED;

    lws_callback_on_writable(wsi);
  }
  break;

  case LWS_CALLBACK_CLIENT_RECEIVE:
  {
    if (lws_is_first_fragment(wsi))
    {
      lejp_destruct(&req->lejpctx);
      lejp_construct(&req->lejpctx, json_parse_cb, req, tok, LWS_ARRAY_SIZE(tok));
    }

    m = lejp_parse(&req->lejpctx, (uint8_t *)pDataIn, dataSize);
    if (m < 0 && m != LEJP_CONTINUE)
    {
      lwsl_err("lejp_parse failed %d\n", m);
    }
    else if (m >= 0)
    {
      // 解析成功，执行命令
      struct lws_map_item *cmd = lws_map_item_lookup_ks(req->m, "cmd");
      struct lws_map_item *msgid = lws_map_item_lookup_ks(req->m, "msgid");
      if (cmd)
      {
        lwsl_notice("action_str %s msgid %s\n", lws_map_item_value(cmd),
                    (msgid ? (const char *)lws_map_item_value(msgid) : "NOMSGID"));
        int found_cmd = 0;
        size_t len = 0;
        for (size_t i = 0; cmd_list[i].action_str != NULL; i++)
        {
          if (strncmp(cmd_list[i].action_str, lws_map_item_value(cmd), lws_map_item_value_len(cmd)) == 0)
          {
            found_cmd = 1;
            len = cmd_list[i].cmd(req->m, &mco->sendBuffer[LWS_SEND_BUFFER_PRE_PADDING], MAX_SIGNALING_MESSAGE_LEN);
            break;
          }
        }
        if (!found_cmd)
        {
          lwsl_err("Unknown command: %.*s\n", lws_map_item_value_len(cmd), (const char *)lws_map_item_value(cmd));
          len = snprintf((uint8_t *)&mco->sendBuffer[LWS_SEND_BUFFER_PRE_PADDING], MAX_SIGNALING_MESSAGE_LEN,
                         SIGNAL_MSG_TEMPLATE("no_cmd", ",\"errcode\":%d"
                                                       ",\"errmsg\":\"%s\""),
                         msgid,
                         -1, "Unknown command");
        }
        if (lws_write(wsi, (uint8_t *)&mco->sendBuffer[LWS_SEND_BUFFER_PRE_PADDING], len, LWS_WRITE_TEXT) < len)
        {
          lwsl_err("Failed to send message\n");
        }
      }
    }
  }
  break;

  case LWS_CALLBACK_CLIENT_WRITEABLE:
  {
    if (mco->state < MYSIGNAL_STATE_REGISTERED)
    {
      const char *msg = "{"
                        "\"role\":\"master\","
                        "\"cmd\":\"register\""
                        "}";
      size_t slen = sprintf(&mco->sendBuffer[LWS_SEND_BUFFER_PRE_PADDING], msg);
      m = lws_write(wsi, (uint8_t *)&mco->sendBuffer[LWS_SEND_BUFFER_PRE_PADDING], slen, LWS_WRITE_TEXT);
      if (m == slen)
      {
        mco->state = MYSIGNAL_STATE_REGISTERED;
      }
    }
  }
  break;

  case LWS_CALLBACK_CLIENT_CLOSED:
  {
    lwsl_notice("Connection closed.\n");
    mco->state = MYSIGNAL_STATE_DISCONN;

    /* On closed, immediately try reconnecting instead of waiting */
    lws_sul_schedule(mco->context, 0, &mco->sul, connect_client, 1000000);
  }
  break;

  default:
    break;
  }

  return 0;
}

static void *mySignalRoutine(void *p)
{
  int n;
  struct my_conn *mco = (struct my_conn *)p;
  prctl(PR_SET_NAME, "mysignal");
  terminateFlag = 0;

  const struct lws_protocols lws_protocols[] = {
      {.name = "ws", .callback = lwsWsCallbackRoutine},
      {0},
  };

  // 初始化 libwebsockets 客户端
  struct lws_context_creation_info creationInfo;
  memset(&creationInfo, 0x00, sizeof(creationInfo));
  creationInfo.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  creationInfo.port = CONTEXT_PORT_NO_LISTEN;
  creationInfo.protocols = lws_protocols;
  creationInfo.timeout_secs = SIGNALING_SERVICE_API_CALL_TIMEOUT_IN_SECONDS;
  creationInfo.gid = -1;
  creationInfo.uid = -1;
  // creationInfo.client_ssl_ca_filepath = SSL_CA_PATH;
  // creationInfo.client_ssl_cipher_list = "HIGH:!PSK:!RSP:!eNULL:!aNULL:!RC4:!MD5:!DES:!3DES:!aDH:!kDH:!DSS";
  creationInfo.ka_time = SIGNALING_SERVICE_TCP_KEEPALIVE_IN_SECONDS;
  creationInfo.ka_probes = SIGNALING_SERVICE_TCP_KEEPALIVE_PROBE_COUNT;
  creationInfo.ka_interval = SIGNALING_SERVICE_TCP_KEEPALIVE_PROBE_INTERVAL_IN_SECONDS;
  creationInfo.retry_and_idle_policy = &retryPolicy;

  // 创建上下文
  struct lws_context *context = lws_create_context(&creationInfo);
  if (!context)
  {
    lwsl_err("lws_create_context failed.\n");
    return 0;
  }
  mco->context = context;

  lwsl_notice("mySignalRoutine enter!\n");

  /* schedule the first client connection attempt to happen immediately (no delay) */
  lws_sul_schedule(context, 0, &mco->sul, connect_client, 0);
  while (n >= 0 && !terminateFlag)
    n = lws_service(context, 1000);

  lwsl_notice("mySignalRoutine exit!\n");

  // 清理
  lws_context_destroy(context);
  lejp_destruct(&mco->req.lejpctx);
  free(mco);

  tidMySignaling = 0;

  return 0;
}

void initMySignaling()
{
  init_httpfile();

  // lws_set_log_level(0, NULL);
}

void startMySignaling(const char *session_id)
{
  if (tidMySignaling > 0)
  {
    MYLOG("MySignaling already started!\n");
    return;
  }
  struct my_conn *mco = (struct my_conn *)calloc(1, sizeof(struct my_conn));
  pthread_create(&tidMySignaling, NULL, mySignalRoutine, mco);
}

void stopMySignaling()
{
  terminateFlag = 1;
  if (tidMySignaling > 0)
  {
    pthread_join(tidMySignaling, NULL);
  }
  MYLOG("stopMySignaling!\n");
}

#define CMD_COMMON_BLOCK                                                \
  const char *msgid = NULL;                                             \
  struct lws_map_item *msgid_item = lws_map_item_lookup_ks(m, "msgid"); \
  if (msgid_item)                                                       \
    msgid = lws_map_item_value(msgid_item);

static size_t cmd_do_snapshot(lws_map_t *m, char *buf, size_t bufsize)
{
  CMD_COMMON_BLOCK

  int fd = open("/tmp/snap-cmdx", O_RDONLY, 0644);
  if (fd >= 0)
  {
    char cmd = 0;
    read(fd, &cmd, 1);
    close(fd);
    if (cmd)
    {
      return snprintf(buf, bufsize,
                      SIGNAL_MSG_TEMPLATE("do_snapshot", ",\"errcode\":%d"),
                      msgid,
                      -1);
    }
  }

  FILE *fp = fopen("/tmp/snap/config.json", "w");
  if (fp)
  {
    fprintf(fp, "{\"api\":\"capture_image\",\"data\":{\"save_path\":\""
                "/tmp/snap/iotsnap.jpg"
                "\"}}");
    fclose(fp);
  }

  fd = open("/tmp/snap-cmdx", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0)
  {
    char cmd = 1;
    write(fd, &cmd, 1);
    close(fd);
  }

  printf("Snapshot command executed.\n");

  return snprintf(buf, bufsize,
                  SIGNAL_MSG_TEMPLATE("do_snapshot", ",\"errcode\":%d"),
                  msgid,
                  0);
}

static size_t cmd_do_http(lws_map_t *m, char *buf, size_t bufsize)
{
  CMD_COMMON_BLOCK

  struct lws_map_item *url_item = lws_map_item_lookup_ks(m, "url");
  const char *url = url_item ? (const char *)lws_map_item_value(url_item) : NULL;
  struct lws_map_item *method_item = lws_map_item_lookup_ks(m, "method");
  const char *method = method_item ? (const char *)lws_map_item_value(method_item) : "GET";

  if (!url || strlen(url) == 0)
  {
    lwsl_err("HTTP command missing url\n");
    return snprintf(buf, bufsize,
                    SIGNAL_MSG_TEMPLATE("http", ",\"errcode\":%d"),
                    msgid,
                    -1);
  }

  int errcode = 0;

  return snprintf(buf, bufsize,
                  SIGNAL_MSG_TEMPLATE("http", ",\"errcode\":%d"),
                  msgid,
                  errcode);
}

static void download_pcm_audiofile_callback(const char *url, const char *file_path)
{
}

static size_t cmd_play_pcm_audiofile(lws_map_t *m, char *buf, size_t bufsize)
{
  CMD_COMMON_BLOCK

  const char *url = "";
  struct lws_map_item *url_item = lws_map_item_lookup_ks(m, "url");
  if (url_item)
    url = lws_map_item_value(url_item);

  int timeout = 60;
  struct lws_map_item *timeout_item = lws_map_item_lookup_ks(m, "timeout");
  if (timeout_item)
    timeout = atoi(lws_map_item_value(timeout_item));

  int err = download_file_async(url, SOUND_TEMP_FILE, download_pcm_audiofile_callback, timeout, 512 * 1024);
  return snprintf(buf, bufsize,
                  SIGNAL_MSG_TEMPLATE("play_pcm_audiofile", ",\"errcode\":%d"),
                  msgid,
                  err);
}

static void *firmware_upgrade_routinue(void *arg)
{
  return NULL;
}

static size_t cmd_update_firmware(lws_map_t *m, char *buf, size_t bufsize)
{
  CMD_COMMON_BLOCK

  const char *url = "";
  struct lws_map_item *url_item = lws_map_item_lookup_ks(m, "url");
  if (url_item)
    url = lws_map_item_value(url_item);

  int err = -1;
  FILE *fp = fopen(FWURL_FILE, "w");
  if (fp)
  {
    if (fprintf(fp, "FW_URL=%s\n", url) > 0)
    {
      err = 0;
    }
    fclose(fp);
  }

  pthread_t tid;
  err = pthread_create(&tid, NULL, firmware_upgrade_routinue, NULL);
  if (err == 0)
  {
    pthread_detach(tid);
  }

  return snprintf(buf, bufsize,
                  SIGNAL_MSG_TEMPLATE("update_firmware", ",\"errcode\":%d"),
                  msgid,
                  err);
}

static const mysignal_cmd_t cmd_list[] = {
    {"do_snapshot", cmd_do_snapshot},
    {"play_pcm_audiofile", cmd_play_pcm_audiofile},
    {"update_firmware", cmd_update_firmware},
    {"http", cmd_do_http},
    {NULL, NULL},
};
