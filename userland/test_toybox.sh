#!/bin/sh
# FNX toybox applet smoke test: one marker per step, sync'ed so a crash
# shows exactly which applet completed.
echo "start" > /s0.log; sync
ls -la / > /s1.log 2>&1; sync
echo "ls done" > /s2.log; sync
cat /bin/sh > /out_cat.txt 2>&1; sync
echo "cat done" > /s3.log; sync
cp /bin/sh /out_cp.txt 2>&1; sync
echo "cp done" > /s4.log; sync
echo "hello world" | grep -c hello > /s5.log 2>&1; sync
echo "grep done" > /s6.log; sync
wc -c /out_cp.txt > /s7.log 2>&1; sync
echo "wc done" > /s8.log; sync
uname -a > /s9.log 2>&1; sync
echo "uname done" > /s10.log; sync
id > /s11.log 2>&1; sync
echo "id done" > /s12.log; sync
mkdir /t_dir && rm -r /t_dir && echo "ok" > /s13.log; sync
echo "mkdir done" > /s14.log; sync
date > /s15.log 2>&1; sync
echo "date done" > /s16.log; sync
ps > /s17.log 2>&1; sync
echo "ps done" > /s18.log; sync
echo "ALL DONE" > /s19.log; sync
exit 0
