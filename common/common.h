
#ifndef _COMMON_H_
#define _COMMON_H_

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <sys/prctl.h>
#include <iconv.h>
#include <dirent.h>
#include <math.h>
#include <sys/select.h>
#include <linux/input.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "rk_debug.h"
#include "rk_defines.h"
#include "rk_mpi_adec.h"
#include "rk_mpi_aenc.h"
#include "rk_mpi_ai.h"
#include "rk_mpi_ao.h"
#include "rk_mpi_avs.h"
#include "rk_mpi_cal.h"
#include "rk_mpi_ivs.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_rgn.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_tde.h"
#include "rk_mpi_vdec.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_vo.h"
#include "rk_mpi_vpss.h"

#include "cJSON.h"

#include <nanomsg/nn.h>
#include <nanomsg/pair.h>
#include <nanomsg/pubsub.h>

#include <websocket/libwebsockets.h>
#include <curl/curl.h>


#if 1
#define ARGB_white 				0xFFFFFFFF	//白色    
#define ARGB_ivory	 			0xFFFFFFF0	//象牙色    
#define ARGB_lightyellow	 	0xFFFFFFE0	//亮黄色    
#define ARGB_yellow	 			0xFFFFFF00	//黄色    
#define ARGB_snow	 			0xFFFFFAFA	//雪白色    
#define ARGB_floralwhite	 	0xFFFFFAF0	//花白色    
#define ARGB_lemonchiffon	 	0xFFFFFACD	//柠檬绸色    
#define ARGB_cornsilk	 		0xFFFFF8DC	//米绸色    
#define ARGB_seashell	 		0xFFFFF5EE	//海贝色    
#define ARGB_lavenderblush	 	0xFFFFF0F5	//淡紫红    
#define ARGB_papayawhip	 		0xFFFFEFD5	//番木色    
#define ARGB_blanchedalmond	 	0xFFFFEBCD	//白杏色    
#define ARGB_mistyrose	 		0xFFFFE4E1	//浅玫瑰色    
#define ARGB_bisque	 			0xFFFFE4C4	//桔黄色    
#define ARGB_moccasin	 		0xFFFFE4B5	//鹿皮色    
#define ARGB_navajowhite	 	0xFFFFDEAD	//纳瓦白    
#define ARGB_peachpuff	 		0xFFFFDAB9	//桃色    
#define ARGB_gold	 			0xFFFFD700	//金色    
#define ARGB_pink	 			0xFFFFC0CB	//粉红色    
#define ARGB_lightpink	 		0xFFFFB6C1	//亮粉红色    
#define ARGB_orange	 			0xFFFFA500	//橙色    
#define ARGB_lightsalmon	 	0xFFFFA07A	//亮肉色    
#define ARGB_darkorange	 		0xFFFF8C00	//暗桔黄色    
#define ARGB_coral	 			0xFFFF7F50	//珊瑚色    
#define ARGB_hotpink	 		0xFFFF69B4	//热粉红色    
#define ARGB_tomato	 			0xFFFF6347	//西红柿色    
#define ARGB_orangered	 		0xFFFF4500	//红橙色    
#define ARGB_deeppink	 		0xFFFF1493	//深粉红色    
#define ARGB_fuchsia	 		0xFFFF00FF	//紫红色    
#define ARGB_magenta	 		0xFFFF00FF	//红紫色    
#define ARGB_red	 			0xFFFF0000	//红色    
#define ARGB_oldlace	 		0xFFFDF5E6	//老花色    
#define ARGB_lightgoldenrodyellow	 0xFFFAFAD2	//亮金黄色    
#define ARGB_linen	 			0xFFFAF0E6	//亚麻色    
#define ARGB_antiquewhite	 	0xFFFAEBD7	//古董白    
#define ARGB_salmon	 			0xFFFA8072	//鲜肉色    
#define ARGB_ghostwhite	 		0xFFF8F8FF	//幽灵白    
#define ARGB_mintcream	 		0xFFF5FFFA	//薄荷色    
#define ARGB_whitesmoke	 		0xFFF5F5F5	//烟白色    
#define ARGB_beige	 			0xFFF5F5DC	//米色    
#define ARGB_wheat	 			0xFFF5DEB3	//浅黄色    
#define ARGB_sandybrown	 		0xFFF4A460	//沙褐色    
#define ARGB_azure	 			0xFFF0FFFF	//天蓝色    
#define ARGB_honeydew	 		0xFFF0FFF0	//蜜色    
#define ARGB_aliceblue	 		0xFFF0F8FF	//艾利斯兰    
#define ARGB_khaki	 			0xFFF0E68C	//黄褐色    
#define ARGB_lightcoral	 		0xFFF08080	//亮珊瑚色    
#define ARGB_palegoldenrod	 	0xFFEEE8AA	//苍麒麟色    
#define ARGB_violet	 			0xFFEE82EE	//紫罗兰色    
#define ARGB_darksalmon	 		0xFFE9967A	//暗肉色    
#define ARGB_lavender	 		0xFFE6E6FA	//淡紫色    
#define ARGB_lightcyan	 		0xFFE0FFFF	//亮青色    
#define ARGB_burlywood	 		0xFFDEB887	//实木色    
#define ARGB_plum	 			0xFFDDA0DD	//洋李色    
#define ARGB_gainsboro	 		0xFFDCDCDC	//淡灰色    
#define ARGB_crimson	 		0xFFDC143C	//暗深红色    
#define ARGB_palevioletred	 	0xFFDB7093	//苍紫罗兰色    
#define ARGB_goldenrod	 		0xFFDAA520	//金麒麟色    
#define ARGB_orchid	 			0xFFDA70D6	//淡紫色    
#define ARGB_thistle	 		0xFFD8BFD8	//蓟色    
#define ARGB_lightgray	 		0xFFD3D3D3	//亮灰色    
#define ARGB_lightgrey	 		0xFFD3D3D3	//亮灰色    
#define ARGB_tan	 			0xFFD2B48C	//茶色    
#define ARGB_chocolate	 		0xFFD2691E	//巧可力色    
#define ARGB_peru	 			0xFFCD853F	//秘鲁色    
#define ARGB_indianred	 		0xFFCD5C5C	//印第安红    
#define ARGB_mediumvioletred	0xFFC71585	//中紫罗兰色    
#define ARGB_silver	 			0xFFC0C0C0	//银色    
#define ARGB_darkkhaki	 		0xFFBDB76B	//暗黄褐色   
#define ARGB_rosybrown	 		0xFFBC8F8F	//褐玫瑰红    
#define ARGB_mediumorchid	 	0xFFBA55D3	//中粉紫色    
#define ARGB_darkgoldenrod	 	0xFFB8860B	//暗金黄色    
#define ARGB_firebrick	 		0xFFB22222	//火砖色    
#define ARGB_powderblue	 		0xFFB0E0E6	//粉蓝色    
#define ARGB_lightsteelblue	 	0xFFB0C4DE	//亮钢兰色   
#define ARGB_paleturquoise	 	0xFFAFEEEE	//苍宝石绿    
#define ARGB_greenyellow	 	0xFFADFF2F	//黄绿色    
#define ARGB_lightblue	 		0xFFADD8E6	//亮蓝色    
#define ARGB_darkgray	 		0xFFA9A9A9	//暗灰色    
#define ARGB_darkgrey	 		0xFFA9A9A9	//暗灰色    
#define ARGB_brown	 			0xFFA52A2A	//褐色    
#define ARGB_sienna	 			0xFFA0522D	//赭色    
#define ARGB_darkorchid	 		0xFF9932CC	//暗紫色    
#define ARGB_palegreen	 		0xFF98FB98	//苍绿色    
#define ARGB_darkviolet	 		0xFF9400D3	//暗紫罗兰色    
#define ARGB_mediumpurple	 	0xFF9370DB	//中紫色    
#define ARGB_lightgreen	 		0xFF90EE90	//亮绿色    
#define ARGB_darkseagreen	 	0xFF8FBC8F	//暗海兰色    
#define ARGB_saddlebrown	 	0xFF8B4513	//重褐色    
#define ARGB_darkmagenta	 	0xFF8B008B	//暗洋红    
#define ARGB_darkred	 		0xFF8B0000	//暗红色    
#define ARGB_blueviolet	 		0xFF8A2BE2	//紫罗兰蓝色    
#define ARGB_lightskyblue	 	0xFF87CEFA	//亮天蓝色    
#define ARGB_skyblue	 		0xFF87CEEB	//天蓝色    
#define ARGB_gray	 			0xFF808080	//灰色    
#define ARGB_grey	 			0xFF808080	//灰色    
#define ARGB_olive	 			0xFF808000	//橄榄色    
#define ARGB_purple	 			0xFF800080	//紫色    
#define ARGB_maroon	 			0xFF800000	//粟色    
#define ARGB_aquamarine	 		0xFF7FFFD4	//碧绿色    
#define ARGB_chartreuse	 		0xFF7FFF00	//黄绿色    
#define ARGB_lawngreen	 		0xFF7CFC00	//草绿色    
#define ARGB_mediumslateblue	0xFF7B68EE	//中暗蓝色    
#define ARGB_lightslategray	 	0xFF778899	//亮蓝灰    
#define ARGB_lightslategrey	 	0xFF778899	//亮蓝灰    
#define ARGB_slategray	 		0xFF708090	//灰石色    
#define ARGB_slategrey	 		0xFF708090	//灰石色    
#define ARGB_olivedrab	 		0xFF6B8E23	//深绿褐色    
#define ARGB_slateblue	 		0xFF6A5ACD	//石蓝色    
#define ARGB_dimgray	 		0xFF696969	//暗灰色    
#define ARGB_dimgrey	 		0xFF696969	//暗灰色    
#define ARGB_mediumaquamarine	0xFF66CDAA	//中绿色    
#define ARGB_cornflowerblue	 	0xFF6495ED	//菊兰色    
#define ARGB_cadetblue	 		0xFF5F9EA0	//军兰色    
#define ARGB_darkolivegreen	 	0xFF556B2F	//暗橄榄绿   
#define ARGB_indigo	 			0xFF4B0082	//靛青色    
#define ARGB_mediumturquoise	0xFF48D1CC	//中绿宝石    
#define ARGB_darkslateblue	 	0xFF483D8B	//暗灰蓝色    
#define ARGB_steelblue	 		0xFF4682B4	//钢兰色    
#define ARGB_royalblue	 		0xFF4169E1	//皇家蓝    
#define ARGB_turquoise	 		0xFF40E0D0	//青绿色    
#define ARGB_mediumseagreen	 	0xFF3CB371	//中海蓝    
#define ARGB_limegreen	 		0xFF32CD32	//橙绿色    
#define ARGB_darkslategray	 	0xFF2F4F4F	//暗瓦灰色    
#define ARGB_darkslategrey	 	0xFF2F4F4F	//暗瓦灰色    
#define ARGB_seagreen	 		0xFF2E8B57	//海绿色    
#define ARGB_forestgreen	 	0xFF228B22	//森林绿    
#define ARGB_lightseagreen	 	0xFF20B2AA	//亮海蓝色    
#define ARGB_dodgerblue	 		0xFF1E90FF	//闪兰色    
#define ARGB_midnightblue	 	0xFF191970	//中灰兰色    
#define ARGB_aqua	 			0xFF00FFFF	//浅绿色    
#define ARGB_cyan	 			0xFF00FFFF	//青色    
#define ARGB_springgreen	 	0xFF00FF7F	//春绿色    
#define ARGB_lime	 			0xFF00FF00	//酸橙色    
#define ARGB_mediumspringgreen	0xFF00FA9A	//中春绿色    
#define ARGB_darkturquoise	 	0xFF00CED1	//暗宝石绿    
#define ARGB_deepskyblue	 	0xFF00BFFF	//深天蓝色    
#define ARGB_darkcyan	 		0xFF008B8B	//暗青色    
#define ARGB_teal	 			0xFF008080	//水鸭色    
#define ARGB_green	 			0xFF008000	//绿色    
#define ARGB_darkgreen	 		0xFF006400	//暗绿色    
#define ARGB_blue	 			0xFF0000FF	//蓝色    
#define ARGB_mediumblue	 		0xFF0000CD	//中兰色    
#define ARGB_darkblue	 		0xFF00008B	//暗蓝色    
#define ARGB_navy	 			0xFF000080	//海军色    
#define ARGB_black	 			0xFF000000	//黑色    
#endif      

