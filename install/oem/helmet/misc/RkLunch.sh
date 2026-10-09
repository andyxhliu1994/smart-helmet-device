#!/bin/sh
# Rootfs /etc/profile.d is read-only; set TZ here so rcS / start_services / children see China time.
export TZ=CST-8
echo 5 > /proc/sys/kernel/panic

io -4 0x20834310 32768

date -s "2026-06-10 12:00:00"

[ -f "/oem/bin/rd_profile" ] && source /oem/bin/rd_profile
[ -f "/oem/profile" ] && source /oem/profile


echo "nameserver 223.5.5.5" > /tmp/resolv.conf
echo "nameserver 114.114.114.114" >> /tmp/resolv.conf
echo "nameserver 121.229.145.146" >> /tmp/resolv.conf
echo "nameserver 202.96.134.133" >> /tmp/resolv.conf
echo "nameserver 8.8.8.8" >> /tmp/resolv.conf


rcS()
{

	for i in /oem/usr/etc/init.d/S??* ;do

		# Ignore dangling symlinks (if any).
		[ ! -f "$i" ] && continue

		case "$i" in
			*/S50nginx|*/S50fcgiwrap)
				continue
				;;
		esac

		case "$i" in
			*.sh)
				# Source shell script for speed.
				(
					trap - INT QUIT TSTP
					set start
					. $i
				)
				;;
			*)
				if [ -x "$i" ]; then
					"$i" start
				else
					sh "$i" start
				fi
				;;
		esac
	done
}

check_linker()
{
        [ ! -L "$2" ] && ln -sf $1 $2
}

network_init()
{
	_cnt=0
	while [ "$_cnt" -lt 3 ]; do
		ethaddr1=`ifconfig -a | grep "eth.*HWaddr" | awk '{print $5}'`
		[ -n "$ethaddr1" ] && break
		_cnt=$((_cnt + 1))
		[ "$_cnt" -lt 3 ] && sleep 5
	done

	if [ -f /data/ethaddr.txt ]; then
		ethaddr2=`cat /data/ethaddr.txt`
		if [ $ethaddr1 == $ethaddr2 ]; then
			echo "eth HWaddr cfg ok"
		else
			ifconfig eth0 down
			ifconfig eth0 hw ether $ethaddr2
		fi
	else
		echo $ethaddr1 > /data/ethaddr.txt
	fi
	ifconfig eth0 up && udhcpc -i eth0
	ifconfig lo up
	ifconfig lo 127.0.0.1

	
	eth0_gw=`route -n | grep eth0 | grep UG | awk '{print $2}'`
	route del default gw $eth0_gw dev eth0
	route add default gw $eth0_gw dev eth0 metric 200
}

