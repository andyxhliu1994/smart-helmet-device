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


int main(int argc, char *argv[]) {
	RK_S32 s32Ret = RK_FAILURE;
	int ret = -1;
	int uptime = 0;
	

	if ( 0 != ( s32Ret = init_common( argc, argv ) )  ){
		RK_LOGE("init common fail! %d", s32Ret );
		goto __FAILED;
	}
	if( 0 != init_app_sh() ) {
		RK_LOGE("init sh fail!" );
		goto __FAILED;
	}
	if (RK_MPI_SYS_Init() != RK_SUCCESS) {
		RK_LOGE("rk mpi sys init fail!");
		goto __FAILED_COMMON;
	}

	/************** */
	
	init_vi( mainStreamWidth, mainStreamHeight, mainStreamFps ) ;
	init_ai( aiSampleRate );
	init_ao();
	ao_play_snd( SND_POWERON );
	init_osd();
	init_broder_osd();
	init_ivs( mainStreamWidth, mainStreamHeight, u32Sensitivity);
	start_vi_vis_iav_rknn_jpeg_thread();
	init_iva();
	init_rknn();
	init_keys();

	if(  0 == check_network() ){
		init_rtmp(0);
		init_rtsp();
		init_httpc();
		init_lwsc();
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