extern bool quit;
enum rec_file_t{
	FILE_NULL = 0,
	FILE_AI_G711,
	FILE_AI_PCM,	
	FILE_AO_G711,
	FILE_AO_PCM,	
	FILE_VI_H264,
	FILE_VI_RAW,
	FILE_RKNN,
};
extern FILE *save_file;

#define IVA_CHN_ID 	1

extern RK_U32 g_debug ;
extern RK_U64 g_msgid;

extern char devSn[];
extern RK_U32 mainStreamWidth ;
extern RK_U32 mainStreamHeight;
extern RK_U32 mainStreamFps;	
extern RK_S32 aiSampleRate ;
extern RK_S32 aoSampleRate ;
extern RK_U32 u32Sensitivity ;
extern enum rec_file_t rawFarmeRecType ;	
extern char rawFarmeRecPath[] ;
extern char rawFarmeRecFileName[];

extern RK_S32 aiVolume ;
extern RK_S32 aoVolume ;

extern char osdFontPath[] ;
extern char aivqePath[] ;
extern char aovqePath[] ;
extern char pIVAModelPath[] ;
extern char pRKnnModelPath[] ;
extern char pRKnnModelLabelPath[] ;
extern char nngAiPcmTxPath[];
extern char rtspLocPath[];
extern char rtmpServerUrl[];
extern char httpServerUrl[];

