#!/usr/bin/env python3
"""QEMU VM Manager - Graphical manager for QEMU virtual machines on Linux/Asahi."""
from __future__ import annotations

import json
import os
import signal
import subprocess
import sys
import threading
import time
import tkinter as tk
import tkinter.font as tkfont
from pathlib import Path
from tkinter import filedialog, messagebox, simpledialog, ttk
import urllib.error
import urllib.request


HOME = Path.home()
REGISTRY = HOME / ".config" / "qemu-vm-manager.json"
FALLBACK_REGISTRY = HOME / ".config" / "windows-vm-manager.json"
DEFAULT_VMS_DIR = HOME / "VMs"
DEFAULT_DOWNLOADS_DIR = HOME / "Downloads"

LIVE_MEMORY_PLACEHOLDER = "VM stopped"
LIVE_MEMORY_READING = "reading..."

PRESETS = {
    "Eco": {"VCPUS": "4", "MEM": "4096", "GPU": "2d", "HOSTMEM": "2G", "PERF": "eco", "SCALE": "auto", "FULLSCREEN": "no", "REFRESH": "60"},
    "Balanced": {"VCPUS": "6", "MEM": "8192", "GPU": "3d", "HOSTMEM": "4G", "PERF": "balanced", "SCALE": "auto", "FULLSCREEN": "no", "REFRESH": "60"},
    "Performance": {"VCPUS": "8", "MEM": "12288", "GPU": "3d", "HOSTMEM": "8G", "PERF": "performance", "SCALE": "auto", "FULLSCREEN": "no", "REFRESH": "60"},
}

FIELD_HELP = {
    "PRESET": "Loads a tested group of VM resource settings. Choose one, review the values, then apply them.",
    "VCPUS": "Virtual CPU count. QEMU is pinned to the host's performance cores; extra vCPUs would oversubscribe them.",
    "MEM": "RAM assigned when the VM boots in MiB. Live memory cannot be raised above this boot maximum.",
    "LIVE_MEM": "Current balloon memory limit in MiB. It is read from the running guest and can only be changed while the VM is running.",
    "GPU": "2d uses stable virtio-gpu. 3d enables accelerated host OpenGL/Vulkan (Virgl/Venus/Yttrium).",
    "HOSTMEM": "Host-visible GPU aperture used by 3D GPU mode.",
    "PERF": "QEMU CPU scheduling profile: eco, balanced, or performance.",
    "SCALE": "Guest display scale. Auto matches host monitor scale for crisp HiDPI rendering.",
    "FULLSCREEN": "Whether QEMU starts in fullscreen mode. Ctrl+Alt+F toggles fullscreen after launch.",
    "REFRESH": "Guest display refresh rate in Hz (60 or 120).",
    "ISO": "Attached boot/installation ISO image path.",
}

POPULAR_ISOS = [
    {
        "id": "ubuntu-desktop-arm64",
        "name": "Ubuntu 24.04 LTS Desktop",
        "arch": "arm64",
        "size": "3.9 GB",
        "category": "Ubuntu",
        "desc": "Official Ubuntu 24.04 Desktop with GNOME. Native KVM hardware acceleration on Apple Silicon.",
        "filename": "ubuntu-24.04.5-desktop-arm64.iso",
        "url": "https://cdimage.ubuntu.com/releases/noble/release/ubuntu-24.04.5-desktop-arm64.iso",
    },
    {
        "id": "ubuntu-server-arm64",
        "name": "Ubuntu 24.04 LTS Server",
        "arch": "arm64",
        "size": "3.8 GB",
        "category": "Ubuntu",
        "desc": "Official Ubuntu 24.04 Server. Fast, lightweight base; optional GUI. Native KVM acceleration.",
        "filename": "ubuntu-24.04.5-live-server-arm64.iso",
        "url": "https://cdimage.ubuntu.com/releases/noble/release/ubuntu-24.04.5-live-server-arm64.iso",
    },
    {
        "id": "ubuntu-desktop-x86_64",
        "name": "Ubuntu 24.04 LTS Desktop",
        "arch": "x86_64",
        "size": "6.2 GB",
        "category": "Ubuntu",
        "desc": "Official Ubuntu 24.04 Desktop for x86_64 (runs via QEMU x86_64 TCG emulation).",
        "filename": "ubuntu-24.04.5.1-desktop-amd64.iso",
        "url": "https://releases.ubuntu.com/noble/ubuntu-24.04.5.1-desktop-amd64.iso",
    },
    {
        "id": "ubuntu-server-x86_64",
        "name": "Ubuntu 24.04 LTS Server",
        "arch": "x86_64",
        "size": "4.0 GB",
        "category": "Ubuntu",
        "desc": "Official Ubuntu 24.04 Server for x86_64 (runs via QEMU x86_64 TCG emulation).",
        "filename": "ubuntu-24.04.5-live-server-amd64.iso",
        "url": "https://releases.ubuntu.com/noble/ubuntu-24.04.5-live-server-amd64.iso",
    },
    {
        "id": "debian-netinst-arm64",
        "name": "Debian 13 (Trixie) Netinst",
        "arch": "arm64",
        "size": "800 MB",
        "category": "Debian",
        "desc": "Minimal network installer for Debian on ARM64. Fast install, lightweight base.",
        "filename": "debian-13.7.0-arm64-netinst.iso",
        "url": "https://cdimage.debian.org/debian-cd/current/arm64/iso-cd/debian-13.7.0-arm64-netinst.iso",
    },
    {
        "id": "debian-netinst-x86_64",
        "name": "Debian 13 (Trixie) Netinst",
        "arch": "x86_64",
        "size": "800 MB",
        "category": "Debian",
        "desc": "Minimal network installer for Debian on x86_64.",
        "filename": "debian-13.7.0-amd64-netinst.iso",
        "url": "https://cdimage.debian.org/debian-cd/current/amd64/iso-cd/debian-13.7.0-amd64-netinst.iso",
    },
    {
        "id": "fedora-server-arm64",
        "name": "Fedora 41 Server Netinst",
        "arch": "arm64",
        "size": "977 MB",
        "category": "Fedora",
        "desc": "Fedora 41 network installer for ARM64 (can install Server, Workstation, or custom desktop).",
        "filename": "Fedora-Server-netinst-aarch64-41-1.4.iso",
        "url": "https://download.fedoraproject.org/pub/fedora/linux/releases/41/Server/aarch64/iso/Fedora-Server-netinst-aarch64-41-1.4.iso",
    },
    {
        "id": "fedora-server-x86_64",
        "name": "Fedora 41 Server Netinst",
        "arch": "x86_64",
        "size": "955 MB",
        "category": "Fedora",
        "desc": "Fedora 41 network installer for x86_64.",
        "filename": "Fedora-Server-netinst-x86_64-41-1.4.iso",
        "url": "https://download.fedoraproject.org/pub/fedora/linux/releases/41/Server/x86_64/iso/Fedora-Server-netinst-x86_64-41-1.4.iso",
    },
    {
        "id": "fedora-workstation-x86_64",
        "name": "Fedora 41 Workstation Live",
        "arch": "x86_64",
        "size": "2.4 GB",
        "category": "Fedora",
        "desc": "Fedora 41 Workstation Live desktop image for x86_64.",
        "filename": "Fedora-Workstation-Live-x86_64-41-1.4.iso",
        "url": "https://download.fedoraproject.org/pub/fedora/linux/releases/41/Workstation/x86_64/iso/Fedora-Workstation-Live-x86_64-41-1.4.iso",
    },
    {
        "id": "archlinux-x86_64",
        "name": "Arch Linux Install Medium",
        "arch": "x86_64",
        "size": "1.6 GB",
        "category": "Arch",
        "desc": "Official Arch Linux live installation medium for x86_64.",
        "filename": "archlinux-x86_64.iso",
        "url": "https://geo.mirror.pkgbuild.com/iso/latest/archlinux-x86_64.iso",
    },
    {
        "id": "alpine-arm64",
        "name": "Alpine Linux 3.21 Standard",
        "arch": "arm64",
        "size": "268 MB",
        "category": "Alpine",
        "desc": "Ultralight, security-oriented Linux distribution for ARM64.",
        "filename": "alpine-standard-3.21.3-aarch64.iso",
        "url": "https://dl-cdn.alpinelinux.org/alpine/v3.21/releases/aarch64/alpine-standard-3.21.3-aarch64.iso",
    },
    {
        "id": "alpine-x86_64",
        "name": "Alpine Linux 3.21 Standard",
        "arch": "x86_64",
        "size": "257 MB",
        "category": "Alpine",
        "desc": "Ultralight, security-oriented Linux distribution for x86_64.",
        "filename": "alpine-standard-3.21.3-x86_64.iso",
        "url": "https://dl-cdn.alpinelinux.org/alpine/v3.21/releases/x86_64/alpine-standard-3.21.3-x86_64.iso",
    },
    {
        "id": "kali-installer-x86_64",
        "name": "Kali Linux 2026.2 Installer",
        "arch": "x86_64",
        "size": "4.5 GB",
        "category": "Kali",
        "desc": "Official Kali Linux complete offline installer for x86_64 penetration testing.",
        "filename": "kali-linux-2026.2-installer-amd64.iso",
        "url": "https://kali.download/base-images/kali-2026.2/kali-linux-2026.2-installer-amd64.iso",
    },
    {
        "id": "kali-installer-arm64",
        "name": "Kali Linux 2026.2 Installer",
        "arch": "arm64",
        "size": "3.8 GB",
        "category": "Kali",
        "desc": "Official Kali Linux installer for ARM64 with native Apple Silicon KVM acceleration.",
        "filename": "kali-linux-2026.2-installer-arm64.iso",
        "url": "https://kali.download/base-images/kali-2026.2/kali-linux-2026.2-installer-arm64.iso",
    },
    {
        "id": "virtio-win",
        "name": "VirtIO Windows Drivers",
        "arch": "arm64 & x86_64",
        "size": "880 MB",
        "category": "Windows",
        "desc": "Official Red Hat VirtIO drivers ISO for Windows guests (storage, network, balloon, GPU).",
        "filename": "virtio-win.iso",
        "url": "https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/stable-virtio/virtio-win.iso",
    },
]


