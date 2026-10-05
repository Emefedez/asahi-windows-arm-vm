#!/usr/bin/env python3
"""Small Tkinter manager for the local QEMU Windows VM profiles."""
from __future__ import annotations

import json
import os
import signal
import subprocess
import time
import tkinter as tk
import tkinter.font as tkfont
from pathlib import Path
from tkinter import filedialog, messagebox, simpledialog, ttk


HOME = Path.home()
REGISTRY = HOME / ".config" / "windows-vm-manager.json"
DEFAULT_DIR = HOME / "VMs" / "windows-arm"
LIVE_MEMORY_PLACEHOLDER = "VM stopped"
LIVE_MEMORY_READING = "reading..."
PRESETS = {
    "Eco": {"VCPUS": "4", "MEM": "4096", "GPU": "2d", "HOSTMEM": "2G", "PERF": "eco", "SCALE": "auto", "FULLSCREEN": "no", "REFRESH": "60"},
    "Balanced": {"VCPUS": "6", "MEM": "8192", "GPU": "3d", "HOSTMEM": "4G", "PERF": "balanced", "SCALE": "auto", "FULLSCREEN": "no", "REFRESH": "60"},
    "Performance": {"VCPUS": "8", "MEM": "12288", "GPU": "3d", "HOSTMEM": "8G", "PERF": "performance", "SCALE": "auto", "FULLSCREEN": "no", "REFRESH": "60"},
}
FIELD_HELP = {
    "PRESET": "Loads a tested group of VM resource settings. Choose one, review the values, then apply them.",
    "VCPUS": "Virtual CPU count. QEMU is pinned to the host's Apple performance cores; extra vCPUs would oversubscribe them.",
    "MEM": "RAM assigned when the VM boots. Live memory cannot be raised above this boot maximum.",
    "LIVE_MEM": "Current balloon memory limit. It is read from the running guest and can only be changed while the VM is running.",
    "GPU": "2d uses the stable virtio-gpu path. 3d enables the Yttrium/Venus driver path.",
    "HOSTMEM": "Host-visible GPU aperture used by the 3d GPU mode.",
    "PERF": "QEMU scheduling profile. Performance raises the scheduler floor and requests the performance governor; P-core pinning is separate.",
    "SCALE": "Guest display scale. Auto compensates for the monitor's host scale factor.",
    "FULLSCREEN": "Whether QEMU starts fullscreen. Ctrl+Alt+F still toggles it after launch.",
    "REFRESH": "Windows display refresh rate (Yttrium 3D driver). 60 Hz is known good; 120 Hz matches the MacBook panel "
               "but is experimental. Windows reads it when the driver starts, so it applies after Windows restarts.",
}


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
VCPU_VALUES = tuple(str(value) for value in (2, 4, 6, 8, 10, 12) if value <= PCORE_COUNT)


def read_registry():
    try:
        data = json.loads(REGISTRY.read_text())
        profiles = data.get("profiles", [])
        if profiles:
            return profiles
    except (OSError, ValueError):
        pass
    return [{"name": "Windows ARM", "dir": str(DEFAULT_DIR)}]


def write_registry(profiles):
    REGISTRY.parent.mkdir(parents=True, exist_ok=True)
    REGISTRY.write_text(json.dumps({"profiles": profiles}, indent=2) + "\n")


def parse_conf(path):
    values = {}
    try:
        for line in path.read_text().splitlines():
            if "=" in line and not line.lstrip().startswith("#"):
                key, value = line.split("=", 1)
                values[key.strip()] = value.strip()
    except OSError:
        pass
    return values


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
        tk.Label(self.tip, text=self.text, justify="left", wraplength=340, padx=9, pady=7,
                 background="#fff8d8", foreground="#222", relief="solid", borderwidth=1).pack()

    def hide(self, _event=None):
        if self.timer:
            self.widget.after_cancel(self.timer)
            self.timer = None
        if self.tip:
            self.tip.destroy()
            self.tip = None


