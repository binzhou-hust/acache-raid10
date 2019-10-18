#! /bin/sh

sleep 2
echo 3 > /proc/sys/vm/drop_caches
sleep 2
#/home/binbin/zhoubin/raid-10/A-Cache-Final/acache spc-fin 1 lru 8 8 26.99 1280000
sleep 2
echo 3 > /proc/sys/vm/drop_caches
sleep 2
/home/binbin/zhoubin/raid-10/A-Cache-Final/acache spc-fin 1 alru 8 8 26.99 1280000
sleep 2
echo 3 > /proc/sys/vm/drop_caches
sleep 2
