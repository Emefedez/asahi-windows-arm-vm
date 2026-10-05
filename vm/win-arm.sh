#!/bin/bash
# Windows 11 on Arm under QEMU/KVM on Apple Silicon (Asahi).
#
#   ./win-arm.sh start     boot with the settings in vm.conf (GPU=2d|3d); focuses the window if running
#   ./win-arm.sh stop      ACPI shutdown (like pressing the power button)
#   ./win-arm.sh kill      force off
#   ./win-arm.sh status    prints running/stopped
#   ./win-arm.sh install   first install: ramfb display, Windows + virtio-win ISOs over USB
#   ./win-arm.sh run       2D virtio-gpu (viogpudo driver, resizable display)
#   ./win-arm.sh run3d     virtio-gpu with Venus/blob for the Yttrium 3D driver
#   ./win-arm.sh perf eco|balanced|performance   set the profile (live if running, saved to vm.conf)
#   ./win-arm.sh memory <MiB>                     live memory limit via the balloon driver
#   ./win-arm.sh refresh 60|120                   guest display refresh (Yttrium; applies after guest reboot)
#   ./win-arm.sh usb list|attached|attach VID:PID|detach VID:PID   USB passthrough (live)
#   ./win-arm.sh set KEY VALUE                    change vm.conf (next start)
#   ./win-arm.sh info                             current state, one key=value per line
#
# Settings: vm.conf (VCPUS, MEM in MiB, GPU, HOSTMEM, FULLSCREEN, PERF, SCALE); environment overrides it.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

mode=${1:-start}
conf=()
[[ -r vm.conf ]] && mapfile -t conf < <(grep -E '^[A-Z_]+=' vm.conf)
for kv in "${conf[@]}"; do name=${kv%%=*}; [[ -n ${!name:-} ]] || declare "$kv"; done  # env wins
VCPUS=${VCPUS:-8}
MEM=${MEM:-8192}
GPU=${GPU:-2d}
HOSTMEM=${HOSTMEM:-4G}
FULLSCREEN=${FULLSCREEN:-no}
PERF=${PERF:-performance}
# Guest pixels per host *logical* pixel. auto = 1/monitor scale, so on a 2x HiDPI panel the guest
# renders at physical resolution (sharp; set Windows display scaling to 200%).
SCALE=${SCALE:-auto}
# Apple P-cores. M1 P- and E-cores have different PMUs; Windows divides by the
# cycle counter rate and crashes if a vCPU migrates to a core whose emulated PMU
# behaves differently (fixed by KVM_ARM_VCPU_PMU_V3_FIXED_COUNTERS_ONLY, not in this kernel yet).
PCORES=${PCORES:-$(for p in /sys/devices/system/cpu/cpufreq/policy*; do
  [[ $(<"$p/cpuinfo_max_freq") -ge 3000000 ]] && cat "$p/related_cpus"; done | tr ' \n' ',,' | sed 's/,*$//')}

qemu_pid() {
  local p
  while read -r p; do
    [[ -n $p && $(readlink "/proc/$p/cwd" 2>/dev/null) == "$PWD" ]] && { echo "$p"; return; }
  done < <(pgrep -f '^qemu-system-aarch64 -name win-arm( |$)' || true)
}
running() { [[ -n $(qemu_pid) ]]; }
monitor() { echo "$*" | socat - UNIX-CONNECT:monitor.sock >/dev/null; }
# Monitor command with its output (HMP echoes the prompt; keep only the answer lines).
monitor_out() { { echo "$*"; sleep 0.3; } | socat - UNIX-CONNECT:monitor.sock | tr -d '\r' | grep -vE '^(QEMU |\(qemu\))' ; }
focus() {
  local address
  address=$(hyprctl clients -j | python3 -c 'import json,sys; print(next((w["address"] for w in json.load(sys.stdin) if w["class"] == "qemu" and w["title"] == "QEMU (win-arm)"), ""))')
  [[ $address =~ ^0x[0-9a-fA-F]+$ ]] || return 0
  hyprctl dispatch "hl.dsp.focus({ window = \"address:$address\" })" >/dev/null
}
set_conf() { # key value
  if grep -q "^$1=" vm.conf 2>/dev/null; then sed -i "s|^$1=.*|$1=$2|" vm.conf; else echo "$1=$2" >>vm.conf; fi
}

