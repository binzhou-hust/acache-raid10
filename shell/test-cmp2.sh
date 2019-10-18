#! /bin/sh

sleep 2
echo 3 > /proc/sys/vm/drop_caches
sleep 2
/usr/src/wsg/acache spc-fin 1 lru 8 8 26.99 1280000
sleep 2
echo 3 > /proc/sys/vm/drop_caches
sleep 2
/usr/src/wsg/acache spc-fin 1 alru 8 8 26.99 1280000
sleep 2
echo 3 > /proc/sys/vm/drop_caches
sleep 2
/usr/src/wsg/acache ms-dtrs 1 lru 8 128 15.20 1900000
sleep 2
echo 3 > /proc/sys/vm/drop_caches
sleep 2
/usr/src/wsg/acache ms-dtrs 1 alru 8 128 15.20 1900000
sleep 2
echo 3 > /proc/sys/vm/drop_caches
sleep 2