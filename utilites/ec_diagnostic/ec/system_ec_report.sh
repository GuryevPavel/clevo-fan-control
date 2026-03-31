#!/bin/bash
# system_ec_report.sh - Полный отчёт о EC и системе

REPORT_DIR="ec_report_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$REPORT_DIR"
cd "$REPORT_DIR"

echo "=== EC System Report ===" > report.txt
echo "Generated: $(date)" >> report.txt
echo "Host: $(hostname)" >> report.txt
echo "Kernel: $(uname -a)" >> report.txt
echo "" >> report.txt

# 1. Информация о DMI
echo "=== DMI Information ===" >> report.txt
sudo dmidecode -t system >> report.txt 2>/dev/null
sudo dmidecode -t bios >> report.txt 2>/dev/null
sudo dmidecode -t baseboard >> report.txt 2>/dev/null

# 2. ACPI информация
echo "" >> report.txt
echo "=== ACPI Tables ===" >> report.txt
sudo acpidump > acpidump.dat 2>/dev/null
sudo acpixtract -a acpidump.dat 2>/dev/null
if [ -f dsdt.dat ]; then
    iasl -d dsdt.dat 2>/dev/null
    grep -i "ec0\|ec_|\_SB_.EC" dsdt.dsl >> report.txt 2>/dev/null
fi

# 3. Загруженные модули
echo "" >> report.txt
echo "=== Loaded Modules (clevo/tuxedo) ===" >> report.txt
lsmod | grep -E "clevo|tuxedo|ec" >> report.txt

# 4. PCI устройства
echo "" >> report.txt
echo "=== PCI Devices (LPC/ISA) ===" >> report.txt
lspci -vnn | grep -i "isa\|lpc\|82801" >> report.txt

# 5. I2C устройства
echo "" >> report.txt
echo "=== I2C Devices ===" >> report.txt
for bus in /dev/i2c-*; do
    if [ -e "$bus" ]; then
        echo "Bus $bus:" >> report.txt
        sudo i2cdetect -y $(echo $bus | grep -o '[0-9]*') >> report.txt 2>/dev/null
    fi
done

# 6. Super I/O чип
echo "" >> report.txt
echo "=== Super I/O Detection ===" >> report.txt
sudo modprobe it87 2>/dev/null
sensors >> report.txt 2>/dev/null

# 7. EC интерфейсы
echo "" >> report.txt
echo "=== EC Interfaces ===" >> report.txt
for path in /sys/kernel/debug/ec/* /sys/devices/platform/*ec* /sys/devices/platform/*clevo* /sys/devices/platform/*tuxedo*; do
    if [ -e "$path" ]; then
        echo "$path:" >> report.txt
        ls -la "$path" >> report.txt 2>/dev/null
    fi
done

# 8. Полный дамп EC регистров (если доступен)
echo "" >> report.txt
echo "=== EC Full Dump (if available) ===" >> report.txt
sudo modprobe ec_sys write_support=1 2>/dev/null
sudo mount -t debugfs none /sys/kernel/debug 2>/dev/null
if [ -f /sys/kernel/debug/ec/ec0/io ]; then
    echo "EC dump:" >> report.txt
    sudo dd if=/sys/kernel/debug/ec/ec0/io bs=1 count=256 2>/dev/null | xxd >> report.txt
fi

# 9. Информация о GPU и NVIDIA
echo "" >> report.txt
echo "=== GPU Information ===" >> report.txt
nvidia-smi --query-gpu=temperature.gpu,name --format=csv >> report.txt 2>/dev/null
echo "" >> report.txt
nvidia-smi -q | grep -i "temperature" >> report.txt 2>/dev/null

# 10. Температуры через разные методы
echo "" >> report.txt
echo "=== Temperature Readings ===" >> report.txt
echo "sensors output:" >> report.txt
sensors >> report.txt 2>/dev/null
echo "" >> report.txt
echo "/proc/acpi/ibm/thermal:" >> report.txt
cat /proc/acpi/ibm/thermal 2>/dev/null >> report.txt

echo "Report saved to: $REPORT_DIR/report.txt"
echo "Please upload the entire directory: $REPORT_DIR"
