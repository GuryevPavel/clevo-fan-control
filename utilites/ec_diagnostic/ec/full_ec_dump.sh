#!/bin/bash
# full_ec_dump.sh - Полный дамп всех EC регистров

echo "=== Full EC Register Dump ==="
echo "Date: $(date)"
echo ""

# Проверяем доступность EC
if [ ! -f /sys/kernel/debug/ec/ec0/io ]; then
    sudo mount -t debugfs none /sys/kernel/debug
fi

# Создаём файл с дампом
dump_file="ec_full_dump_$(date +%Y%m%d_%H%M%S).txt"

echo "Register | Value | Dec | Description" > "$dump_file"
echo "---------|-------|-----|-------------" >> "$dump_file"

for reg in $(seq 0 255); do
    hex_reg=$(printf "0x%02X" $reg)
    val=$(sudo dd if=/sys/kernel/debug/ec/ec0/io bs=1 skip=$reg count=1 2>/dev/null | xxd -p)
    if [ -n "$val" ]; then
        dec_val=$((0x$val))
        # Добавляем описание для известных регистров
        desc=""
        case $reg in
            0x07) desc="CPU Temp" ;;
            0x80|0x81|0x82|0x83|0x84|0x85|0x86|0x87) desc="GPU Temp candidate" ;;
            0xCA) desc="Ambient Temp" ;;
            0xCE) desc="CPU Fan Duty" ;;
            0xCF) desc="GPU Fan Duty" ;;
            0xD0) desc="CPU RPM High" ;;
            0xD1) desc="CPU RPM Low" ;;
            0xD2) desc="GPU RPM High" ;;
            0xD3) desc="GPU RPM Low" ;;
        esac
        printf "%-8s | 0x%02X    | %3d  | %s\n" "$hex_reg" $((0x$val)) $dec_val "$desc" >> "$dump_file"
    fi
done

echo "Dump saved to: $dump_file"
cat "$dump_file"
