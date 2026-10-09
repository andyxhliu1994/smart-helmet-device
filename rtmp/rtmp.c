// Copyright 2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rtmp.h"

static int g_rtmp_enable[3] = {0, 0, 0};
static VideoParam g_video_param;
static pthread_mutex_t g_rtmp_mutex = PTHREAD_MUTEX_INITIALIZER;

int init_rtmp(int id ) {

    if( strlen(rtmpServerUrl) <= 3 ){
		return -1;
	}

	int ret = 0;
	char entry[128] = {'\0'};
	RK_LOGI("begin\n");
	system("ifconfig lo up");

	// set g_video_param
	memset(&g_video_param, 0, sizeof(g_video_param));
	g_video_param.level = 52;
	g_video_param.width =  mainStreamWidth;
	g_video_param.height = mainStreamHeight;
	g_video_param.bit_rate = 1024 * 1024;
	g_video_param.frame_rate_den = 1 ;
	g_video_param.frame_rate_num = mainStreamFps ;
	memcpy(g_video_param.codec, "H.264", strlen("H.264"));
	g_video_param.profile = 77;//"main"
    //g_video_param.profile = 100;//"high"
	//g_video_param.profile = 66;//"baseline"
	memcpy(g_video_param.format, "NV12", strlen("NV12"));
	// set g_audio_param
	// g_audio_param.channels = rk_param_get_int("audio.0:channels", 2);
	// g_audio_param.sample_rate = rk_param_get_int("audio.0:sample_rate", 16000);
	// g_audio_param.frame_size = rk_param_get_int("audio.0:frame_size", 1024);
	// const char *format = rk_param_get_string("audio.0:format", NULL);
	// if (format)
	// 	memcpy(g_audio_param.format, format, strlen(format));
	// const char *codec = rk_param_get_string("audio.0:encode_type", NULL);
	// if (codec)
	// 	memcpy(g_audio_param.codec, codec, strlen(codec));
	pthread_mutex_lock(&g_rtmp_mutex);
	rkmuxer_init(id + 3, "flv", rtmpServerUrl, &g_video_param, NULL);
	g_rtmp_enable[id] = 1;
	pthread_mutex_unlock(&g_rtmp_mutex);

	return ret;
}

int deinit_rtmp(int id) {

    if( strlen(rtmpServerUrl) <= 3 ){
		return -1;
	}

	RK_LOGI("begin\n");
	pthread_mutex_lock(&g_rtmp_mutex);
	g_rtmp_enable[id] = 0;
	rkmuxer_deinit(id + 3);
	pthread_mutex_unlock(&g_rtmp_mutex);
	RK_LOGI("end\n");

	return 0;
}

int rtmp_write_video_frame(int id, unsigned char *buffer, unsigned int buffer_size,
                              int64_t present_time, int key_frame) {
	int ret = 0;
	pthread_mutex_lock(&g_rtmp_mutex);
	if (g_rtmp_enable[id])
		ret = rkmuxer_write_video_frame(id + 3, buffer, buffer_size, present_time, key_frame);
	pthread_mutex_unlock(&g_rtmp_mutex);

	return ret;
}

int rtmp_write_audio_frame(int id, unsigned char *buffer, unsigned int buffer_size,
                              int64_t present_time) {
	int ret = 0;
	if (g_rtmp_enable[id]) {
		pthread_mutex_lock(&g_rtmp_mutex);
		ret = rkmuxer_write_audio_frame(id + 3, buffer, buffer_size, present_time);
		pthread_mutex_unlock(&g_rtmp_mutex);
	}
	return ret;
}