def host_pcore_count():
    cpus = set()
    for policy in Path("/sys/devices/system/cpu/cpufreq").glob("policy*"):
        try:
            if int((policy / "cpuinfo_max_freq").read_text()) >= 3_000_000:
                cpus.update((policy / "related_cpus").read_text().split())
        except (OSError, ValueError):
            continue
    return len(cpus) or 8


PCORE_COUNT = host_pcore_count()
VCPU_VALUES = tuple(str(v) for v in (2, 4, 6, 8, 10, 12) if v <= PCORE_COUNT) or ("2", "4", "6", "8")


def read_registry():
    for reg_path in (REGISTRY, FALLBACK_REGISTRY):
        try:
            data = json.loads(reg_path.read_text())
            profiles = data.get("profiles", [])
            if profiles:
                return profiles
        except (OSError, ValueError):
            pass

    profiles = []
    ubuntu_dir = DEFAULT_VMS_DIR / "ubuntu-arm"
    windows_dir = DEFAULT_VMS_DIR / "windows-arm"

    if ubuntu_dir.exists():
        profiles.append({"name": "Ubuntu ARM", "dir": str(ubuntu_dir), "os": "ubuntu", "arch": "arm64"})
    if windows_dir.exists():
        profiles.append({"name": "Windows ARM", "dir": str(windows_dir), "os": "windows", "arch": "arm64"})

    if not profiles:
        profiles.append({"name": "Ubuntu ARM", "dir": str(ubuntu_dir), "os": "ubuntu", "arch": "arm64"})

    return profiles


def write_registry(profiles):
    for reg_path in (REGISTRY, FALLBACK_REGISTRY):
        try:
            reg_path.parent.mkdir(parents=True, exist_ok=True)
            reg_path.write_text(json.dumps({"profiles": profiles}, indent=2) + "\n")
        except OSError:
            pass


def parse_conf(path: Path | None):
    values = {}
    if not path or not path.exists():
        return values
    try:
        for line in path.read_text().splitlines():
            line = line.strip()
            if "=" in line and not line.startswith("#"):
                key, value = line.split("=", 1)
                values[key.strip()] = value.strip()
    except OSError:
        pass
    return values


def parse_info(text: str):
    info = {}
    for line in text.splitlines():
        for token in line.split():
            if "=" in token:
                k, v = token.split("=", 1)
                info[k.strip()] = v.strip()
    return info


class Tooltip:
    def __init__(self, widget, text):
        self.widget = widget
        self.text = text
        self.tip = None
        self.timer = None
        widget.bind("<Enter>", self.schedule)
        widget.bind("<Leave>", self.hide)

    def schedule(self, _event=None):
        self.timer = self.widget.after(450, self.show)

    def show(self):
        self.timer = None
        if self.tip or not self.widget.winfo_exists():
            return
        self.tip = tk.Toplevel(self.widget)
        self.tip.wm_overrideredirect(True)
        self.tip.attributes("-topmost", True)
        x = self.widget.winfo_rootx() + self.widget.winfo_width() + 6
        y = self.widget.winfo_rooty() + self.widget.winfo_height() + 4
        self.tip.geometry(f"+{x}+{y}")
        tk.Label(
            self.tip,
            text=self.text,
            justify="left",
            wraplength=340,
            padx=9,
            pady=7,
            background="#fff8d8",
            foreground="#222",
            relief="solid",
            borderwidth=1,
        ).pack()

    def hide(self, _event=None):
        if self.timer:
            self.widget.after_cancel(self.timer)
            self.timer = None
        if self.tip:
            self.tip.destroy()
            self.tip = None