post_chk()
{
	#TODO: ensure /userdata mount done
	cnt=0
	while [ $cnt -lt 30 ];
	do
		cnt=$(( cnt + 1 ))
		if mount | grep -w userdata; then
			break
		fi
		sleep .1
	done

	# if ko exist, install ko first
	default_ko_dir=/ko
	if [ -f "/oem/usr/ko/insmod_ko.sh" ];then
		default_ko_dir=/oem/usr/ko
	fi
	if [ -f "$default_ko_dir/insmod_ko.sh" ];then
		cd $default_ko_dir && sh insmod_ko.sh && cd -
	fi

	# network_init &
	check_linker /userdata   /oem/usr/www/userdata
	check_linker /media/usb0 /oem/usr/www/usb0
	check_linker /mnt/sdcard /oem/usr/www/sdcard
	# if /data/rkipc not exist, cp /usr/share
	rkipc_ini=/userdata/rkipc.ini
	default_rkipc_ini=/tmp/rkipc-factory-config.ini

	if [ ! -f "/oem/usr/share/rkipc.ini" ]; then
		resolution=$(grep -o "Size:[0-9]*x[0-9]*" /proc/rkisp-vir0 | cut -d':' -f2)
		if [ "$resolution" = "2688x1520" ] ;then
			ln -s -f /oem/usr/share/rkipc-2688x1520.ini $default_rkipc_ini
		fi
		if [ "$resolution" = "3200x1800" ] ;then
			ln -s -f /oem/usr/share/rkipc-3200x1800.ini $default_rkipc_ini
		fi
		if [ "$resolution" = "3840x2160" ] ;then
			ln -s -f /oem/usr/share/rkipc-3840x2160.ini $default_rkipc_ini
		fi
	fi
	tmp_md5=/tmp/.rkipc-ini.md5sum
	data_md5=/userdata/.rkipc-default.md5sum
	md5sum $default_rkipc_ini > $tmp_md5
	chk_rkipc=`cat $tmp_md5|awk '{print $1}'`
	rm $tmp_md5
	if [ ! -f $data_md5 ];then
		md5sum $default_rkipc_ini > $data_md5
	fi
	grep -w $chk_rkipc $data_md5
	if [ $? -ne 0 ] ;then
		rm -f $rkipc_ini
		echo "$chk_rkipc" > $data_md5
	fi

	if [ ! -f "$default_rkipc_ini" ];then
		echo "Error: not found rkipc.ini !!!"
		exit -1
	fi
	if [ ! -s "$rkipc_ini" ]; then
		cp $default_rkipc_ini $rkipc_ini -f
	fi

	# avoid MIPI screen not displaying when startup
	export rt_vo_disable_vop=0

	if [ ! -f "/userdata/image.bmp" ]; then
		cp -fa /oem/usr/share/image.bmp /userdata/
	fi

	if [ -d "/oem/usr/share/iqfiles" ];then
		rkipc -a /oem/usr/share/iqfiles &
	else
		rkipc &
	fi

	sleep 1 # avoid rockti dumpsys connect fail

	# swap sdi0 and sdi1
	rk_mpi_amix_test --control "SAI2 Receive PATH0 Source Select" --value "From SDI1"
	rk_mpi_amix_test --control "SAI2 Receive PATH1 Source Select" --value "From SDI0"
	if [ -f "/oem/usr/share/speaker_test.wav" ];then
		rk_mpi_ao_test -i /oem/usr/share/speaker_test.wav --sound_card_name=default --device_ch=2 --device_rate=8000 --input_rate=8000 --input_ch=2 --set_volume 50
	fi
}

ulimit -c unlimited
echo "/data/core-%p-%e" > /proc/sys/kernel/core_pattern

post_chk &

echo 100 > /sys/devices/platform/pwm-light/hwmon/hwmon2/pwm1

/etc/init.d/S50usbdevice start
telnetd


if [ -f /oem/scripts/chmod_deploy.sh ]; then
	sh /oem/scripts/chmod_deploy.sh
else
	for _sh in /oem/scripts/*.sh /oem/usr/bin/*.sh /oem/echo/*.sh; do
		[ -f "$_sh" ] && chmod +x "$_sh" 2>/dev/null
	done
	for _initf in /oem/usr/etc/init.d/S??* /oem/usr/etc/init.d/S?; do
		[ -f "$_initf" ] && chmod +x "$_initf" 2>/dev/null
	done
fi

_DNS_INIT=/oem/usr/etc/init.d/S40rd_dnsmasq
if [ -f "$_DNS_INIT" ]; then
	if [ -x "$_DNS_INIT" ]; then
		"$_DNS_INIT" start
	else
		sh "$_DNS_INIT" start
	fi
elif [ -x /oem/bin/dnsmasq ] && [ -f /oem/bin/dnsmasq.conf ]; then
	/oem/bin/dnsmasq -C /oem/bin/dnsmasq.conf &
fi

network_init &


# RdCamera / safety_helmet / asr_worker / moni (watchdog) — see start_services.sh
# Use "sh script" not "./script": CRLF or bad shebang causes "not found" on BusyBox
# if [ -f /oem/scripts/start_services.sh ]; then
#	sh /oem/scripts/start_services.sh &
	sleep 2
# fi

if [ -f /oem/helmet/helmet_monitor.sh ]; then
	if ! ps w 2>/dev/null | grep -v grep | grep -q '[h]elmet_monitor.sh'; then
		ps 2>/dev/null | grep -v grep | grep -q '[h]elmet_monitor.sh' || \
		source /oem/helmet/helmet_monitor.sh &
	fi
fi

# chmod +x /oem/usr/bin/mp3player
# [ -f /oem/app/start.mp3 ] && mp3player /oem/app/start.mp3

/usr/bin/amixer sset "ACodec PGA Gain" 30
sh -c "echo 'acodec PAG Gain set 30' > /dev/kmsg"

rcS # fcgi and nginx

network_init

ntpdate time.runde.pro
sleep 10
ntpdate time.runde.pro

sh -c "echo 'Rklunch.sh exit' > /dev/kmsg"