extern char wsServerAddr[];
extern RK_U32 wsServerPort;
extern char wsCmdPath[];
extern char wsVoicePath[];

extern char recTmpFileDir[];
extern char sndFileDir[64] ;

extern bool ispRotation;
extern bool keyEvent;

#define UPALIGNTO(value, align) ((value + align - 1) & (~(align - 1)))
#define UPALIGNTO2(value) UPALIGNTO(value, 2)
#define UPALIGNTO4(value) UPALIGNTO(value, 4)
#define UPALIGNTO16(value) UPALIGNTO(value, 16)
#define DOWNALIGNTO16(value) (UPALIGNTO(value, 16) - 16)
#define MULTI_UPALIGNTO16(grad, value) UPALIGNTO16((int)(grad * value))

enum shm_id{
	SHM_H264 = 0,
	SHM_G711,
	SHM_RAUD,
};
struct shm_set{
	int fd;
	void* ptr;
	uint32_t size;
	char name[8];
	sem_t* semid;
};
extern struct shm_set ipc_shm[];
int init_shm_sem(void);
int deinit_shm_sem(void);

extern pthread_mutex_t obj_det_update_lock;
extern sem_t* obj_det_sem;//用于目标检测输出Osd的通知

//************************************************************************** */
void *rk_signal_create(int defval, int maxval);
void rk_signal_destroy(void *sem);
int rk_signal_wait(void *sem, int timeout);
void rk_signal_give(void *sem);
void rk_signal_reset(void *sem);
//*************************************************************************** */
long long rkipc_get_curren_time_ms();
double __get_us(struct timeval t);
/**************************************************************************** */
void sigterm_handler(int sig);
RK_U64 TEST_COMM_GetNowUs() ;