# Performance profiles: uclamp min/max for every QEMU thread (schedutil picks CPU clocks from it),
# and whether to ask muvm-perf-governor (root service) for the performance governor while running.
#   eco          cap boosting (quiet, battery)       uclamp 0..512
#   balanced     modest floor                         uclamp 512..1024
#   performance  always fast + performance governor  uclamp 1024..1024
uclamp_for() { case $1 in eco) echo "0 512" ;; performance) echo "1024 1024" ;; *) echo "512 1024" ;; esac; }
perf_request() { # the root governor service reads this while QEMU runs
  echo "$1" > "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/win-arm.perf"
}
apply_perf() { # profile [pid]
  local lo hi; read -r lo hi < <(uclamp_for "$1")
  perf_request "$1"
  [[ -n ${2:-} ]] && uclampset -a -m "$lo" -M "$hi" -p "$2" >/dev/null
}

case $mode in
  status) running && echo running || echo stopped; exit 0 ;;
  stop)   running && monitor system_powerdown; exit 0 ;;
  kill)   running && monitor quit; exit 0 ;;
  set)
    [[ ${2:-} =~ ^(VCPUS|MEM|GPU|HOSTMEM|FULLSCREEN|PERF|SCALE)$ ]] || { echo "unknown setting ${2:-}" >&2; exit 2; }
    set_conf "$2" "${3:?value}"; exit 0 ;;
  perf)
    [[ ${2:-} =~ ^(eco|balanced|performance)$ ]] || { echo "usage: $0 perf eco|balanced|performance" >&2; exit 2; }
    set_conf PERF "$2"
    running && apply_perf "$2" "$(qemu_pid)"
    exit 0 ;;
  usb)
    # USB passthrough (QEMU usb-host). Needs device access: sudo host/install-usb-access.sh (udev uaccess).
    #   usb list | usb attached | usb attach VID:PID | usb detach VID:PID
    case ${2:-list} in
      list)
        for d in /sys/bus/usb/devices/*; do
          [[ -f $d/idVendor && $(cat "$d/bDeviceClass") != 09 ]] || continue   # skip hubs
          vp="$(<"$d/idVendor"):$(<"$d/idProduct")"
          dev=/dev/bus/usb/$(printf %03d "$(<"$d/busnum")")/$(printf %03d "$(<"$d/devnum")")
          acc=no; [[ -r $dev && -w $dev ]] && acc=yes
          echo "$vp | $(cat "$d/manufacturer" 2>/dev/null) $(cat "$d/product" 2>/dev/null) | access=$acc"
        done ;;
      attached) running && monitor_out info usb ;;
      attach|detach)
        [[ ${3:-} =~ ^[0-9a-fA-F]{4}:[0-9a-fA-F]{4}$ ]] || { echo "usage: $0 usb $2 VID:PID" >&2; exit 2; }
        running || { echo "VM not running" >&2; exit 1; }
        id=usb-${3/:/-}
        if [[ $2 == attach ]]; then
          monitor_out "device_add usb-host,bus=xhci.0,vendorid=0x${3%:*},productid=0x${3#*:},id=$id"
        else
          monitor_out "device_del $id"
        fi ;;
      *) echo "usage: $0 usb list|attached|attach VID:PID|detach VID:PID" >&2; exit 2 ;;
    esac
    exit 0 ;;
  _sync-refresh)
    # Started in the background by "start": once the guest agent answers, make the Yttrium driver's
    # registry match vm.conf / the display: RefreshRate (REFRESH) and HWCursorScale (host cursor size;
    # 100 x the GTK display scale, i.e. 50 on a 2x HiDPI panel with SCALE=auto). The driver reads both at
    # start, so a change applies at the next Windows restart.
    want_rr=${REFRESH:-60}
    gs=${GTK_SCALE:-1}
    want_cs=$(python3 -c "s=float('$gs'); print(25 if s<=0.3 else 50 if s<=0.6 else 100)")
    for _ in $(seq 120); do ./ga.sh ping >/dev/null 2>&1 && break; sleep 5; done
    out=$(./ga.sh "\$changed=\$false; foreach (\$k in @(Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}' -EA SilentlyContinue | Where-Object { (Get-ItemProperty \$_.PSPath).DriverDesc -match 'VirtIO GPU 3D' })) { \$p=Get-ItemProperty \$k.PSPath; if (\$p.RefreshRate -ne $want_rr) { Set-ItemProperty \$k.PSPath -Name RefreshRate -Value $want_rr; \$changed=\$true }; if (\$p.HWCursorScale -ne $want_cs) { Set-ItemProperty \$k.PSPath -Name HWCursorScale -Value $want_cs; \$changed=\$true } }; if (\$changed) { 'changed' }" 2>/dev/null | tr -d '\r')
    [[ $out == *changed* ]] && notify-send "Windows VM" "Display settings updated (${want_rr} Hz, cursor ${want_cs}%) — restart Windows to apply" 2>/dev/null
    exit 0 ;;
  refresh)
    # Guest display refresh (Yttrium driver "RefreshRate" registry value; read at driver start, so it
    # applies after the guest reboots). 60 is the known-good value; 120 is experimental (purple flashes
    # were seen with an earlier 120 Hz timing).
    [[ ${2:-} =~ ^(60|120)$ ]] || { echo "usage: $0 refresh 60|120" >&2; exit 2; }
    set_conf REFRESH "$2"
    if running; then
      # Every Yttrium instance (reinstalls can leave several device keys; only one is active).
      ./ga.sh "\$ks=@(Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}' -EA SilentlyContinue | Where-Object { (Get-ItemProperty \$_.PSPath).DriverDesc -match 'VirtIO GPU 3D' }); foreach (\$k in \$ks) { Set-ItemProperty \$k.PSPath -Name RefreshRate -Value $2 }; if (\$ks) { 'RefreshRate=$2 on ' + \$ks.Count + ' instance(s) (reboot Windows to apply)' } else { 'Yttrium 3D driver not installed' }"
    else
      echo "saved; applied by the guest agent on the next run (win-arm.sh refresh $2 while running)"
    fi
    exit 0 ;;
  memory)
    [[ ${2:-} =~ ^[0-9]+$ ]] || { echo "usage: $0 memory <MiB>" >&2; exit 2; }
    running || { echo "not running" >&2; exit 1; }
    (( $2 <= MEM )) || { echo "max is the boot size, $MEM MiB" >&2; exit 1; }
    monitor "balloon $2"; exit 0 ;;
  info)
    echo "state=$(running && echo running || echo stopped)"
    echo "perf=$PERF vcpus=$VCPUS mem=$MEM gpu=$GPU hostmem=$HOSTMEM fullscreen=$FULLSCREEN scale=$SCALE refresh=${REFRESH:-60}"
    if running; then
      pid=$(qemu_pid)
      echo "uclamp=$(uclampset -p "$pid" | sed -n 's/.*min: \([0-9]*\) max: \([0-9]*\).*/\1..\2/p')"
      echo "balloon=$(monitor_out info balloon | sed -n 's/.*actual=\([0-9]*\).*/\1/p')"
      echo "host_cpu_rss_mib=$(( $(awk '/VmRSS/{print $2}' /proc/$pid/status) / 1024 ))"
    fi
    echo "governor=$(sort -u /sys/devices/system/cpu/cpufreq/policy*/scaling_governor | paste -sd,)"
    grep -q 'win-arm.perf' /usr/local/bin/muvm-perf-governor 2>/dev/null && echo "governor_helper=yes" || echo "governor_helper=no"
    exit 0 ;;
  start)
    if running; then focus; exit 0; fi
    [[ $GPU == 3d ]] && mode=run3d || mode=run
    ;;
