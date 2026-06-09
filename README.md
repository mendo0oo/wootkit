# Wootkit Kernel Sensor

Wootkit Kernel Sensor is a defensive Windows kernel telemetry component for
early process, image-load, and registry visibility. It runs as a system-start
driver and exposes recent events to a user-mode collector through a read-only
IOCTL interface.

The driver is intentionally non-stealth. It does not hide files, processes,
drivers, registry keys, or network state. It does not patch kernel memory, hook
SSDT, perform DKOM, bypass driver signing, or disable security products.

## Capabilities

- Process create and exit telemetry
- Image and module load telemetry
- Registry create-key and set-value telemetry
- In-memory event ring buffer
- Read-only collector interface
- System-start service configuration
- Suspicious signal tagging for common persistence paths:
  - user-writable image locations such as `Users`, `AppData`, and `Temp`
  - autorun registry values
  - service `ImagePath` changes
  - IFEO `Debugger` changes
  - Winlogon `Shell` and `Userinit` changes
  - `AppInit_DLLs` changes

## Requirements

- Windows 10 or Windows 11
- Administrator PowerShell
- Visual Studio Build Tools with MSVC C++ tools
- Windows Driver Kit
- Driver signing policy suitable for the build being loaded

Check and install build requirements:

```powershell
.\scripts\bootstrap_requirements.ps1
.\scripts\bootstrap_requirements.ps1 -Install
```

The bootstrap script uses `winget` to install Git, Visual Studio Build Tools,
and the Windows Driver Kit when they are missing.

Prepare the host for local test-signed driver loading:

```powershell
.\scripts\bootstrap_requirements.ps1 -PrepareHost
Restart-Computer
```

`-PrepareHost` installs missing build tools, enables Windows test-signing, and
creates/trusts a local code-signing certificate for Wootkit driver builds.

## Build

Build the driver:

```powershell
.\driver\build_driver.ps1
```

Build the collector:

```powershell
.\collector\build_collector.ps1
```

Build both:

```powershell
.\scripts\bootstrap_requirements.ps1 -Build
```

Build and sign the driver:

```powershell
.\scripts\bootstrap_requirements.ps1 -Build -SignDriver
```

Run the full local setup path:

```powershell
.\scripts\bootstrap_requirements.ps1 -Full
Restart-Computer
.\scripts\install_sensor.ps1
```

Build outputs:

```text
driver\x64\Release\WootkitSensor.sys
collector\WootkitCollector.exe
```

## Install

Install and start the kernel sensor from an Administrator PowerShell:

```powershell
.\scripts\install_sensor.ps1
```

The install script copies the driver to `System32\drivers`, registers the INF,
creates the kernel service as system-start, and starts the service.

## Collect Events

Read available kernel events:

```powershell
.\collector\WootkitCollector.exe
```

Events are emitted as JSON lines. Each event can include:

- `type`
- `pid`
- `ppid`
- `tid`
- `severity`
- `suspicious`
- `kernel_image`
- `rule`
- `image`
- `registry`

## Remove

Stop and remove the sensor:

```powershell
.\scripts\remove_sensor.ps1
```

## Notes

Wootkit Kernel Sensor is designed for defensive visibility and host inspection.
Kernel driver installation requires administrative control of the machine and a
valid driver signing posture for the environment where it is loaded.
