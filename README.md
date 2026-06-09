# Wootkit Kernel Sensor

Defensive Windows kernel sensor prototype for Wootkit.

This is a non-stealth boot/system-start driver concept. It does not hide files,
processes, drivers, registry keys, or network state. It does not patch kernel
memory, hook SSDT, perform DKOM, disable security products, or bypass signing.

## What It Can Do

- Register documented kernel callbacks:
  - process create/exit
  - image/module load
  - registry create-key and set-value events
- Store recent events in an in-memory ring buffer.
- Flag conservative suspicious signals:
  - user-writable image paths such as `Users`, `AppData`, or `Temp`
  - autorun values
  - service `ImagePath` changes
  - IFEO `Debugger` changes
  - Winlogon `Shell` or `Userinit` changes
  - `AppInit_DLLs` changes
- Expose a read-only IOCTL for the collector.
- Run as `SERVICE_SYSTEM_START` in the INF/install script.

## Requirements

- Windows VM
- Visual Studio Build Tools with MSVC
- Windows Driver Kit headers/libs
- Administrator shell
- Test signing enabled for VM testing:

```powershell
.\scripts\enable_testsigning.ps1
Restart-Computer
```

## Build Driver

```powershell
.\driver\build_driver.ps1
```

The script writes:

```text
driver\x64\Release\WootkitSensor.sys
```

## Install In VM

```powershell
.\scripts\install_sensor.ps1
```

## Build Collector

```powershell
.\collector\build_collector.ps1
```

## Read Events

```powershell
.\collector\WootkitCollector.exe
```

Each JSON line includes `severity`, `suspicious`, `kernel_image`, and `rule`
fields so boot-time persistence and unusual path signals can be filtered.

## Remove

```powershell
.\scripts\remove_sensor.ps1
```

Keep VM snapshots before installing any boot/system-start driver. A broken
kernel driver can blue-screen the VM before login.