/**************************************************************************** */
#include "rtsp_demo.h"
extern rtsp_demo_handle g_rtsplive ;
extern rtsp_session_handle g_rtsp_session;

RK_S32 init_rtsp();
RK_S32 deinit_rtsp();

/**************************************************************************** */
extern bool take_photo_one;
RK_S32 upload_lwsc( char* cmd_str, char* file_name, char* file_path );
RK_S32 start_file_rec( enum rec_file_t rec_file_type);
RK_S32 stop_file_rec( void );
RK_S32 take_photo(void);

/**************************************************************************** */

RK_S32 init_common(int argc, char *argv[] );
RK_S32 deinit_common();
int init_app_sh(void);
int check_network(void);

//
void start_vi_vis_iav_rknn_jpeg_thread();
void join_vi_vis_iav_rknn_jpeg_thread();

//
typedef struct obj_i{
	char  		enable;
	int   		type;
	float 		score;
	float 		start_x;
	float 		start_y;
	float 		width;
	float 		hight;
	uint32_t  	color;
}obj_info;

#define OBJ_DET_OSD_MAX 12
typedef struct det_i{
	char 		num;
	float 		w_whole;
	float 		h_whole;
	float 		x_start;
	float 		y_start;
	obj_info 	obj_det[OBJ_DET_OSD_MAX];
}osd_obj_det_info;
extern osd_obj_det_info obj_det_info;