class IsoDownloaderDialog(tk.Toplevel):
    """Download popular OS ISO images with real-time progress and cancellation."""

    def __init__(self, parent: Manager):
        super().__init__(parent)
        self.parent_mgr = parent
        self.title("Download Operating System ISOs")
        self.geometry("860x620")
        self.minsize(740, 500)
        self.transient(parent)

        self.arch_filter = tk.StringVar(value="all")
        self.search_filter = tk.StringVar(value="")
        self.dest_dir = tk.StringVar(value=str(DEFAULT_DOWNLOADS_DIR))
        self.custom_url = tk.StringVar(value="")
        self.custom_name = tk.StringVar(value="")

        self.download_active = False
        self.cancel_requested = False
        self.download_thread: threading.Thread | None = None

        self.progress_var = tk.DoubleVar(value=0.0)
        self.progress_text = tk.StringVar(value="Select an ISO to download")

        self.build_ui()
        self.refresh_iso_list()

    def build_ui(self):
        pad = 10
        outer = ttk.Frame(self, padding=pad)
        outer.pack(fill="both", expand=True)

        top = ttk.LabelFrame(outer, text="ISO Filter & Destination", padding=8)
        top.pack(fill="x", pady=(0, 8))

        row1 = ttk.Frame(top)
        row1.pack(fill="x", pady=2)
        ttk.Label(row1, text="Architecture:").pack(side="left")
        arch_box = ttk.Combobox(
            row1,
            textvariable=self.arch_filter,
            values=("all", "arm64", "x86_64"),
            state="readonly",
            width=12,
        )
        arch_box.pack(side="left", padx=(6, 16))
        arch_box.bind("<<ComboboxSelected>>", lambda _: self.refresh_iso_list())

        ttk.Label(row1, text="Search:").pack(side="left")
        search_ent = ttk.Entry(row1, textvariable=self.search_filter, width=22)
        search_ent.pack(side="left", padx=(6, 8))
        search_ent.bind("<KeyRelease>", lambda _: self.refresh_iso_list())

        ttk.Button(row1, text="Clear", command=self.clear_search).pack(side="left")

        row2 = ttk.Frame(top)
        row2.pack(fill="x", pady=(6, 2))
        ttk.Label(row2, text="Save to:").pack(side="left")
        ttk.Entry(row2, textvariable=self.dest_dir).pack(side="left", fill="x", expand=True, padx=6)
        ttk.Button(row2, text="Browse...", command=self.browse_dest).pack(side="right")

        mid = ttk.Frame(outer)
        mid.pack(fill="both", expand=True)

        cols = ("name", "arch", "category", "size", "status")
        self.tree = ttk.Treeview(mid, columns=cols, show="headings", selectmode="browse")
        self.tree.heading("name", text="Distribution / OS")
        self.tree.heading("arch", text="Arch")
        self.tree.heading("category", text="Category")
        self.tree.heading("size", text="Size")
        self.tree.heading("status", text="Status")

        self.tree.column("name", width=260, anchor="w")
        self.tree.column("arch", width=80, anchor="center")
        self.tree.column("category", width=90, anchor="center")
        self.tree.column("size", width=80, anchor="center")
        self.tree.column("status", width=220, anchor="w")

        scrollbar = ttk.Scrollbar(mid, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=scrollbar.set)
        self.tree.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        self.tree.bind("<<TreeviewSelect>>", self.on_select_iso)

        self.desc_label = ttk.Label(
            outer,
            text="",
            wraplength=800,
            justify="left",
            foreground="#333",
            padding=(4, 6),
        )
        self.desc_label.pack(fill="x")

        cust = ttk.LabelFrame(outer, text="Download Custom ISO URL", padding=6)
        cust.pack(fill="x", pady=(4, 8))
        c_row = ttk.Frame(cust)
        c_row.pack(fill="x")
        ttk.Label(c_row, text="URL:").pack(side="left")
        ttk.Entry(c_row, textvariable=self.custom_url).pack(side="left", fill="x", expand=True, padx=6)
        ttk.Label(c_row, text="Filename:").pack(side="left")
        ttk.Entry(c_row, textvariable=self.custom_name, width=20).pack(side="left", padx=6)
        ttk.Button(c_row, text="Download URL", command=self.start_custom_download).pack(side="left")

        bot = ttk.Frame(outer)
        bot.pack(fill="x", pady=(4, 0))

        self.pbar = ttk.Progressbar(bot, variable=self.progress_var, maximum=100.0)
        self.pbar.pack(fill="x", pady=(0, 4))

        status_frame = ttk.Frame(bot)
        status_frame.pack(fill="x")
        ttk.Label(status_frame, textvariable=self.progress_text, font="TkDefaultFont 9").pack(side="left")

        btn_box = ttk.Frame(status_frame)
        btn_box.pack(side="right")

        self.dl_btn = ttk.Button(btn_box, text="Download Selected ISO", command=self.start_selected_download)
        self.dl_btn.pack(side="left", padx=3)

        self.cancel_btn = ttk.Button(btn_box, text="Cancel", command=self.cancel_download, state="disabled")
        self.cancel_btn.pack(side="left", padx=3)

        self.use_btn = ttk.Button(
            btn_box,
            text="Create VM with this ISO",
            command=self.create_vm_from_selected,
            state="disabled",
        )
        self.use_btn.pack(side="left", padx=3)

        ttk.Button(btn_box, text="Close", command=self.destroy).pack(side="left", padx=3)

    def browse_dest(self):
        d = filedialog.askdirectory(initialdir=self.dest_dir.get(), parent=self)
        if d:
            self.dest_dir.set(d)
            self.refresh_iso_list()

    def clear_search(self):
        self.search_filter.set("")
        self.refresh_iso_list()

    def get_dest_path(self, filename: str) -> Path:
        return Path(self.dest_dir.get()).expanduser() / filename

    def refresh_iso_list(self):
        self.tree.delete(*self.tree.get_children())
        arch = self.arch_filter.get().lower()
        search = self.search_filter.get().lower().strip()

        for item in POPULAR_ISOS:
            if arch != "all":
                if arch not in item["arch"].lower() and "universal" not in item["arch"].lower():
                    continue
            if search:
                haystack = f"{item['name']} {item['category']} {item['desc']} {item['filename']}".lower()
                if search not in haystack:
                    continue

            target = self.get_dest_path(item["filename"])
            if target.exists():
                size_mb = target.stat().st_size / (1024 * 1024)
                status = f"✓ Downloaded ({size_mb:.0f} MB)"
            else:
                status = "Available"

            self.tree.insert(
                "",
                "end",
                iid=item["id"],
                values=(item["name"], item["arch"], item["category"], item["size"], status),
            )

    def on_select_iso(self, _event=None):
        sel = self.tree.selection()
        if not sel:
            self.desc_label.config(text="")
            self.use_btn.config(state="disabled")
            return
        iso_id = sel[0]
        item = next((x for x in POPULAR_ISOS if x["id"] == iso_id), None)
        if not item:
            return

        target = self.get_dest_path(item["filename"])
        is_dl = target.exists()
        self.desc_label.config(
            text=f"• {item['name']} ({item['arch']}): {item['desc']}\n• File: {item['filename']} | Direct URL: {item['url']}"
        )
        self.use_btn.config(state="normal" if is_dl else "disabled")

    def start_selected_download(self):
        sel = self.tree.selection()
        if not sel:
            messagebox.showinfo("Download", "Please select an ISO from the list.", parent=self)
            return
        iso_id = sel[0]
        item = next((x for x in POPULAR_ISOS if x["id"] == iso_id), None)
        if not item:
            return

        target = self.get_dest_path(item["filename"])
        if target.exists():
            if not messagebox.askyesno(
                "Overwrite",
                f"{item['filename']} already exists. Re-download and overwrite?",
                parent=self,
            ):
                return

        self.run_download(item["url"], target, item["name"])

    def start_custom_download(self):
        url = self.custom_url.get().strip()
        name = self.custom_name.get().strip()
        if not url:
            messagebox.showerror("Custom URL", "Please enter a valid HTTP/HTTPS URL.", parent=self)
            return
        if not name:
            name = url.split("?")[0].rstrip("/").split("/")[-1] or "custom.iso"
            if not name.endswith(".iso"):
                name += ".iso"

        target = self.get_dest_path(name)
        self.run_download(url, target, name)

    def run_download(self, url: str, target: Path, display_name: str):
        if self.download_active:
            messagebox.showwarning(
                "Download in progress",
                "A download is already running. Please wait or cancel it first.",
                parent=self,
            )
            return

        target.parent.mkdir(parents=True, exist_ok=True)
        self.download_active = True
        self.cancel_requested = False
        self.dl_btn.config(state="disabled")
        self.cancel_btn.config(state="normal")
        self.progress_var.set(0.0)
        self.progress_text.set(f"Connecting to {display_name}...")

        def worker():
            part_file = target.with_suffix(target.suffix + ".part")
            try:
                req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (QEMU-VM-Manager)"})
                with urllib.request.urlopen(req, timeout=30) as resp, open(part_file, "wb") as out_f:
                    total_bytes = int(resp.headers.get("Content-Length", 0))
                    downloaded = 0
                    start_time = time.time()
                    last_update = 0.0

                    while True:
                        if self.cancel_requested:
                            break
                        chunk = resp.read(256 * 1024)
                        if not chunk:
                            break
                        out_f.write(chunk)
                        downloaded += len(chunk)

                        now = time.time()
                        if now - last_update >= 0.35:
                            last_update = now
                            elapsed = max(0.1, now - start_time)
                            speed_mb = (downloaded / (1024 * 1024)) / elapsed
                            pct = (downloaded / total_bytes * 100) if total_bytes > 0 else 0.0
                            mb_done = downloaded / (1024 * 1024)
                            mb_total = total_bytes / (1024 * 1024) if total_bytes > 0 else 0
                            eta_sec = int((total_bytes - downloaded) / (speed_mb * 1024 * 1024)) if total_bytes and speed_mb > 0 else 0

                            text = f"Downloading {display_name}: {pct:.1f}% ({mb_done:.1f} / {mb_total:.1f} MB) · {speed_mb:.2f} MB/s · ETA {eta_sec // 60}m {eta_sec % 60}s"
                            self.after(0, lambda p=pct, t=text: self.update_progress(p, t))

                if self.cancel_requested:
                    if part_file.exists():
                        part_file.unlink()
                    self.after(0, lambda: self.on_download_cancelled(display_name))
                else:
                    if part_file.exists():
                        part_file.replace(target)
                    self.after(0, lambda: self.on_download_complete(display_name, target))

            except Exception as exc:
                if part_file.exists():
                    try:
                        part_file.unlink()
                    except OSError:
                        pass
                self.after(0, lambda e=str(exc): self.on_download_error(e))

        self.download_thread = threading.Thread(target=worker, daemon=True)
        self.download_thread.start()

    def update_progress(self, pct: float, text: str):
        self.progress_var.set(pct)
        self.progress_text.set(text)

    def cancel_download(self):
        if self.download_active:
            self.cancel_requested = True
            self.progress_text.set("Cancelling download...")

    def on_download_cancelled(self, name: str):
        self.download_active = False
        self.dl_btn.config(state="normal")
        self.cancel_btn.config(state="disabled")
        self.progress_var.set(0.0)
        self.progress_text.set(f"Download of {name} was cancelled.")
        self.refresh_iso_list()

    def on_download_error(self, err: str):
        self.download_active = False
        self.dl_btn.config(state="normal")
        self.cancel_btn.config(state="disabled")
        self.progress_text.set(f"Download failed: {err}")
        messagebox.showerror("Download Error", f"Failed to download ISO:\n{err}", parent=self)
        self.refresh_iso_list()

    def on_download_complete(self, name: str, target: Path):
        self.download_active = False
        self.dl_btn.config(state="normal")
        self.cancel_btn.config(state="disabled")
        self.progress_var.set(100.0)
        self.progress_text.set(f"✓ {name} downloaded successfully to {target.name}!")
        self.refresh_iso_list()
        self.use_btn.config(state="normal")

        if messagebox.askyesno(
            "Download Finished",
            f"Successfully downloaded {name}!\n\nWould you like to create a new VM with this ISO now?",
            parent=self,
        ):
            self.parent_mgr.open_new_vm_wizard(initial_iso=str(target))
            self.destroy()

    def create_vm_from_selected(self):
        sel = self.tree.selection()
        if not sel:
            return
        iso_id = sel[0]
        item = next((x for x in POPULAR_ISOS if x["id"] == iso_id), None)
        if not item:
            return
        target = self.get_dest_path(item["filename"])
        if target.exists():
            self.parent_mgr.open_new_vm_wizard(initial_iso=str(target), initial_name=item["name"])
            self.destroy()


