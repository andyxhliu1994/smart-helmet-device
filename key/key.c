#include "key.h"

const char *device_paths[] = {
    "/dev/input/event0",
    "/dev/input/event2",
    "/dev/input/event3",    
};
#define MAX_DEVICES 5
int fds[MAX_DEVICES];
int num_devices = 0;
int max_fd = -1;
/***************************************** 
录音 
设备 /dev/input/event3 发生事件 -> 类型: 1, 代码: 62, 值: 1
设备 /dev/input/event3 发生事件 -> 类型: 1, 代码: 62, 值: 0
开关机
设备 /dev/input/event0 发生事件 -> 类型: 1, 代码: 116, 值: 1
设备 /dev/input/event0 发生事件 -> 类型: 1, 代码: 116, 值: 0
拍照
设备 /dev/input/event3 发生事件 -> 类型: 1, 代码: 61, 值: 1
设备 /dev/input/event3 发生事件 -> 类型: 1, 代码: 61, 值: 0
音量
设备 /dev/input/event2 发生事件 -> 类型: 1, 代码: 115, 值: 1
设备 /dev/input/event2 发生事件 -> 类型: 1, 代码: 115, 值: 0
sos
设备 /dev/input/event2 发生事件 -> 类型: 1, 代码: 59, 值: 1
设备 /dev/input/event2 发生事件 -> 类型: 1, 代码: 59, 值: 0
灯
设备 /dev/input/event2 发生事件 -> 类型: 1, 代码: 60, 值: 1
设备 /dev/input/event2 发生事件 -> 类型: 1, 代码: 60, 值: 0

**************************************** */
unsigned int light_brightness_class = 0;
unsigned int spk_volume_class = 0;
bool powerkey_en = false;

void *key_event_task(void *arg)
{
    fd_set readfds;
    printf("成功监听 %d 个设备，正在等待输入...\n", num_devices);

    while ( !quit ) {

        FD_ZERO(&readfds);
        for (int i = 0; i < num_devices; i++) {
            if (fds[i] >= 0) 
                FD_SET(fds[i], &readfds);
        }
        int activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            if (errno == EINTR) continue; // 被信号打断，继续循环
            perror("select 错误");
            break;
        }

        // 4. 检查哪个文件描述符触发了事件
        struct input_event ev;
        for (int i = 0; i < num_devices; i++) {
            if (fds[i] >= 0 && FD_ISSET(fds[i], &readfds)) {
                // 循环读取，直到读完缓冲区中的所有事件
                while (read(fds[i], &ev, sizeof(struct input_event)) > 0) {
                    // 过滤掉多余的同步事件 (EV_SYN) ，只打印有效的输入类型
                    if (ev.type != EV_SYN) {
                        printf("设备 %s 发生事件 -> 类型: %d, 代码: %d, 值: %d\n", 
                               device_paths[i], ev.type, ev.code, ev.value);

                        if( ev.code == 116 ){ //开关机
                            powerkey_en = (bool)ev.value ;

                        }else if( ev.code == 115  ){//音量
                            if( 0 == ev.value ){
                                spk_volume_class ++;
                                if( spk_volume_class%3 == 1 )
                                    ao_set_other( 99 );
                                else if ( spk_volume_class%3 == 2 )
                                    ao_set_other( 20 );
                                else if ( spk_volume_class%3 == 0 )
                                    ao_set_other( 50 );
                                ao_play_snd( SND_SPK_V );
                            }
                        }else if( ev.code == 62  ){//录音
                            if( 1 == ev.value ){
                                start_file_rec( FILE_AI_PCM );
                                ao_play_snd( SND_REC_AI_S );
                            }else{
                                stop_file_rec();
                                ao_play_snd( SND_REC_AI_E );
                            }
                        }else if( ev.code == 61  ){//拍照  
                            if( 0 == ev.value ){
                                take_photo();
                                ao_play_snd( SND_TAKEPHOTO );
                            }

                        }else if( ev.code == 60  ){//灯
                            if( 0 == ev.value ){
                                light_brightness_class ++;
                                if( light_brightness_class%3 == 1 )
                                    system("echo 60 > /sys/devices/platform/pwm-light/hwmon/hwmon2/pwm1");
                                else if ( light_brightness_class%3 == 2 )
                                    system("echo 255 > /sys/devices/platform/pwm-light/hwmon/hwmon2/pwm1");
                                else if ( light_brightness_class%3 == 0 )
                                    system("echo 0 > /sys/devices/platform/pwm-light/hwmon/hwmon2/pwm1");
                                ao_play_snd( SND_LED );
                            }
                        }else if( ev.code == 59  ){//sos
                            if( 1 == ev.value ){
                                start_file_rec( FILE_VI_H264 );
                                ao_play_snd( SND_REC_VI_S );
                            }else{
                                stop_file_rec();
                                ao_play_snd( SND_REC_VI_E );
                            }
                        }
                    }
                }
            }
        }
    }
    RK_LOGW("key event task end");
    return NULL;
}

void *power_task(void *arg)
{
    unsigned int key_t = 0;
    while(1){
        sleep(1);
        if( powerkey_en ){
             key_t++;
             if( key_t >= 4 ){
                key_t = 0;
                ao_play_snd( SND_POWEROFF );
                RK_LOGW("will poweroff");
                sleep(2);
                system("/sbin/poweroff");
             }
        }else 
            key_t =0;
    }
    return NULL;
}


int init_keys(void)
{
    if( !keyEvent )
        return -1;

    num_devices = sizeof(device_paths) / sizeof(device_paths[0]);
    for (int i = 0; i < num_devices; i++) {
        fds[i] = open(device_paths[i], O_RDONLY | O_NONBLOCK); // 设为非阻塞
        if (fds[i] < 0) {
            perror("无法打开设备");
            continue; // 处理打开失败，可选择跳过或退出
        }
        // 记录最大的文件描述符，select 需要它
        if (fds[i] > max_fd) 
            max_fd = fds[i];
    }

    if (max_fd == -1) {
        printf("没有成功打开的设备，程序退出。\n");
        return 1;
    }
    
    pthread_t key_thread;
    pthread_create(&key_thread, NULL, key_event_task, NULL);
    pthread_detach(key_thread); 

    pthread_t power_thread;
    pthread_create(&power_thread, NULL, power_task, NULL);
    pthread_detach(power_thread);    
    
    return 0;
}


void deinit_keys(void)
{
    if( !keyEvent )
        return;
    //关闭文件描述符
    for (int i = 0; i < num_devices; i++) {
        if (fds[i] >= 0) 
            close(fds[i]);
    }
}