#define mmin(a,b)   ((a)<(b)?(a):(b))
#define mmax(a,b)   ((a)>(b)?(a):(b))

/************************************************************************ */
#define AUDIO_SAMPLE_RATE 		(16000)
#define AUDIO_FRAME_FREQ_HZ 	(50)// 20ms @ 16kHz, S16_LE
#define FRAME_BYTES 			( AUDIO_SAMPLE_RATE * 2 / AUDIO_FRAME_FREQ_HZ )
#define RING_BUFFER_SIZE        ( FRAME_BYTES * AUDIO_FRAME_FREQ_HZ * 30 )    // 缓冲区大小
#define FIXED_READ_LENGTH       ( FRAME_BYTES )   // 每个元素的大小

#define H264_VENC_CHN 0
#define JPEG_VENC_CHN 1

// 环形缓冲区结构体
typedef struct {
    uint8_t buffer[RING_BUFFER_SIZE];  // 缓冲区
    int read_pos;                 // 读指针
    int write_pos;                // 写指针
    int size;                     // 当前缓冲区中的元素个数
} CircularBuffer;
extern CircularBuffer ao_play_cb;
extern int lws_audio_recv_t;
extern bool ao_playing ;

enum snd_id{
	SND_NULL = 0,
	SND_POWERON, //1
	SND_POWEROFF,//2	
	SND_REC_AI_S,//3
	SND_REC_AI_E,//4
	SND_TAKEPHOTO,//5
	SND_SPK_V,    //6
	SND_REC_VI_S, //7
	SND_REC_VI_E, //8
	SND_LED,      //9
	SND_CONNECTING,//10
	SND_CONNECTED,//11
	SND_CONNECTFAILED,//12
};

RK_S32 ao_set_other(RK_S32 s32SetVolume);
void *ao_play_task(void *arg);
int ao_play_snd( enum snd_id snd );

// 初始化缓冲区
void init_buffer(CircularBuffer* cb) ;
// 检查缓冲区是否为空
int is_empty(CircularBuffer* cb);
// 检查缓冲区是否为满
int is_full(CircularBuffer* cb) ;
// 写数据到缓冲区
int write_to_buffer(CircularBuffer* cb, uint8_t* data, int data_len) ;
// 从缓冲区读取数据
int read_from_buffer(CircularBuffer* cb, uint8_t* data, int data_len) ;

/********************************************************** */
int is_process_running(const char *proc_path) ;

/*lws****************************************************** */
extern struct lws *wsi_cmd ;
extern struct lws *wsi_voice ;
//
int init_lwsc(void);
void deinit_lwsc(void);
void lws_send_message(struct lws *wsi, const char *message);

/************************************************************* */
CURLcode upload_file(const char *url, const char *filepath );;

#endif