esac
if running; then echo "win-arm is already running" >&2; exit 1; fi

DISK=disk.qcow2
VARS=efivars.fd
TPMDIR=$PWD/tpm
WIN_ISO=${WIN_ISO:-$(ls -t "$HOME"/Downloads/*.iso 2>/dev/null | grep -iE 'win.*(arm|a64)|arm64' | head -1 || true)}
VIRTIO_ISO=$(ls -t virtio-win-*.iso 2>/dev/null | head -1)

[[ -f $DISK ]] || qemu-img create -f qcow2 -o cluster_size=64k "$DISK" 256G
[[ -f $VARS ]] || cp /usr/share/AAVMF/AAVMF_VARS.fd "$VARS"

# TPM 2.0 for Windows 11, kept alive only as long as QEMU.
mkdir -p "$TPMDIR"
swtpm socket --tpm2 --tpmstate dir="$TPMDIR" --ctrl type=unixio,path="$TPMDIR/swtpm.sock" \
  --terminate --daemon --log file="$TPMDIR/swtpm.log"

args=(
  -name win-arm
  -machine virt,gic-version=3,highmem=on,memory-backend=mem
  -accel kvm -cpu host,pmu=on -smp "$VCPUS"
  # memfd + share=on: required for Venus host-visible blobs, harmless otherwise.
  -object memory-backend-memfd,id=mem,size="${MEM}M",share=on
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.fd
  -drive if=pflash,format=raw,file="$VARS"
  -chardev socket,id=chrtpm,path="$TPMDIR/swtpm.sock"
  -tpmdev emulator,id=tpm0,chardev=chrtpm -device tpm-tis-device,tpmdev=tpm0
  # Windows on Arm has an inbox NVMe driver, so no driver is needed to install.
  -drive if=none,id=disk0,file="$DISK",format=qcow2,cache=none,aio=io_uring,discard=unmap
  -device nvme,drive=disk0,serial=winarm0,bootindex=1
  # USB keyboard for the firmware boot menu; Windows input goes through virtio-input below.
  -device qemu-xhci,id=xhci,p2=8,p3=8 -device usb-kbd
  -nic user,model=virtio-net-pci
  # Clipboard sharing with the GTK window via the SPICE agent protocol.
  -device virtio-serial-pci
  -chardev qemu-vdagent,id=vdagent,name=vdagent,clipboard=on
  -device virtserialport,chardev=vdagent,name=com.redhat.spice.0
  # QEMU guest agent (x64 qemu-ga runs under Windows' x64 emulation): ./ga.sh
  -chardev socket,id=qga0,path="$PWD/qga.sock",server=on,wait=off
  -device virtserialport,chardev=qga0,name=org.qemu.guest_agent.0
  -device virtio-balloon-pci
  -rtc base=localtime
  # Human monitor for scripting (sendkey, screendump, system_powerdown): socat - UNIX:monitor.sock
  -monitor unix:"$PWD/monitor.sock",server=on,wait=off
)

add_iso() { # $1 file, $2 id, $3 bootindex (optional)
  args+=(-drive if=none,id="$2",media=cdrom,readonly=on,file="$1"
         -device usb-storage,drive="$2",removable=on${3:+,bootindex=$3})
}

# ./share appears in Windows as a read-only USB drive labelled QEMU VVFAT (host -> guest file drop).
# vvfat snapshots the directory at start-up, so restart the VM to pick up new files.
add_share() {
  [[ -d share ]] && args+=(-drive if=none,id=share,format=raw,readonly=on,file=fat:"$PWD/share"
                          -device usb-storage,drive=share,removable=on)
}

# Host folders shared into Windows via virtio-fs (needs the memfd share=on backend above).
# SHARES="Tag=/path,Tag2=/path2" in vm.conf; each tag appears as a drive in Windows (VirtioFsSvc).
# One unprivileged virtiofsd per share; it exits when QEMU closes the vhost-user connection.
add_shares() {
  local spec tag dir sock rt=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
  IFS=',' read -ra specs <<<"${SHARES-Documents=$HOME/Documents,Downloads=$HOME/Downloads}"
  for spec in "${specs[@]}"; do
    tag=${spec%%=*}; dir=${spec#*=}; dir=${dir/#\~/$HOME}
    [[ $tag =~ ^[A-Za-z0-9_-]+$ && -d $dir ]] || { echo "skipping share '$spec'" >&2; continue; }
    sock=$rt/win-arm-fs-$tag.sock
    rm -f "$sock"
    setsid -f /usr/lib/virtiofsd --socket-path="$sock" --shared-dir="$dir" --sandbox=none \
      --cache=auto --announce-submounts --log-level=warn >>"$PWD/virtiofsd-$tag.log" 2>&1
    for _ in $(seq 50); do [[ -S $sock ]] && break; sleep 0.1; done
    args+=(-chardev socket,id="fs-$tag",path="$sock" -device vhost-user-fs-pci,chardev="fs-$tag",tag="$tag")
  done
}

# USB_AUTO="VID:PID,VID:PID" in vm.conf: passed through at start, and grabbed again whenever plugged in.
add_usb_auto() {
  local vp
  IFS=',' read -ra vps <<<"${USB_AUTO:-}"
  for vp in "${vps[@]}"; do
    [[ $vp =~ ^[0-9a-fA-F]{4}:[0-9a-fA-F]{4}$ ]] || continue
    args+=(-device usb-host,bus=xhci.0,vendorid=0x${vp%:*},productid=0x${vp#*:},id=usb-${vp/:/-})
  done
}

# virtio-input (vioinput driver): lower latency than the polled USB tablet.
input=(-device virtio-tablet-pci -device virtio-keyboard-pci)
if [[ $SCALE == auto ]]; then
  ms=$(hyprctl monitors -j 2>/dev/null | python3 -c 'import json,sys; m=[x for x in json.load(sys.stdin) if x["focused"]] or [{"scale":1}]; print(m[0]["scale"])' 2>/dev/null || echo 1)
  gtk_scale=$(python3 -c "print(round(1/float('$ms'), 4))")
else
  gtk_scale=$SCALE
fi

case $mode in
  install)
    [[ -n $WIN_ISO && -f $WIN_ISO ]] || { echo "Windows ARM64 ISO not found; set WIN_ISO=/path/to.iso" >&2; exit 1; }
    add_iso "$WIN_ISO" wincd 0
    [[ -n $VIRTIO_ISO ]] && add_iso "$VIRTIO_ISO" virtiocd
    # Windows Setup searches removable drives for autounattend.xml. vvfat exposes
    # the directory as a read-only FAT disk, so no image needs building.
    [[ -f unattend/autounattend.xml ]] && args+=(-drive if=none,id=unattend,format=raw,readonly=on,file=fat:"$PWD/unattend"
                                                 -device usb-storage,drive=unattend,removable=on)
    args+=(-device ramfb -device usb-tablet)
    display=${DISPLAY_OPTS:-gtk}
    ;;
  run)
    [[ -n $VIRTIO_ISO ]] && add_iso "$VIRTIO_ISO" virtiocd
    add_share
    add_shares
    add_usb_auto
    args+=("${input[@]}")
    args+=(-device virtio-gpu-pci,xres=1920,yres=1200)
    # gl=on: the host GPU scales/presents the guest image. The non-GL GTK path shears and is slow
    # on HiDPI (scale 2) outputs.
    display=${DISPLAY_OPTS:-gtk,gl=on,zoom-to-fit=off,show-menubar=off,scale=$gtk_scale}
    ;;
  run3d)
    [[ -n $VIRTIO_ISO ]] && add_iso "$VIRTIO_ISO" virtiocd
    add_share
    add_shares
    add_usb_auto
    args+=("${input[@]}")
    args+=(-device virtio-gpu-gl-pci,blob=on,venus=on,hostmem="$HOSTMEM",edid=off,xres=1920,yres=1200)
    display=${DISPLAY_OPTS:-gtk,gl=on,zoom-to-fit=off,show-menubar=off,scale=$gtk_scale}
    ;;
  *) echo "usage: $0 start|stop|kill|status|perf|memory|refresh|usb|set|info|install|run|run3d" >&2; exit 2 ;;
esac

[[ $FULLSCREEN == yes ]] && display+=",full-screen=on"
# vCPUs stay on the P-cores (see PCORES above); fewer vCPUs leave cores free for QEMU's own
# display/IO/GPU threads.
[[ $mode == run3d ]] && GTK_SCALE=$gtk_scale setsid -f "$0" _sync-refresh >/dev/null 2>&1
read -r ulo uhi < <(uclamp_for "$PERF")
perf_request "$PERF"
echo "vCPUs $VCPUS on CPUs $PCORES, ${MEM} MiB, mode $mode, profile $PERF (uclamp $ulo..$uhi), scale $gtk_scale" >&2
# QEMU's GTK UI pins the window's minimum size to the guest resolution (zoom-to-fit=off), so the
# guest could follow the window up (fullscreen) but never back down. host/gtk-free-resize drops
# that pin while keeping the device-pixel (HiDPI) sizing. Build it with host/gtk-free-resize/build.sh.
preload=$PWD/host/gtk-free-resize/gtk-free-resize.so
[[ $display == gtk* && -r $preload ]] && export LD_PRELOAD=$preload${LD_PRELOAD:+:$LD_PRELOAD}
exec uclampset -m "$ulo" -M "$uhi" taskset -c "$PCORES" qemu-system-aarch64 "${args[@]}" -display "$display" "${@:2}"
