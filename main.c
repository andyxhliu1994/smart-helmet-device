#include "log.h"
#include "osd.h"
#include "ivs.h"
#include "vi.h"
#include "ai.h"
#include "ao.h"
#include "iva.h"
#include "rknn.h"
#include "rtmp.h"
#include "httpc.h"
#include "lwsc.h"
#include "key.h"

#define POWERON_PROMPT_MARKER "/tmp/helmet_poweron_prompted"
bool protocol_only_mode = false;

static void startup_trace(const char *stage, int result)
{
	if (!getenv("HELMET_DIAG_STARTUP")) return;
	int fd = open("/data/helmet-startup-diag.log", O_WRONLY | O_CREAT | O_APPEND, 0600);
	if (fd < 0) return;
	char line[160];
	int length = snprintf(line, sizeof(line), "%ld %s result=%d\n", (long)time(NULL), stage, result);
	if (length > 0 && length < (int)sizeof(line)) write(fd, line, length);
	fsync(fd);
	close(fd);
}

static void play_poweron_prompt_once(void)
{
	int fd = open(POWERON_PROMPT_MARKER, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd < 0)
		return;
	close(fd);
	ao_play_snd(SND_POWERON);
}

int main(int argc, char *argv[]) {
	RK_S32 s32Ret = RK_FAILURE;
	int ret = -1;
	int uptime = 0;
	int vi_result = 0, ai_result = 0, ao_result = 0;
	const char *diag_stage = getenv("HELMET_DIAG_STARTUP");
	startup_trace("process_start", 0);
	

	if ( 0 != ( s32Ret = init_common( argc, argv ) )  ){
		RK_LOGE("init common fail! %d", s32Ret );
		goto __FAILED;
	}
	startup_trace("init_common_done", s32Ret);
	/* 仅供联调：验证设备自身的 WebSocket 链路，不占用摄像头/音频/按键。 */
	if (getenv("HELMET_PROTOCOL_ONLY") && strcmp(getenv("HELMET_PROTOCOL_ONLY"), "1") == 0) {
		protocol_only_mode = true;
		init_httpc();
		init_lwsc();
		while (!quit) sleep(1);
		deinit_lwsc();
		deinit_httpc();
		deinit_common();
		return 0;
	}
	if (diag_stage && strcmp(diag_stage, "app-sh") == 0) {
		int result = init_app_sh();
		startup_trace("init_app_sh_done", result);
		deinit_common();
		return result;
	}
	if( 0 != init_app_sh() ) {
		RK_LOGE("init sh fail!" );
		goto __FAILED;
	}
	startup_trace("init_app_sh_done", 0);
	if (RK_MPI_SYS_Init() != RK_SUCCESS) {
		RK_LOGE("rk mpi sys init fail!");
		goto __FAILED_COMMON;
	}
	startup_trace("rk_mpi_sys_init_done", 0);
	if (diag_stage && strcmp(diag_stage, "mpi") == 0) {
		RK_MPI_SYS_Exit();
		startup_trace("rk_mpi_sys_exit_done", 0);
		deinit_common();
		return 0;
	}

	/************** */
	
	startup_trace("init_vi_begin", 0);
	vi_result = init_vi( mainStreamWidth, mainStreamHeight, mainStreamFps );
	startup_trace("init_vi_done", vi_result);
	if (diag_stage && strcmp(diag_stage, "vi") == 0) {
		if (vi_result == 0) deinit_vi();
		startup_trace("init_vi_exit_done", vi_result);
		RK_MPI_SYS_Exit();
		deinit_common();
		return vi_result;
	}
	ai_result = init_ai( aiSampleRate );
	startup_trace("init_ai_done", ai_result);
	ao_result = init_ao();
	startup_trace("init_ao_done", ao_result);
	play_poweron_prompt_once();
	init_osd();
	startup_trace("init_osd_done", 0);
	init_broder_osd();
	init_ivs( mainStreamWidth, mainStreamHeight, u32Sensitivity);
	startup_trace("init_ivs_done", 0);
	start_vi_vis_iav_rknn_jpeg_thread();
	startup_trace("media_threads_started", 0);
	init_iva();
	init_rknn();
	startup_trace("ai_models_init_done", 0);
	init_keys();
	startup_trace("init_keys_done", 0);

	if(  0 == check_network() ){
		init_rtmp(0);
		init_rtsp();
		init_httpc();
		init_lwsc();
		startup_trace("network_clients_started", 0);
	}

	/************** */
	while (!quit) {
		sleep(1);
		uptime++;
	}
	/************** */	
	
	join_vi_vis_iav_rknn_jpeg_thread();
	deinit_osd();
	deinit_broder_osd();
	deinit_vi();
	deinit_ai();
	deinit_ao();
	deinit_keys();
	deinit_rtsp();
	deinit_ivs();
	deinit_iva();
	deinit_rknn();
	deinit_rtmp(0);
	deinit_httpc();
	deinit_lwsc();

	/*********************************************** */
	RK_MPI_SYS_Exit();
	ret = 0;
	/*********************************************** */

__FAILED_COMMON:
	deinit_common();

__FAILED:
	RK_LOGW("exit:%d", s32Ret);
	return ret;
}
