# Development Notes for Clevo Fan Control Utility

## Key Findings

### 1. GPU Temperature Register
**CORRECT:** `0xFB`
**INCORRECT:** `0xCD` (always returns 0)

This explains why GPU temperature always showed 0°C in previous versions.

### 2. Fan Control Method
Both CPU and GPU fans are controlled via EC command `0x99` with specific ports:
- CPU: `ec_io_write(0x99, 0x01, duty_value)`
- GPU: `ec_io_write(0x99, 0x02, duty_value)`

Direct writes to registers `0xCE` and `0xCF` may not work reliably; use commands.

### 3. Module Conflicts
When `tuxedo_io` or `clevo_acpi` are loaded, direct ioperm access may:
- Fail silently
- Cause thread synchronization issues
- Produce `QThread::wait: Thread tried to wait on itself` errors

**Solution:** Either unload conflicting modules OR modify utility to use sysfs interface.

## Recommended Architecture for Main Utility

### Option A: Unload Conflicting Modules
```bash
# Before starting utility
sudo modprobe -r tuxedo_io tuxedo_keyboard tuxedo_compatibility_check clevo_acpi
sudo ./clevo-fan-control-kde
```

### Option B: Use sysfs Interface (if modules loaded)

The utility should detect available interface:

    Check /sys/devices/platform/tuxedo_io/ → use sysfs

    Check /sys/kernel/debug/ec/ec0/io → use debugfs

    Fallback to ioperm

### Option C: Modify main utility to use correct GPU temp register

In main.cpp, change:

```cpp
#define EC_REG_GPU_TEMP 0xCD   // OLD (incorrect)
#define EC_REG_GPU_TEMP 0xFB   // NEW (correct)
```

### Tested Working Code (gpu_fan_test_fixed.cpp)

The following functions are confirmed working:

```cpp
// Set GPU fan to percentage
int set_gpu_fan(int percent) {
    int value = (percent * 255) / 100;
    return ec_io_write(0x99, 0x02, value);
}

// Read GPU temperature
int read_gpu_temp() {
    return ec_io_read(0xFB);  // CORRECT register
}

// Calculate RPM from raw values
int calculate_rpm(int high, int low) {
    int raw = (high << 8) + low;
    return raw > 0 ? 2156220 / raw : 0;
}
```

### Next Steps

1) Update main utility:

   Change GPU temp register from 0xCD to 0xFB

   Add interface detection (ioperm vs sysfs)

   Fix thread synchronization issues

2) Create launcher script:

```bash
#!/bin/bash
# launch-fan-control.sh
sudo modprobe -r tuxedo_io tuxedo_keyboard tuxedo_compatibility_check clevo_acpi 2>/dev/null
sudo ./clevo-fan-control-kde
```

3) Documentation:

   Keep this EC register map for future reference

   Share findings with community (Clevo Linux users)
   
Version History

    2026-03-21: Discovered correct GPU temp register (0xFB)

    2026-03-21: Confirmed fan control via cmd=0x99, port=0x02

    2026-03-21: Documented module conflicts
    
    
```text

---

### Файл: `QUICK_REFERENCE.txt`

```text
╔═══════════════════════════════════════════════════════════════════════════════╗
║              QUICK REFERENCE - Clevo N960KPx Fan Control                      ║
╚═══════════════════════════════════════════════════════════════════════════════╝

┌───────────────────────────────────────────────────────────────────────────────┐
│  REGISTERS                                                                   │
├───────────────────────────────────────────────────────────────────────────────┤
│  CPU Temp      : 0x07                                                         │
│  GPU Temp      : 0xFB  ⚠️ NOT 0xCD!                                          │
│  Ambient Temp  : 0xCA                                                         │
│  CPU Fan Duty  : 0xCE                                                         │
│  GPU Fan Duty  : 0xCF                                                         │
│  CPU RPM       : 0xD0 (high), 0xD1 (low)                                      │
│  GPU RPM       : 0xD2 (high), 0xD3 (low)                                      │
└───────────────────────────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────────────────────────┐
│  COMMANDS (ec_io_write)                                                       │
├───────────────────────────────────────────────────────────────────────────────┤
│  Set CPU Fan   : ec_io_write(0x99, 0x01, value)                               │
│  Set GPU Fan   : ec_io_write(0x99, 0x02, value)                               │
│  Auto Mode     : ec_io_write(0x99, 0xFF, 0xFF)                                │
└───────────────────────────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────────────────────────┐
│  RPM FORMULA                                                                  │
├───────────────────────────────────────────────────────────────────────────────┤
│  RPM = 2156220 / ((high << 8) + low)                                          │
└───────────────────────────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────────────────────────┐
│  FIX GPU TEMP IN CODE                                                         │
├───────────────────────────────────────────────────────────────────────────────┤
│  #define EC_REG_GPU_TEMP 0xFB   // instead of 0xCD                           │
└───────────────────────────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────────────────────────┐
│  BEFORE RUNNING YOUR UTILITY (if tuxedo modules loaded)                       │
├───────────────────────────────────────────────────────────────────────────────┤
│  sudo modprobe -r tuxedo_io tuxedo_keyboard tuxedo_compatibility_check       │
│  sudo modprobe -r clevo_acpi                                                  │
└───────────────────────────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────────────────────────┐
│  TEST COMMANDS                                                                │
├───────────────────────────────────────────────────────────────────────────────┤
│  sudo ./gpu_fan_test_fixed status                                             │
│  sudo ./gpu_fan_test_fixed gpu 50                                             │
│  sudo ./gpu_fan_test_fixed test                                               │
└───────────────────────────────────────────────────────────────────────────────┘
```




