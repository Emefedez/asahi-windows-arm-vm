@echo off
rem Installs the QEMU guest agent (x64 build; runs under Windows' x64 emulation) from the virtio-win ISO.
rem The MSI cannot be installed normally on ARM64: its RegisterCom custom action runs the ARM64
rem rundll32 on the x64 qga-vss.dll and fails (error 1722). Extract it and register the service by hand.
for %%d in (D E F G H I J K) do if exist %%d:\guest-agent\qemu-ga-x86_64.msi set QGA_MSI=%%d:\guest-agent\qemu-ga-x86_64.msi
if "%QGA_MSI%"=="" (echo virtio-win ISO not found & exit /b 1)
start /wait msiexec /a "%QGA_MSI%" /qn TARGETDIR=C:\qga
xcopy /e /i /y /q "C:\qga\QEMU Guest Agent\Qemu-ga" "C:\Program Files\Qemu-ga"
sc create QEMU-GA binPath= "\"C:\Program Files\Qemu-ga\qemu-ga.exe\" -d --retry-path" start= auto DisplayName= "QEMU Guest Agent"
net start QEMU-GA
