#include "httpc.h"

/**
 * 使用 libcurl 上传文件
 * 
 * @param url 上传的目标 URL
 * @param filepath 本地文件路径
 * @return 成功返回 CURLE_OK，失败返回相应的错误码
 */
struct upload_response { char text[4096]; size_t length; };

static size_t receive_upload_response(char *data, size_t size, size_t count, void *user)
{
    struct upload_response *response = (struct upload_response *)user;
    size_t bytes = size * count;
    if (bytes >= sizeof(response->text) - response->length)
        return 0;
    memcpy(response->text + response->length, data, bytes);
    response->length += bytes;
    response->text[response->length] = '\0';
    return bytes;
}

int upload_file(const char *url, const char *filepath, char *saved_file, size_t saved_file_size) {

    struct upload_response response = {{0}, 0};
    long http_status = 0;
    int result = -1;
    if (!url || !*url || !filepath || !saved_file || !saved_file_size ||
        access(filepath, R_OK) != 0) return -1;
    saved_file[0] = '\0';

    CURL *curl;
    // 初始化 libcurl
    curl = curl_easy_init();
    if(curl) {
        struct curl_httppost *formpost = NULL;
        struct curl_httppost *lastptr = NULL;
        struct curl_slist *headerlist = NULL;
        static const char buf[] = "Expect:";
        /* 1. 构建 HTTP multipart 表单 */
        // 添加文件部分
        curl_formadd(&formpost, &lastptr,
                     CURLFORM_COPYNAME, "file",
                     CURLFORM_FILE, filepath,
                     CURLFORM_END);
        /* 2. 设置 libcurl 请求选项 */
        // 设置目标 URL
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_upload_response);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        // 绑定表单数据
        curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
        // 可选：禁用 Expect: 100-continue，提升小文件上传速度
        headerlist = curl_slist_append(headerlist, buf);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);

        CURLcode code = curl_easy_perform(curl);
        if (code == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
        if (code == CURLE_OK && http_status == 200) {
            cJSON *body = cJSON_Parse(response.text);
            if (body) {
                cJSON *business_code = cJSON_GetObjectItemCaseSensitive(body, "code");
                cJSON *file = cJSON_GetObjectItemCaseSensitive(body, "file");
                if (cJSON_IsNumber(business_code) && business_code->valueint == 0 &&
                    cJSON_IsString(file) && file->valuestring && file->valuestring[0] &&
                    strlen(file->valuestring) < saved_file_size) {
                    strcpy(saved_file, file->valuestring);
                    result = 0;
                }
                cJSON_Delete(body);
            }
        }
        if (result != 0) RK_LOGE("upload failed: curl=%d HTTP=%ld response=%s", (int)code, http_status, response.text);

        /* 5. 清理申请的资源 */
        curl_easy_cleanup(curl);
        curl_formfree(formpost);
        curl_slist_free_all(headerlist);
    }
    return result;
}

void init_httpc(void)
{
    if( strlen(httpServerUrl) <= 5 )
        return;
    // 全局初始化（整个程序生命周期只需调用一次）
    curl_global_init(CURL_GLOBAL_ALL);
}

void deinit_httpc(void)
{
    if( strlen(httpServerUrl) <= 5 )
        return;
    // 全局清理
    curl_global_cleanup();
}




