#! /bin/sh

sleep 2
echo 3 > /proc/sys/vm/drop_caches
sleep 2
/usr/src/wsg/acache ms-dtrs 1 lru 8 128 22.8 1900000
sleep 2
echo 3 > /proc/sys/vm/drop_caches
sleep 2
/usr/src/wsg/acache ms-dtrs 1 alru 8 128 22.8 1900000
sleep 2
echo 3 > /proc/sys/vm/drop_caches
sleep 2
/usr/src/wsg/acache ms-dtrs 1 lru 12 128 22.8 1900000
sleep 2
echo 3 > /proc/sys/vm/drop_caches
sleep 2
/usr/src/wsg/acache ms-dtrs 1 alru 12 128 22.8 1900000
sleep 2
echo 3 > /proc/sys/vm/drop_caches
sleep 2
