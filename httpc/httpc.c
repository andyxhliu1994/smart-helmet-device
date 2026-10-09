#include "httpc.h"

/**
 * 使用 libcurl 上传文件
 * 
 * @param url 上传的目标 URL
 * @param filepath 本地文件路径
 * @return 成功返回 CURLE_OK，失败返回相应的错误码
 */
CURLcode upload_file( const char *url, const char *filepath ) {

    CURLcode res = CURLE_FAILED_INIT;
    if( 0 == strlen(url) ){
        printf("url is null, upload file failed\n");
        return res;
    }
    if (access(filepath, F_OK) == 0) {
        printf("文件存在： %s\n", filepath);
    } else {
        if (errno == ENOENT) {
            printf("文件不存在： %s\n", filepath);
        } else if (errno == EACCES) {
            printf("文件存在，但没有访问权限： %s\n", filepath);
        } else {
            perror("发生错误");
        }
        return res;
    }
    
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
        // 绑定表单数据
        curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
        // 可选：禁用 Expect: 100-continue，提升小文件上传速度
        headerlist = curl_slist_append(headerlist, buf);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);

        int retries = 3 ; // 设置最大重试次数
        while(retries--) {
            res = curl_easy_perform(curl);/* 3. 执行上传请求 */
            if(res == CURLE_OK) {
                printf("Operation successful!\n");
                break; // 成功，退出循环
            } else if(res == CURLE_COULDNT_CONNECT || res == CURLE_OPERATION_TIMEDOUT) {
                printf("Retrying... %d attempts left\n", retries);
                continue; // 失败，但可重试，继续循环
            } else {
                fprintf(stderr, "curl_easy_perform failed: %s\n", curl_easy_strerror(res));
                break; // 其他错误，退出循环
            }
        }

        /* 5. 清理申请的资源 */
        curl_easy_cleanup(curl);
        curl_formfree(formpost);
        curl_slist_free_all(headerlist);
    }
    return res;
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





