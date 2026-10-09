#ifndef _HTTPC_H_
#define _HTTPC_H_

#include "common.h"

void init_httpc(void);
void deinit_httpc(void);



/**
    result = upload_file( httpServerUrl, "/tmp/out.png");
    if (result == CURLE_OK) 
        printf("文件上传成功！\n");
 */

#endif