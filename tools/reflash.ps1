$ErrorActionPreference = 'Stop'
# 1. 停掉串口桥接 (释放 COM6)
Get-CimInstance Win32_Process -Filter "Name='python.exe'" |
  Where-Object { $_.CommandLine -like '*wb_serial_bridge*' } |
  ForEach-Object { Write-Host ("kill PID " + $_.ProcessId); Stop-Process -Id $_.ProcessId -Force }
Start-Sleep -Seconds 2

# 2. 编译
cd E:\code\esp32-codex-quota\tools
.\arduino-cli.exe compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" --build-path ..\wb_serial_display\build ..\wb_serial_display 2>&1 | Select-Object -Last 3

# 3. 烧录
.\arduino-cli.exe upload -p COM6 --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" --input-dir ..\wb_serial_display\build 2>&1 | Select-Object -Last 4

# 4. 重启桥接
Start-Process -FilePath 'C:/Users/Administrator/.workbuddy/binaries/python/envs/default/Scripts/python.exe' `
  -ArgumentList '-u','E:/code/esp32-codex-quota/bridge/wb_serial_bridge.py' `
  -WindowStyle Hidden -RedirectStandardOutput 'E:/code/esp32-codex-quota/tools/bridge.log' `
  -RedirectStandardError 'E:/code/esp32-codex-quota/tools/bridge.err'
Start-Sleep -Seconds 12
Write-Host '--- bridge log ---'
Get-Content 'E:/code/esp32-codex-quota/tools/bridge.log' -Tail 3