class NewVmDialog(tk.Toplevel):
    """Wizard to create and configure a new QEMU virtual machine."""

    def __init__(self, parent: Manager, initial_iso: str = "", initial_name: str = ""):
        super().__init__(parent)
        self.parent_mgr = parent
        self.title("New QEMU Virtual Machine")
        self.geometry("640x520")
        self.minsize(560, 440)
        self.transient(parent)

        default_name = initial_name or "Ubuntu ARM"
        self.name_var = tk.StringVar(value=default_name)
        self.os_type_var = tk.StringVar(value="Ubuntu ARM" if "ubuntu" in default_name.lower() else "Generic Linux ARM")
        self.arch_var = tk.StringVar(value="arm64")
        self.dir_var = tk.StringVar(value=str(DEFAULT_VMS_DIR / self.slugify(default_name)))
        self.iso_var = tk.StringVar(value=initial_iso)
        self.vcpus_var = tk.StringVar(value="6")
        self.mem_var = tk.StringVar(value="8192")
        self.disk_size_var = tk.StringVar(value="64")
        self.gpu_var = tk.StringVar(value="3d")

        self.build_ui()

    @staticmethod
    def slugify(text: str) -> str:
        s = "".join(c.lower() if c.isalnum() else "-" for c in text.strip())
        return "-".join(part for part in s.split("-") if part) or "vm"

    def build_ui(self):
        outer = ttk.Frame(self, padding=14)
        outer.pack(fill="both", expand=True)

        ttk.Label(outer, text="Create a New Virtual Machine", font="TkHeadingFont 13 bold").pack(anchor="w", pady=(0, 10))

        grid = ttk.LabelFrame(outer, text="VM Configuration", padding=10)
        grid.pack(fill="both", expand=True)

        rows = [
            ("VM Name:", self.name_var, "entry"),
            ("OS Template:", self.os_type_var, "os_combo"),
            ("Architecture:", self.arch_var, "arch_combo"),
            ("VM Directory:", self.dir_var, "dir_entry"),
            ("Installation ISO:", self.iso_var, "iso_entry"),
            ("vCPUs:", self.vcpus_var, "vcpu_combo"),
            ("RAM (MiB):", self.mem_var, "mem_combo"),
            ("Virtual Disk (GiB):", self.disk_size_var, "disk_entry"),
            ("GPU Mode:", self.gpu_var, "gpu_combo"),
        ]

        for i, (label, var, widget_type) in enumerate(rows):
            ttk.Label(grid, text=label).grid(row=i, column=0, sticky="w", pady=4, padx=4)

            if widget_type == "entry":
                w = ttk.Entry(grid, textvariable=var)
                w.grid(row=i, column=1, columnspan=2, sticky="ew", pady=4, padx=4)
                w.bind("<KeyRelease>", self.on_name_change)
            elif widget_type == "os_combo":
                w = ttk.Combobox(
                    grid,
                    textvariable=var,
                    values=("Ubuntu ARM", "Debian ARM", "Fedora ARM", "Alpine ARM", "Windows ARM", "Generic Linux ARM", "Generic x86_64"),
                    state="readonly",
                )
                w.grid(row=i, column=1, columnspan=2, sticky="ew", pady=4, padx=4)
                w.bind("<<ComboboxSelected>>", self.on_template_change)
            elif widget_type == "arch_combo":
                w = ttk.Combobox(grid, textvariable=var, values=("arm64", "x86_64"), state="readonly")
                w.grid(row=i, column=1, columnspan=2, sticky="ew", pady=4, padx=4)
            elif widget_type == "dir_entry":
                f = ttk.Frame(grid)
                f.grid(row=i, column=1, columnspan=2, sticky="ew", pady=4, padx=4)
                ttk.Entry(f, textvariable=var).pack(side="left", fill="x", expand=True)
                ttk.Button(f, text="Browse...", command=self.browse_dir).pack(side="right", padx=(4, 0))
            elif widget_type == "iso_entry":
                f = ttk.Frame(grid)
                f.grid(row=i, column=1, columnspan=2, sticky="ew", pady=4, padx=4)
                ttk.Entry(f, textvariable=var).pack(side="left", fill="x", expand=True)
                ttk.Button(f, text="Browse...", command=self.browse_iso).pack(side="right", padx=(4, 0))
            elif widget_type == "vcpu_combo":
                w = ttk.Combobox(grid, textvariable=var, values=VCPU_VALUES)
                w.grid(row=i, column=1, columnspan=2, sticky="ew", pady=4, padx=4)
            elif widget_type == "mem_combo":
                w = ttk.Combobox(grid, textvariable=var, values=("2048", "4096", "6144", "8192", "12288", "16384", "20480"))
                w.grid(row=i, column=1, columnspan=2, sticky="ew", pady=4, padx=4)
            elif widget_type == "disk_entry":
                w = ttk.Entry(grid, textvariable=var)
                w.grid(row=i, column=1, columnspan=2, sticky="ew", pady=4, padx=4)
            elif widget_type == "gpu_combo":
                w = ttk.Combobox(grid, textvariable=var, values=("3d", "2d"), state="readonly")
                w.grid(row=i, column=1, columnspan=2, sticky="ew", pady=4, padx=4)

        grid.columnconfigure(1, weight=1)

        bot = ttk.Frame(outer)
        bot.pack(fill="x", pady=(12, 0))
        ttk.Button(bot, text="Create VM", command=self.create_vm).pack(side="right", padx=4)
        ttk.Button(bot, text="Cancel", command=self.destroy).pack(side="right", padx=4)

    def on_name_change(self, _event=None):
        name = self.name_var.get().strip()
        if name:
            self.dir_var.set(str(DEFAULT_VMS_DIR / self.slugify(name)))

    def on_template_change(self, _event=None):
        tpl = self.os_type_var.get()
        if "x86" in tpl:
            self.arch_var.set("x86_64")
        else:
            self.arch_var.set("arm64")

    def browse_dir(self):
        d = filedialog.askdirectory(initialdir=str(DEFAULT_VMS_DIR), parent=self)
        if d:
            self.dir_var.set(d)

    def browse_iso(self):
        f = filedialog.askopenfilename(
            initialdir=str(DEFAULT_DOWNLOADS_DIR),
            filetypes=[("ISO Images", "*.iso"), ("All Files", "*.*")],
            parent=self,
        )
        if f:
            self.iso_var.set(f)

    def create_vm(self):
        name = self.name_var.get().strip()
        vm_dir = Path(self.dir_var.get().strip()).expanduser()
        iso = self.iso_var.get().strip()
        disk_size = self.disk_size_var.get().strip()
        vcpus = self.vcpus_var.get().strip()
        mem = self.mem_var.get().strip()
        gpu = self.gpu_var.get().strip()
        arch = self.arch_var.get().strip()

        if not name:
            messagebox.showerror("Create VM", "Please specify a VM name.", parent=self)
            return
        if not disk_size.isdigit():
            messagebox.showerror("Create VM", "Disk size must be a number in GiB.", parent=self)
            return

        try:
            vm_dir.mkdir(parents=True, exist_ok=True)
            disk_path = vm_dir / "disk.qcow2"
            if not disk_path.exists():
                subprocess.run(
                    ["qemu-img", "create", "-f", "qcow2", "-o", "cluster_size=64k", str(disk_path), f"{disk_size}G"],
                    check=True,
                    capture_output=True,
                    text=True,
                )

            efivars = vm_dir / "efivars.fd"
            if arch == "arm64" and not efivars.exists():
                aavmf_src = Path("/usr/share/AAVMF/AAVMF_VARS.fd")
                if aavmf_src.exists():
                    subprocess.run(["cp", str(aavmf_src), str(efivars)], check=True)

            conf_lines = [
                f"# VM Configuration for {name}",
                f"VCPUS={vcpus}",
                f"MEM={mem}",
                f"GPU={gpu}",
                "HOSTMEM=4G",
                "FULLSCREEN=no",
                "PERF=performance",
                "SCALE=auto",
                "REFRESH=60",
                f"ISO={iso}",
                f"SHARES=Documents={HOME}/Documents,Downloads={HOME}/Downloads",
            ]
            (vm_dir / "vm.conf").write_text("\n".join(conf_lines) + "\n")

            slug = self.slugify(name)
            script_path = vm_dir / "vm.sh"
            script_code = generate_runner_script(name, slug, arch)
            script_path.write_text(script_code)
            script_path.chmod(0o755)

            self.parent_mgr.profiles.append({"name": name, "dir": str(vm_dir), "arch": arch})
            write_registry(self.parent_mgr.profiles)
            self.parent_mgr.refresh_profile_list()
            self.parent_mgr.selected.set(len(self.parent_mgr.profiles) - 1)
            self.parent_mgr.profile_box.current(self.parent_mgr.selected.get())
            self.parent_mgr.load_config()

            if messagebox.askyesno(
                "VM Created",
                f"VM '{name}' was created successfully!\n\nWould you like to launch the installer now?",
                parent=self,
            ):
                self.parent_mgr.install_vm()

            self.destroy()

        except Exception as exc:
            messagebox.showerror("Create VM Failed", str(exc), parent=self)


