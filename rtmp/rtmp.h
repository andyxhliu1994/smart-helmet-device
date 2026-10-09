#ifndef __RTMP_H__
#define __RTMP_H__

#include "common.h"
#include "rkmuxer.h"
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

int init_rtmp(int id );
int deinit_rtmp(int id );
int rtmp_write_video_frame(int id, unsigned char *buffer, unsigned int buffer_size,
                              int64_t present_time, int key_frame);
int rtmp_write_audio_frame(int id, unsigned char *buffer, unsigned int buffer_size,
                              int64_t present_time);

#ifdef __cplusplus
}
#endif
#endif