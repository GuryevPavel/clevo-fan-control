#!/bin/bash
# update_ec_registers.sh - Обновление файла регистров на основе реальных данных

cat > ec_registers_n960kpx.txt << 'EOF'
# EC Registers for Clevo N960KPx (Hasee TX9-CA5DP)
# Based on actual hardware testing with gpu_fan_test_fixed
# Generated: $(date)

## System Information
- Model: Notebook N960Kx
- BIOS: INSYDE 1.07.14 (05/18/2022)
- Chipset: Intel H570
- EC Controller: Integrated in ITE Super I/O (part of LPC)
- GPU: NVIDIA GeForce RTX 3070 Laptop GPU

## Confirmed EC Registers (via ioperm access)

| Register | Name | Access | Notes |
|----------|------|--------|-------|
| 0x07 | CPU_TEMP | RO | Confirmed working, value matches sensors |
| 0xCA | AMBIENT_TEMP | RO | Ambient/motherboard temperature |
| 0xCE | CPU_FAN_DUTY | RW | 0-255, write via cmd=0x99 port=0x01 |
| 0xCF | GPU_FAN_DUTY | RW | 0-255, write via cmd=0x99 port=0x02 |
| 0xD0 | CPU_FAN_RPM_HI | RO | High byte of RPM counter |
| 0xD1 | CPU_FAN_RPM_LO | RO | Low byte of RPM counter |
| 0xD2 | GPU_FAN_RPM_HI | RO | High byte of RPM counter |
| 0xD3 | GPU_FAN_RPM_LO | RO | Low byte of RPM counter |

## GPU Temperature Candidates (to be verified)
Based on scan, GPU temp likely in range 0x80-0x8F:

| Register | Status | Method |
|----------|--------|--------|
| 0xCD | ❌ Invalid | Always returns 0 |
| 0x80-0x8F | ⚠️ Pending | To be tested |

## Verified EC Commands

| Command | Port | Value | Effect | Status |
|---------|------|-------|--------|--------|
| 0x99 | 0x01 | 0x00-0xFF | Set CPU fan duty | ✅ Working |
| 0x99 | 0x02 | 0x00-0xFF | Set GPU fan duty | ✅ Working |
| 0x99 | 0xFF | - | Auto mode (both fans) | ✅ Working |

## RPM Calculation
Formula: RPM = 2156220 / ((high << 8) + low)

Tested values:
- 25% (0x40) → 1395 RPM
- 50% (0x80) → 2623 RPM
- 75% (0xC0) → 3648 RPM
- 100% (0xFF) → 4577 RPM

## Module Status
Currently loaded modules that may affect EC access:
- clevo_acpi (loaded)
- tuxedo_io (loaded)
- tuxedo_keyboard (loaded)

These modules provide alternative interfaces via sysfs:
- /sys/devices/platform/tuxedo_keyboard/
- /sys/kernel/debug/ec/ec0/io

EOF

echo "Updated EC registers saved to: ec_registers_n960kpx.txt"