def generate_runner_script(name: str, slug: str, arch: str) -> str:
    """Generate a high-performance QEMU runner script for ARM64 or x86_64."""
    if arch == "arm64":
        return f"""#!/bin/bash
# {name} VM runner script on Apple Silicon QEMU/KVM
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

mode=${{1:-start}}

conf=()
[[ -r vm.conf ]] && mapfile -t conf < <(grep -E '^[A-Za-z0-9_]+=' vm.conf)
for kv in "${{conf[@]}}"; do name=${{kv%%=*}}; [[ -n ${{!name:-}} ]] || declare "$kv"; done

VCPUS=${{VCPUS:-6}}
MEM=${{MEM:-8192}}
GPU=${{GPU:-3d}}
HOSTMEM=${{HOSTMEM:-4G}}
FULLSCREEN=${{FULLSCREEN:-no}}
PERF=${{PERF:-performance}}
SCALE=${{SCALE:-auto}}
REFRESH=${{REFRESH:-60}}
ISO=${{ISO:-}}

PCORES=${{PCORES:-$(for p in /sys/devices/system/cpu/cpufreq/policy*; do
  [[ $(<"$p/cpuinfo_max_freq") -ge 3000000 ]] && cat "$p/related_cpus"; done | tr ' \\n' ',,' | sed 's/,*$//')}}
[[ -n $PCORES ]] || PCORES=2-9

VM_NAME="{slug}"

qemu_pid() {{
  local p
  while read -r p; do
    [[ -n $p && $(readlink "/proc/$p/cwd" 2>/dev/null) == "$PWD" ]] && {{ echo "$p"; return; }}
  done < <(pgrep -f "qemu-system-aarch64.*-name $VM_NAME( |$)" || true)
}}

running() {{ [[ -n $(qemu_pid) ]]; }}
monitor() {{ echo "$*" | socat - UNIX-CONNECT:monitor.sock >/dev/null 2>&1 || true; }}
monitor_out() {{ {{ echo "$*"; sleep 0.2; }} | socat - UNIX-CONNECT:monitor.sock 2>/dev/null | tr -d '\\r' | grep -vE '^(QEMU |\\(qemu\\))' || true; }}

focus() {{
  local address
  address=$(hyprctl clients -j 2>/dev/null | python3 -c 'import json,sys
try:
    for w in json.load(sys.stdin):
        if w.get("class") == "qemu" and "{slug}" in w.get("title", "").lower():
            print(w.get("address", ""))
            break
except Exception: pass' 2>/dev/null || true)
  [[ $address =~ ^0x[0-9a-fA-F]+$ ]] || return 0
  hyprctl dispatch "hl.dsp.focus({{ window = \\"address:$address\\" }})" >/dev/null 2>&1 || true
}}

set_conf() {{
  if grep -q "^$1=" vm.conf 2>/dev/null; then
    sed -i "s|^$1=.*|$1=$2|" vm.conf
  else
    echo "$1=$2" >>vm.conf
  fi
}}

uclamp_for() {{ case $1 in eco) echo "0 512" ;; performance) echo "1024 1024" ;; *) echo "512 1024" ;; esac; }}
perf_request() {{ echo "$1" > "${{XDG_RUNTIME_DIR:-/run/user/$(id -u)}}/{slug}.perf" 2>/dev/null || true; }}

case $mode in
  status) running && echo running || echo stopped; exit 0 ;;
  stop)   running && monitor system_powerdown; exit 0 ;;
  kill)   running && monitor quit; exit 0 ;;
  set)    set_conf "$2" "${{3:?value}}"; exit 0 ;;
  perf)   set_conf PERF "$2"; exit 0 ;;
  memory) running && monitor "balloon $2"; exit 0 ;;
  info)
    echo "state=$(running && echo running || echo stopped)"
    echo "perf=$PERF vcpus=$VCPUS mem=$MEM gpu=$GPU hostmem=$HOSTMEM fullscreen=$FULLSCREEN scale=$SCALE refresh=$REFRESH"
    if running; then
      echo "balloon=$(monitor_out info balloon | sed -n 's/.*actual=\\([0-9]*\\).*/\\1/p')"
    fi
    exit 0 ;;
  start|install)
    if running; then focus; exit 0; fi
    ;;
  *) echo "usage: $0 start|install|stop|kill|status|perf|memory|set|info" >&2; exit 2 ;;
esac

DISK=disk.qcow2
VARS=efivars.fd
[[ -f $DISK ]] || qemu-img create -f qcow2 -o cluster_size=64k "$DISK" 64G
[[ -f $VARS ]] || cp /usr/share/AAVMF/AAVMF_VARS.fd "$VARS" 2>/dev/null || true

gtk_scale=1
if [[ $SCALE == auto ]]; then
  ms=$(hyprctl monitors -j 2>/dev/null | python3 -c 'import json,sys; m=[x for x in json.load(sys.stdin) if x.get("focused")] or [{{"scale":1}}]; print(m[0].get("scale", 1))' 2>/dev/null || echo 1)
  gtk_scale=$(python3 -c "print(round(1/float('$ms'), 4))" 2>/dev/null || echo 1)
fi

args=(
  -name "$VM_NAME"
  -machine virt,gic-version=3,highmem=on,memory-backend=mem
  -accel kvm -cpu host,pmu=on -smp "$VCPUS"
  -object memory-backend-memfd,id=mem,size="${{MEM}}M",share=on
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.fd
  -drive if=pflash,format=raw,file="$VARS"
  -device qemu-xhci,id=xhci,p2=8,p3=8
  -device usb-kbd -device virtio-tablet-pci -device virtio-keyboard-pci
  -nic user,model=virtio-net-pci
  -audiodev pipewire,id=snd0 -device virtio-sound-pci,audiodev=snd0
  -device virtio-serial-pci
  -chardev qemu-vdagent,id=vdagent,name=vdagent,clipboard=on -device virtserialport,chardev=vdagent,name=com.redhat.spice.0
  -chardev socket,id=qga0,path="$PWD/qga.sock",server=on,wait=off -device virtserialport,chardev=qga0,name=org.qemu.guest_agent.0
  -device virtio-balloon-pci
  -rtc base=utc
  -monitor unix:"$PWD/monitor.sock",server=on,wait=off
)

if [[ $mode == install ]]; then
  args+=(-drive if=none,id=disk0,file="$DISK",format=qcow2,cache=none,aio=io_uring,discard=unmap
         -device virtio-blk-pci,drive=disk0,bootindex=1)
  if [[ -n $ISO && -f $ISO ]]; then
    args+=(-drive if=none,id=cdrom,media=cdrom,readonly=on,file="$ISO" -device usb-storage,drive=cdrom,removable=on,bootindex=0)
  fi
else
  args+=(-drive if=none,id=disk0,file="$DISK",format=qcow2,cache=none,aio=io_uring,discard=unmap
         -device virtio-blk-pci,drive=disk0,bootindex=0)
  if [[ -n $ISO && -f $ISO ]]; then
    args+=(-drive if=none,id=cdrom,media=cdrom,readonly=on,file="$ISO" -device usb-storage,drive=cdrom,removable=on,bootindex=2)
  fi
fi

if [[ $GPU == 3d ]]; then
  args+=(-device virtio-gpu-gl-pci,blob=on,venus=on,hostmem="$HOSTMEM",edid=on,xres=1920,yres=1200)
else
  args+=(-device virtio-gpu-pci,edid=on,xres=1920,yres=1200)
fi

display="gtk,gl=on,zoom-to-fit=off,show-menubar=off,scale=$gtk_scale"
[[ $FULLSCREEN == yes ]] && display+=",full-screen=on"

read -r ulo uhi < <(uclamp_for "$PERF")
perf_request "$PERF"

exec uclampset -m "$ulo" -M "$uhi" taskset -c "$PCORES" qemu-system-aarch64 "${{args[@]}}" -display "$display" "${{@:2}}"
"""
    else:
        return f"""#!/bin/bash
# {name} x86_64 VM runner script
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

mode=${{1:-start}}

conf=()
[[ -r vm.conf ]] && mapfile -t conf < <(grep -E '^[A-Za-z0-9_]+=' vm.conf)
for kv in "${{conf[@]}}"; do name=${{kv%%=*}}; [[ -n ${{!name:-}} ]] || declare "$kv"; done

VCPUS=${{VCPUS:-4}}
MEM=${{MEM:-4096}}
ISO=${{ISO:-}}

VM_NAME="{slug}"

qemu_pid() {{
  local p
  while read -r p; do
    [[ -n $p && $(readlink "/proc/$p/cwd" 2>/dev/null) == "$PWD" ]] && {{ echo "$p"; return; }}
  done < <(pgrep -f "qemu-system-x86_64.*-name $VM_NAME( |$)" || true)
}}

running() {{ [[ -n $(qemu_pid) ]]; }}
monitor() {{ echo "$*" | socat - UNIX-CONNECT:monitor.sock >/dev/null 2>&1 || true; }}

case $mode in
  status) running && echo running || echo stopped; exit 0 ;;
  stop)   running && monitor system_powerdown; exit 0 ;;
  kill)   running && monitor quit; exit 0 ;;
  info)   echo "state=$(running && echo running || echo stopped)"; exit 0 ;;
  start|install)
    if running; then exit 0; fi
    ;;
  *) echo "usage: $0 start|install|stop|kill|status|info" >&2; exit 2 ;;
esac

DISK=disk.qcow2
[[ -f $DISK ]] || qemu-img create -f qcow2 "$DISK" 40G

args=(
  -name "$VM_NAME"
  -machine q35 -accel tcg -m "${{MEM}}M" -smp "$VCPUS"
  -drive if=virtio,file="$DISK",format=qcow2
  -device virtio-net-pci,netdev=net0 -netdev user,id=net0
  -device virtio-vga
  -monitor unix:"$PWD/monitor.sock",server=on,wait=off
)

if [[ -n $ISO && -f $ISO ]]; then
  boot_order=c
  [[ $mode == install ]] && boot_order=d
  args+=(-cdrom "$ISO" -boot "$boot_order")
fi

exec qemu-system-x86_64 "${{args[@]}}" -display gtk "${{@:2}}"
"""


