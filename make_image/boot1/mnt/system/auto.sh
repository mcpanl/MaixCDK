#!/bin/sh
${CVI_SHOPTS}

export LD_LIBRARY_PATH="/lib:/lib/3rd:/lib/arm-linux-gnueabihf:/usr/lib:/usr/local/lib:/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd:/mnt/data/lib"
export PATH="/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin:/mnt/system/usr/bin:/mnt/system/usr/sbin:/mnt/data/bin:/mnt/data/sbin"


if [ ! -f "/tmp/evb_init" ];then
   echo 1 > /tmp/evb_init
else
   exit 1
fi

if [ -f /mnt/system/ko/loadusbrndis.sh ];then
    /mnt/system/ko/loadusbrndis.sh
fi

chmod +x /maixapp/apps/launcher/launcher
chmod +x /maixapp/app_launcher.py
python /maixapp/app_launcher.py &