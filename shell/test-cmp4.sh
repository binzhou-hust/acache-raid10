#! /bin/sh

sleep 2
echo 3 > /proc/sys/vm/drop_caches
sleep 2
/usr/src/wsg/acache ms-dtrs 1 lru 8 512 178.00 5200000
sleep 2
echo 3 > /proc/sys/vm/drop_caches
sleep 2
/usr/src/wsg/acache ms-dtrs 1 alru 8 512 178.00 5200000
sleep 2
echo 3 > /proc/sys/vm/drop_caches
sleep 2