class Manager(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Windows VM Manager")
        self.geometry("760x560")
        self.minsize(680, 480)
        # Keep the existing window dimensions while scaling the controls with it.
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
        self.vars = {key: tk.StringVar() for key in ("VCPUS", "MEM", "LIVE_MEM", "GPU", "HOSTMEM", "FULLSCREEN", "PERF", "SCALE", "REFRESH")}
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
        # 1.30x at the normal 760x560 size; larger windows get larger controls.
        scale = 1.30 * min(width / 760, height / 560)
        scale = max(1.15, min(1.80, scale))
        if self._ui_scale is not None and abs(scale - self._ui_scale) < 0.04:
            return
        self._ui_scale = scale
        tkfont.nametofont("TkDefaultFont").configure(size=round(11 * scale))
        tkfont.nametofont("TkTextFont").configure(size=round(11 * scale))
        tkfont.nametofont("TkMenuFont").configure(size=round(11 * scale))
        tkfont.nametofont("TkHeadingFont").configure(size=round(12 * scale), weight="bold")
        style = ttk.Style(self)
        style.configure("TButton", padding=(round(11 * scale), round(7 * scale)))
        style.configure("TCombobox", padding=(round(5 * scale), round(4 * scale)))
        style.configure("TEntry", padding=(round(5 * scale), round(4 * scale)))

    def current(self):
        if not self.profiles:
            return None
        return self.profiles[self.selected.get()]

    def vm_dir(self):
        p = self.current()
        return Path(p["dir"]).expanduser() if p else None

    def script(self):
        d = self.vm_dir()
        return d / "win-arm.sh" if d else None

    def build_ui(self):
        outer = ttk.Frame(self, padding=12)
        outer.pack(fill="both", expand=True)
        top = ttk.Frame(outer)
        top.pack(fill="x")
        ttk.Label(top, text="VM profile:").pack(side="left")
        self.profile_box = ttk.Combobox(top, textvariable=self.profile_var, state="readonly", width=34)
        self.profile_box.pack(side="left", padx=8)
        self.profile_box.bind("<<ComboboxSelected>>", self.profile_changed)
        for label, command in (("Add", self.add_profile), ("Rename", self.rename_profile), ("Remove", self.remove_profile)):
            ttk.Button(top, text=label, command=command).pack(side="left", padx=2)

        ttk.Label(outer, textvariable=self.status_var, foreground="#285a8f").pack(anchor="w", pady=(10, 4))
        actions = ttk.LabelFrame(outer, text="Power and lifecycle", padding=8)
        actions.pack(fill="x")
        buttons = (
            ("Start / show", self.start), ("Suspend", self.suspend), ("Resume", self.resume),
            ("Restart", self.restart), ("Guest shutdown", self.shutdown),
            ("Power off", self.poweroff), ("Fully close", self.force_close),
        )
        for i, (label, command) in enumerate(buttons):
            ttk.Button(actions, text=label, command=command).grid(row=i // 4, column=i % 4, padx=4, pady=4, sticky="ew")
        for i in range(4):
            actions.columnconfigure(i, weight=1)

        resources = ttk.LabelFrame(outer, text="Resources (boot settings unless marked live)", padding=8)
        resources.pack(fill="x", pady=10)
        self.add_info_label(resources, "Preset", "PRESET", 0, 0)
        self.preset_box = ttk.Combobox(resources, textvariable=self.preset_var, values=("", *PRESETS), state="readonly", width=14)
        self.preset_box.grid(row=1, column=0, columnspan=4, sticky="ew", padx=4, pady=(0, 6))
        self.preset_box.bind("<<ComboboxSelected>>", self.apply_preset)
        fields = (("vCPUs", "VCPUS"), ("Memory (MiB)", "MEM"), ("Live memory (MiB)", "LIVE_MEM"), ("GPU", "GPU"), ("3D aperture", "HOSTMEM"),
                  ("Performance", "PERF"), ("Display scale", "SCALE"), ("Fullscreen", "FULLSCREEN"), ("Display refresh (Hz)", "REFRESH"))
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
                widget = ttk.Combobox(resources, textvariable=self.vars[key], values=("4096", "6144", "8192", "12288", "16384", "20480"), width=12)
            elif key == "HOSTMEM":
                widget = ttk.Combobox(resources, textvariable=self.vars[key], values=("2G", "4G", "8G", "12G"), width=12)
            elif key == "REFRESH":
                widget = ttk.Combobox(resources, textvariable=self.vars[key], values=("60", "120"), state="readonly", width=12)
            elif key == "SCALE":
                widget = ttk.Combobox(resources, textvariable=self.vars[key], values=("auto", "1", "0.75"), width=12)
            else:
                widget = ttk.Entry(resources, textvariable=self.vars[key], width=14)
            widget.grid(row=base_row + 1, column=col, sticky="ew", padx=4, pady=(0, 4))
        for i in range(4):
            resources.columnconfigure(i, weight=1)
        ttk.Button(resources, text="Apply resource settings", command=self.apply_resources).grid(row=10, column=0, columnspan=4, pady=5)

        disk = ttk.LabelFrame(outer, text="Disk", padding=8)
        disk.pack(fill="x")
        ttk.Label(disk, textvariable=self.disk_var, justify="left").pack(side="left", fill="x", expand=True)
        ttk.Button(disk, text="Resize stopped VM disk", command=self.resize_disk).pack(side="right")

    def add_info_label(self, parent, text, key, row, column):
        frame = ttk.Frame(parent)
        ttk.Label(frame, text=text).pack(side="left")
        icon = ttk.Label(frame, text="ⓘ", foreground="#285a8f", cursor="question_arrow")
        icon.pack(side="left", padx=(5, 0))
        Tooltip(icon, FIELD_HELP[key])
        frame.grid(row=row, column=column, sticky="w", padx=4, pady=(2, 0))


    def refresh_profile_list(self):
        names = [p["name"] for p in self.profiles]
        self.profile_box["values"] = names
        if names:
            self.selected.set(min(self.selected.get(), len(names) - 1))
            self.profile_box.current(self.selected.get())
            self.load_config()

    def profile_changed(self, _event=None):
        self.selected.set(self.profile_box.current())
        self.load_config()
        self.refresh_status()

    def load_config(self):
        d = self.vm_dir()
        values = parse_conf(d / "vm.conf") if d else {}
        defaults = {"VCPUS": "8", "MEM": "8192", "LIVE_MEM": "", "GPU": "3d", "HOSTMEM": "4G", "FULLSCREEN": "no", "PERF": "performance", "SCALE": "auto", "REFRESH": "60"}
        for key in self.vars:
            default = LIVE_MEMORY_PLACEHOLDER if key == "LIVE_MEM" else defaults[key]
            self.vars[key].set(values.get(key, default))
        self.preset_box.set("")
        self.update_disk_info()

    def apply_preset(self, _event=None):
        name = self.preset_var.get()
        if not name:
            return
        for key, value in PRESETS[name].items():
            self.vars[key].set(value)
        self.vars["LIVE_MEM"].set(LIVE_MEMORY_PLACEHOLDER)
        self.status_var.set(f"{name} preset loaded; click Apply resource settings to save it.")
        self.after(2500, self.refresh_status)

    def run_script(self, action, *args, background=False):
        script = self.script()
        if not script or not script.exists():
            messagebox.showerror("VM profile", "This profile does not contain win-arm.sh.")
            return None
        try:
            if background:
                log = open(self.vm_dir() / "vm-manager.log", "ab")
                return subprocess.Popen([str(script), action, *args], cwd=self.vm_dir(), stdout=log, stderr=log, start_new_session=True)
            return subprocess.run([str(script), action, *args], cwd=self.vm_dir(), text=True, capture_output=True, timeout=15)
        except (OSError, subprocess.TimeoutExpired) as exc:
            messagebox.showerror("VM command failed", str(exc))
            return None

    def monitor(self, command):
        d = self.vm_dir()
        if not d or not (d / "monitor.sock").exists():
            messagebox.showinfo("VM", "This VM is not running.")
            return False
        try:
            subprocess.run(["socat", "-", f"UNIX-CONNECT:{d / 'monitor.sock'}"], input=command + "\n", text=True, timeout=3, check=True, capture_output=True)
            return True
        except (OSError, subprocess.SubprocessError) as exc:
            messagebox.showerror("QEMU monitor", str(exc))
            return False

    def refresh_status(self):
        result = self.run_script("info") if self.current() else None
        info = parse_info(result.stdout if result else "")
        state = info.get("state", "no profile")
        if info.get("balloon"):
            self.vars["LIVE_MEM"].set(info["balloon"])
        elif state == "running":
            self.vars["LIVE_MEM"].set(LIVE_MEMORY_READING)
        else:
            self.vars["LIVE_MEM"].set(LIVE_MEMORY_PLACEHOLDER)
        self.status_var.set(f"{self.profile_var.get()}  ·  {state}  ·  {info.get('gpu', '')} GPU  ·  {info.get('mem', '')} MiB")
        self.update_disk_info()
        self.after(1500, self.refresh_status)

    def update_disk_info(self):
        d = self.vm_dir()
        disk = d / "disk.qcow2" if d else None
        if not disk or not disk.exists():
            self.disk_var.set("Disk: not found")
            return
        try:
            data = json.loads(subprocess.check_output(["qemu-img", "info", "--output=json", str(disk)], text=True))
            virtual = data.get("virtual-size", 0) / (1024 ** 3)
            actual = data.get("actual-size", 0) / (1024 ** 3)
            self.disk_var.set(f"{disk}\nVirtual size: {virtual:.1f} GiB   ·   Host allocation: {actual:.1f} GiB")
        except (OSError, subprocess.SubprocessError, ValueError):
            self.disk_var.set(str(disk))

    def start(self):
        self.run_script("start", background=True)

    def suspend(self): self.monitor("stop")
    def resume(self): self.monitor("cont")
    def restart(self): self.monitor("system_reset")

    def shutdown(self):
        if messagebox.askyesno("Guest shutdown", "Ask Windows to shut down?"):
            self.monitor("system_powerdown")

    def poweroff(self):
        if messagebox.askyesno("Power off", "Power off QEMU immediately? Unsaved guest work may be lost."):
            self.monitor("quit")

    def force_close(self):
        if not messagebox.askyesno("Fully close", "Terminate the QEMU process immediately?"):
            return
        self.monitor("quit")
        time.sleep(0.5)
        d = self.vm_dir()
        if not d:
            return
        for entry in Path("/proc").glob("[0-9]*"):
            try:
                pid = int(entry.name)
                command = (entry / "cmdline").read_bytes().replace(b"\0", b" ").decode(errors="ignore")
                cwd = (entry / "cwd").resolve()
                if "qemu-system-aarch64" in command and cwd == d:
                    os.kill(pid, signal.SIGTERM)
            except (OSError, ValueError):
                continue
        time.sleep(0.4)
        for entry in Path("/proc").glob("[0-9]*"):
            try:
                pid = int(entry.name)
                command = (entry / "cmdline").read_bytes().replace(b"\0", b" ").decode(errors="ignore")
                if "qemu-system-aarch64" in command and (entry / "cwd").resolve() == d:
                    os.kill(pid, signal.SIGKILL)
            except (OSError, ValueError):
                continue

    def apply_resources(self):
        live_mem = self.vars["LIVE_MEM"].get().strip()
        if live_mem in (LIVE_MEMORY_PLACEHOLDER, LIVE_MEMORY_READING):
            live_mem = ""
        if live_mem:
            if not live_mem.isdigit():
                messagebox.showerror("Resources", "Live memory must be a number in MiB.")
                return
            result = self.run_script("memory", live_mem)
            if result and result.returncode:
                messagebox.showerror("Resources", result.stderr.strip() or result.stdout.strip())
                return
        for key in ("VCPUS", "MEM", "GPU", "HOSTMEM", "FULLSCREEN", "SCALE"):
            value = self.vars[key].get().strip()
            if not value:
                messagebox.showerror("Resources", f"{key} cannot be empty.")
                return
            if key in ("VCPUS", "MEM") and not value.isdigit():
                messagebox.showerror("Resources", f"{key} must be a number.")
                return
            result = self.run_script("set", key, value)
            if result and result.returncode:
                messagebox.showerror("Resources", result.stderr.strip() or result.stdout.strip())
                return
        perf = self.vars["PERF"].get().strip()
        if perf not in ("eco", "balanced", "performance"):
            messagebox.showerror("Resources", "Performance must be eco, balanced, or performance.")
            return
        result = self.run_script("perf", perf)
        if result and result.returncode:
            messagebox.showerror("Resources", result.stderr.strip() or result.stdout.strip())
            return
        refresh = self.vars["REFRESH"].get().strip()
        previous = parse_conf(self.vm_dir() / "vm.conf").get("REFRESH", "60")
        if refresh not in ("60", "120"):
            messagebox.showerror("Resources", "Display refresh must be 60 or 120.")
            return
        if refresh != previous:
            result = self.run_script("refresh", refresh)
            if result and result.returncode:
                messagebox.showerror("Resources", result.stderr.strip() or result.stdout.strip())
                return
            messagebox.showinfo("Display refresh", f"Display refresh set to {refresh} Hz. It takes effect after Windows restarts"
                                " (if the VM is stopped, it is applied automatically at the next start).")
        self.load_config()

    def resize_disk(self):
        d = self.vm_dir()
        disk = d / "disk.qcow2" if d else None
        if not disk or not disk.exists():
            return
        state = self.run_script("status")
        if state and state.stdout.strip() == "running":
            messagebox.showwarning("Disk", "Stop the VM before resizing its disk.")
            return
        current = simpledialog.askfloat("Resize disk", "New virtual size in GiB (grow only):", minvalue=1, parent=self)
        if current is None:
            return
        try:
            subprocess.run(["qemu-img", "resize", str(disk), f"{current:g}G"], check=True, capture_output=True, text=True)
            self.update_disk_info()
        except subprocess.CalledProcessError as exc:
            messagebox.showerror("Disk resize", exc.stderr.strip())

    def add_profile(self):
        d = filedialog.askdirectory(title="Choose a VM directory", initialdir=str(HOME / "VMs"))
        if not d:
            return
        name = simpledialog.askstring("Name VM", "Name for this VM:", initialvalue=Path(d).name, parent=self)
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
            messagebox.showinfo("VM profiles", "Keep at least one profile.")
            return
        p = self.current()
        if p and messagebox.askyesno("Remove profile", f"Remove '{p['name']}' from this manager? The VM files will not be deleted."):
            self.profiles.pop(self.selected.get())
            write_registry(self.profiles)
            self.selected.set(max(0, self.selected.get() - 1))
            self.refresh_profile_list()


def parse_info(text):
    info = {}
    for line in text.splitlines():
        for token in line.split():
            if "=" in token:
                key, value = token.split("=", 1)
                info[key.strip()] = value.strip()
    return info


if __name__ == "__main__":
    Manager().mainloop()