class Manager(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("QEMU VM Manager")
        self.geometry("820x620")
        self.minsize(720, 520)
        self._ui_scale = None

        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure("TButton", font="TkDefaultFont")
        style.configure("TCombobox", font="TkDefaultFont")
        style.configure("TEntry", font="TkDefaultFont")
        style.configure("TLabelframe.Label", font="TkHeadingFont")

        self.profiles = read_registry()
        self.selected = tk.IntVar(value=0)
        self.status_var = tk.StringVar(value="")
        self.disk_var = tk.StringVar(value="")
        self.profile_var = tk.StringVar()
        self.preset_var = tk.StringVar()

        keys = ("VCPUS", "MEM", "LIVE_MEM", "GPU", "HOSTMEM", "FULLSCREEN", "PERF", "SCALE", "REFRESH", "ISO")
        self.vars = {k: tk.StringVar() for k in keys}

        self.build_ui()
        self.refresh_profile_list()
        self.bind("<Configure>", self.update_ui_scale)
        self.after_idle(self.update_ui_scale)
        self.after(1500, self.refresh_status)

    def update_ui_scale(self, _event=None):
        width = self.winfo_width()
        height = self.winfo_height()
        if width < 100 or height < 100:
            return
        scale = 1.30 * min(width / 820, height / 620)
        scale = max(1.10, min(1.75, scale))
        if self._ui_scale is not None and abs(scale - self._ui_scale) < 0.04:
            return
        self._ui_scale = scale
        tkfont.nametofont("TkDefaultFont").configure(size=round(11 * scale))
        tkfont.nametofont("TkTextFont").configure(size=round(11 * scale))
        tkfont.nametofont("TkMenuFont").configure(size=round(11 * scale))
        tkfont.nametofont("TkHeadingFont").configure(size=round(12 * scale), weight="bold")
        style = ttk.Style(self)
        style.configure("TButton", padding=(round(10 * scale), round(6 * scale)))
        style.configure("TCombobox", padding=(round(5 * scale), round(4 * scale)))
        style.configure("TEntry", padding=(round(5 * scale), round(4 * scale)))

    def current(self):
        if not self.profiles:
            return None
        idx = max(0, min(self.selected.get(), len(self.profiles) - 1))
        return self.profiles[idx]

    def vm_dir(self) -> Path | None:
        p = self.current()
        return Path(p["dir"]).expanduser() if p and "dir" in p else None

    def script(self) -> Path | None:
        d = self.vm_dir()
        if not d or not d.exists():
            return None
        for name in ("vm.sh", "ubuntu-arm.sh", "win-arm.sh", "start.sh", "run.sh"):
            candidate = d / name
            if candidate.exists() and os.access(candidate, os.X_OK):
                return candidate
        candidates = [p for p in d.glob("*.sh") if os.access(p, os.X_OK)]
        return candidates[0] if candidates else None

    def build_ui(self):
        outer = ttk.Frame(self, padding=12)
        outer.pack(fill="both", expand=True)

        top = ttk.Frame(outer)
        top.pack(fill="x")
        ttk.Label(top, text="VM Profile:").pack(side="left")
        self.profile_box = ttk.Combobox(top, textvariable=self.profile_var, state="readonly", width=26)
        self.profile_box.pack(side="left", padx=6)
        self.profile_box.bind("<<ComboboxSelected>>", self.profile_changed)

        ttk.Button(top, text="New VM", command=lambda: self.open_new_vm_wizard()).pack(side="left", padx=2)
        ttk.Button(top, text="Download ISOs", command=self.open_iso_downloader).pack(side="left", padx=2)
        ttk.Button(top, text="Add Folder", command=self.add_profile).pack(side="left", padx=2)
        ttk.Button(top, text="Rename", command=self.rename_profile).pack(side="left", padx=2)
        ttk.Button(top, text="Remove", command=self.remove_profile).pack(side="left", padx=2)

        ttk.Label(outer, textvariable=self.status_var, foreground="#285a8f", font="TkDefaultFont 10 bold").pack(
            anchor="w", pady=(10, 4)
        )

        actions = ttk.LabelFrame(outer, text="Power, Lifecycle & Clipboard", padding=8)
        actions.pack(fill="x")
        buttons = (
            ("Start / Show", self.start),
            ("Install (Boot ISO)", self.install_vm),
            ("Suspend", self.suspend),
            ("Resume", self.resume),
            ("Restart", self.restart),
            ("Guest Shutdown", self.shutdown),
            ("Power Off", self.poweroff),
            ("Fully Close", self.force_close),
            ("Paste to VM", self.paste_clipboard),
        )
        for i, (label, command) in enumerate(buttons):
            ttk.Button(actions, text=label, command=command).grid(
                row=i // 5, column=i % 5, padx=4, pady=4, sticky="ew"
            )
        for i in range(5):
            actions.columnconfigure(i, weight=1)

        resources = ttk.LabelFrame(outer, text="Resources (boot settings unless marked live)", padding=8)
        resources.pack(fill="x", pady=10)

        self.add_info_label(resources, "Preset", "PRESET", 0, 0)
        self.preset_box = ttk.Combobox(resources, textvariable=self.preset_var, values=("", *PRESETS), state="readonly")
        self.preset_box.grid(row=1, column=0, columnspan=4, sticky="ew", padx=4, pady=(0, 6))
        self.preset_box.bind("<<ComboboxSelected>>", self.apply_preset)

        fields = (
            ("vCPUs", "VCPUS"),
            ("Memory (MiB)", "MEM"),
            ("Live memory (MiB)", "LIVE_MEM"),
            ("GPU", "GPU"),
            ("3D aperture", "HOSTMEM"),
            ("Performance", "PERF"),
            ("Display scale", "SCALE"),
            ("Fullscreen", "FULLSCREEN"),
            ("Display refresh (Hz)", "REFRESH"),
        )
        for i, (label, key) in enumerate(fields):
            row, col = divmod(i, 4)
            base_row = 2 + row * 2
            self.add_info_label(resources, label, key, base_row, col)
            if key == "GPU":
                widget = ttk.Combobox(resources, textvariable=self.vars[key], values=("2d", "3d"), state="readonly", width=12)
            elif key == "PERF":
                widget = ttk.Combobox(resources, textvariable=self.vars[key], values=("eco", "balanced", "performance"), state="readonly", width=12)
            elif key == "FULLSCREEN":
                widget = ttk.Combobox(resources, textvariable=self.vars[key], values=("yes", "no"), state="readonly", width=12)
            elif key == "VCPUS":
                widget = ttk.Combobox(resources, textvariable=self.vars[key], values=VCPU_VALUES, width=12)
            elif key == "MEM":
                widget = ttk.Combobox(resources, textvariable=self.vars[key], values=("2048", "4096", "6144", "8192", "12288", "16384", "20480"), width=12)
            elif key == "HOSTMEM":
                widget = ttk.Combobox(resources, textvariable=self.vars[key], values=("2G", "4G", "8G", "12G"), width=12)
            elif key == "REFRESH":
                widget = ttk.Combobox(resources, textvariable=self.vars[key], values=("60", "120"), state="readonly", width=12)
            elif key == "SCALE":
                widget = ttk.Combobox(resources, textvariable=self.vars[key], values=("auto", "1", "0.75"), width=12)
            else:
                widget = ttk.Entry(resources, textvariable=self.vars[key], width=14)
            widget.grid(row=base_row + 1, column=col, sticky="ew", padx=4, pady=(0, 4))

        iso_row = 8
        self.add_info_label(resources, "Attached Boot / Install ISO", "ISO", iso_row, 0)
        iso_frame = ttk.Frame(resources)
        iso_frame.grid(row=iso_row + 1, column=0, columnspan=4, sticky="ew", padx=4, pady=(0, 6))
        ttk.Entry(iso_frame, textvariable=self.vars["ISO"]).pack(side="left", fill="x", expand=True)
        ttk.Button(iso_frame, text="Browse ISO...", command=self.browse_vm_iso).pack(side="right", padx=(4, 0))

        for i in range(4):
            resources.columnconfigure(i, weight=1)

        ttk.Button(resources, text="Apply Resource Settings", command=self.apply_resources).grid(
            row=11, column=0, columnspan=4, pady=6
        )

        disk = ttk.LabelFrame(outer, text="Virtual Disk", padding=8)
        disk.pack(fill="x")
        ttk.Label(disk, textvariable=self.disk_var, justify="left").pack(side="left", fill="x", expand=True)
        ttk.Button(disk, text="Resize Stopped VM Disk", command=self.resize_disk).pack(side="right")

    def add_info_label(self, parent, text, key, row, column):
        frame = ttk.Frame(parent)
        ttk.Label(frame, text=text).pack(side="left")
        icon = ttk.Label(frame, text="ⓘ", foreground="#285a8f", cursor="question_arrow")
        icon.pack(side="left", padx=(5, 0))
        Tooltip(icon, FIELD_HELP.get(key, text))
        frame.grid(row=row, column=column, sticky="w", padx=4, pady=(2, 0))

    def browse_vm_iso(self):
        f = filedialog.askopenfilename(
            title="Choose Boot/Install ISO",
            initialdir=str(DEFAULT_DOWNLOADS_DIR),
            filetypes=[("ISO Files", "*.iso"), ("All Files", "*.*")],
            parent=self,
        )
        if f:
            self.vars["ISO"].set(f)

    def refresh_profile_list(self):
        names = [p["name"] for p in self.profiles]
        self.profile_box["values"] = names
        if names:
            idx = min(self.selected.get(), len(names) - 1)
            self.selected.set(idx)
            self.profile_box.current(idx)
            self.load_config()

    def profile_changed(self, _event=None):
        self.selected.set(self.profile_box.current())
        self.load_config()
        self.refresh_status()

    def load_config(self):
        d = self.vm_dir()
        values = parse_conf(d / "vm.conf") if d else {}
        defaults = {
            "VCPUS": "6",
            "MEM": "8192",
            "LIVE_MEM": "",
            "GPU": "3d",
            "HOSTMEM": "4G",
            "FULLSCREEN": "no",
            "PERF": "performance",
            "SCALE": "auto",
            "REFRESH": "60",
            "ISO": "",
        }
        for key in self.vars:
            default = LIVE_MEMORY_PLACEHOLDER if key == "LIVE_MEM" else defaults[key]
            self.vars[key].set(values.get(key, default))
        self.preset_var.set("")
        self.update_disk_info()

    def apply_preset(self, _event=None):
        name = self.preset_var.get()
        if not name or name not in PRESETS:
            return
        for key, value in PRESETS[name].items():
            if key in self.vars:
                self.vars[key].set(value)
        self.vars["LIVE_MEM"].set(LIVE_MEMORY_PLACEHOLDER)
        self.status_var.set(f"{name} preset loaded; click 'Apply Resource Settings' to save.")

    def run_script(self, action: str, *args, background=False):
        script = self.script()
        if not script or not script.exists():
            messagebox.showerror(
                "VM Runner Script Missing",
                f"No runner script (vm.sh, ubuntu-arm.sh, or win-arm.sh) found in:\n{self.vm_dir()}",
                parent=self,
            )
            return None
        try:
            if background:
                log_path = self.vm_dir() / "vm-manager.log"
                log = open(log_path, "ab")
                return subprocess.Popen(
                    [str(script), action, *args],
                    cwd=self.vm_dir(),
                    stdout=log,
                    stderr=log,
                    start_new_session=True,
                )
            return subprocess.run(
                [str(script), action, *args],
                cwd=self.vm_dir(),
                text=True,
                capture_output=True,
                timeout=15,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            messagebox.showerror("VM Command Failed", str(exc), parent=self)
            return None

    def monitor(self, command: str) -> bool:
        d = self.vm_dir()
        if not d or not (d / "monitor.sock").exists():
            messagebox.showinfo("VM", "This VM is not running.", parent=self)
            return False
        try:
            subprocess.run(
                ["socat", "-", f"UNIX-CONNECT:{d / 'monitor.sock'}"],
                input=command + "\n",
                text=True,
                timeout=3,
                check=True,
                capture_output=True,
            )
            return True
        except (OSError, subprocess.SubprocessError) as exc:
            messagebox.showerror("QEMU Monitor", str(exc), parent=self)
            return False

    def refresh_status(self):
        result = self.run_script("info") if self.current() else None
        info = parse_info(result.stdout if result else "")
        state = info.get("state", "stopped")

        if info.get("balloon"):
            self.vars["LIVE_MEM"].set(info["balloon"])
        elif state == "running":
            self.vars["LIVE_MEM"].set(LIVE_MEMORY_READING)
        else:
            self.vars["LIVE_MEM"].set(LIVE_MEMORY_PLACEHOLDER)

        profile_name = self.profile_var.get() or "No VM selected"
        vcpus_str = f"{info.get('vcpus', self.vars['VCPUS'].get())} vCPUs"
        mem_str = f"{info.get('mem', self.vars['MEM'].get())} MiB"
        gpu_str = f"{info.get('gpu', self.vars['GPU'].get())} GPU"
        self.status_var.set(f"{profile_name}  ·  {state.upper()}  ·  {gpu_str}  ·  {mem_str}  ·  {vcpus_str}")
        self.update_disk_info()
        self.after(1500, self.refresh_status)

    def update_disk_info(self):
        d = self.vm_dir()
        disk = d / "disk.qcow2" if d else None
        if not disk or not disk.exists():
            self.disk_var.set("Disk: not found")
            return
        try:
            data = json.loads(subprocess.check_output(["qemu-img", "info", "-U", "--output=json", str(disk)], text=True))
            virtual = data.get("virtual-size", 0) / (1024 ** 3)
            actual = data.get("actual-size", 0) / (1024 ** 3)
            self.disk_var.set(f"{disk}\nVirtual size: {virtual:.1f} GiB   ·   Host allocation: {actual:.1f} GiB")
        except (OSError, subprocess.SubprocessError, ValueError):
            self.disk_var.set(str(disk))

    def start(self):
        self.run_script("start", background=True)

    def install_vm(self):
        self.run_script("install", background=True)

    def suspend(self):
        self.monitor("stop")

    def resume(self):
        self.monitor("cont")

    def restart(self):
        self.monitor("system_reset")

    def shutdown(self):
        if messagebox.askyesno("Guest Shutdown", "Request clean guest shutdown via ACPI power button?", parent=self):
            self.monitor("system_powerdown")

    def poweroff(self):
        if messagebox.askyesno("Power Off", "Power off QEMU immediately? Unsaved guest data may be lost.", parent=self):
            self.monitor("quit")

    def force_close(self):
        if not messagebox.askyesno("Fully Close", "Terminate the QEMU process immediately?", parent=self):
            return
        self.monitor("quit")
        time.sleep(0.4)
        d = self.vm_dir()
        if not d:
            return
        for proc in Path("/proc").glob("[0-9]*"):
            try:
                pid = int(proc.name)
                cmd = (proc / "cmdline").read_bytes().replace(b"\0", b" ").decode(errors="ignore")
                cwd = (proc / "cwd").resolve()
                if "qemu-system" in cmd and cwd == d:
                    os.kill(pid, signal.SIGTERM)
            except (OSError, ValueError):
                continue
        time.sleep(0.3)
        for proc in Path("/proc").glob("[0-9]*"):
            try:
                pid = int(proc.name)
                cmd = (proc / "cmdline").read_bytes().replace(b"\0", b" ").decode(errors="ignore")
                cwd = (proc / "cwd").resolve()
                if "qemu-system" in cmd and cwd == d:
                    os.kill(pid, signal.SIGKILL)
            except (OSError, ValueError):
                continue

    def paste_clipboard(self):
        d = self.vm_dir()
        if not d or not (d / "monitor.sock").exists():
            messagebox.showinfo("Paste to VM", "The selected VM is not running.", parent=self)
            return
        try:
            res = subprocess.run(["vm-paste"], capture_output=True, text=True, timeout=8)
            if res.returncode == 0:
                self.status_var.set("✓ Host clipboard pasted into VM!")
            else:
                msg = res.stderr.strip() or "Clipboard is empty."
                messagebox.showinfo("Paste to VM", msg, parent=self)
        except Exception as exc:
            messagebox.showerror("Paste to VM", str(exc), parent=self)

    def apply_resources(self):
        live_mem = self.vars["LIVE_MEM"].get().strip()
        if live_mem in (LIVE_MEMORY_PLACEHOLDER, LIVE_MEMORY_READING):
            live_mem = ""
        if live_mem:
            if not live_mem.isdigit():
                messagebox.showerror("Resources", "Live memory must be a number in MiB.", parent=self)
                return
            result = self.run_script("memory", live_mem)
            if result and result.returncode:
                messagebox.showerror("Resources", result.stderr.strip() or result.stdout.strip(), parent=self)
                return

        for key in ("VCPUS", "MEM", "GPU", "HOSTMEM", "FULLSCREEN", "SCALE"):
            val = self.vars[key].get().strip()
            if not val:
                messagebox.showerror("Resources", f"{key} cannot be empty.", parent=self)
                return
            if key in ("VCPUS", "MEM") and not val.isdigit():
                messagebox.showerror("Resources", f"{key} must be a number.", parent=self)
                return
            result = self.run_script("set", key, val)
            if result and result.returncode:
                messagebox.showerror("Resources", result.stderr.strip() or result.stdout.strip(), parent=self)
                return

        iso_val = self.vars["ISO"].get().strip()
        if iso_val:
            self.run_script("set", "ISO", iso_val)

        perf = self.vars["PERF"].get().strip()
        if perf not in ("eco", "balanced", "performance"):
            messagebox.showerror("Resources", "Performance must be eco, balanced, or performance.", parent=self)
            return
        result = self.run_script("perf", perf)
        if result and result.returncode:
            messagebox.showerror("Resources", result.stderr.strip() or result.stdout.strip(), parent=self)
            return

        refresh = self.vars["REFRESH"].get().strip()
        prev = parse_conf(self.vm_dir() / "vm.conf" if self.vm_dir() else None).get("REFRESH", "60")
        if refresh not in ("60", "120"):
            messagebox.showerror("Resources", "Display refresh must be 60 or 120.", parent=self)
            return
        if refresh != prev:
            self.run_script("refresh", refresh)

        self.load_config()
        messagebox.showinfo("Resources", "Resource settings applied successfully.", parent=self)

    def resize_disk(self):
        d = self.vm_dir()
        disk = d / "disk.qcow2" if d else None
        if not disk or not disk.exists():
            messagebox.showwarning("Disk", "No disk.qcow2 found in this VM directory.", parent=self)
            return
        state = self.run_script("status")
        if state and state.stdout.strip() == "running":
            messagebox.showwarning("Disk", "Please stop the VM before resizing its disk.", parent=self)
            return
        current = simpledialog.askfloat("Resize Disk", "New virtual size in GiB (grow only):", minvalue=1, parent=self)
        if current is None:
            return
        try:
            subprocess.run(["qemu-img", "resize", str(disk), f"{current:g}G"], check=True, capture_output=True, text=True)
            self.update_disk_info()
            messagebox.showinfo("Disk Resize", f"Virtual disk resized to {current:g} GiB.", parent=self)
        except subprocess.CalledProcessError as exc:
            messagebox.showerror("Disk Resize Failed", exc.stderr.strip(), parent=self)

    def open_iso_downloader(self):
        IsoDownloaderDialog(self)

    def open_new_vm_wizard(self, initial_iso: str = "", initial_name: str = ""):
        NewVmDialog(self, initial_iso=initial_iso, initial_name=initial_name)

    def add_profile(self):
        d = filedialog.askdirectory(title="Choose a VM Directory", initialdir=str(DEFAULT_VMS_DIR), parent=self)
        if not d:
            return
        name = simpledialog.askstring("Name VM", "Name for this VM profile:", initialvalue=Path(d).name, parent=self)
        if name:
            self.profiles.append({"name": name.strip(), "dir": d})
            write_registry(self.profiles)
            self.selected.set(len(self.profiles) - 1)
            self.refresh_profile_list()

    def rename_profile(self):
        p = self.current()
        if not p:
            return
        name = simpledialog.askstring("Rename VM", "New VM name:", initialvalue=p["name"], parent=self)
        if name:
            p["name"] = name.strip()
            write_registry(self.profiles)
            self.refresh_profile_list()

    def remove_profile(self):
        if len(self.profiles) <= 1:
            messagebox.showinfo("VM Profiles", "Keep at least one profile.", parent=self)
            return
        p = self.current()
        if p and messagebox.askyesno(
            "Remove Profile",
            f"Remove '{p['name']}' from this manager?\n\nThe VM files on disk will NOT be deleted.",
            parent=self,
        ):
            self.profiles.pop(self.selected.get())
            write_registry(self.profiles)
            self.selected.set(max(0, self.selected.get() - 1))
            self.refresh_profile_list()


if __name__ == "__main__":
    app = Manager()
    if "--isos" in sys.argv or "--download-iso" in sys.argv:
        app.after_idle(app.open_iso_downloader)
    elif "--new-vm" in sys.argv:
        app.after_idle(app.open_new_vm_wizard)
    app.mainloop()
