#!/bin/bash
# prompt-synchronized ACL test harness
cd /home/kyle/Development/Fiwix
LOG=.build/acl_sync.log
rm -f "$LOG"
( while true; do
    if grep -aq '# ' "$LOG" 2>/dev/null; then
      echo "acl_test"; sleep 30
      echo "halt -f"; sleep 2
      break
    fi
    sleep 2
  done ) | FNX_QEMU_BIOS=ovmf ./tools/qemu.sh -nographic -no-reboot -machine pc,usb=off -m 128M -drive file=.build/esp.img,format=raw,if=ide,index=0 -drive file=.build/rootxbfs.img,format=raw,if=none,id=disk -device ich9-ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 > "$LOG" 2>&1 &
echo "harness pid $!"
