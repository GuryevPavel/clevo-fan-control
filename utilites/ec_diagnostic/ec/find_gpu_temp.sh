#!/bin/bash
# find_gpu_temp.sh - Поиск регистра GPU температуры

echo "Searching for GPU temperature register..."

# Включаем GPU если он в режиме экономии
sudo nvidia-smi -pm 1 2>/dev/null

# Получаем температуру через nvidia-smi
nvidia_temp=$(nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader)
echo "nvidia-smi reports GPU temp: ${nvidia_temp}°C"

# Проверяем доступность EC
if [ ! -f /sys/kernel/debug/ec/ec0/io ]; then
    echo "EC interface not available. Mounting debugfs..."
    sudo mount -t debugfs none /sys/kernel/debug
fi

echo "Scanning EC registers for GPU temperature..."

# Сканируем возможные регистры
found=0
for reg in $(seq 0x80 0x8F) $(seq 0xA0 0xAF) $(seq 0xC0 0xCF) $(seq 0xD4 0xFF); do
    # Читаем регистр
    val=$(sudo dd if=/sys/kernel/debug/ec/ec0/io bs=1 skip=$reg count=1 2>/dev/null | xxd -p)
    if [ -n "$val" ]; then
        temp=$((0x$val))
        # Ищем значение, близкое к температуре от nvidia-smi (±5°)
        if [ $temp -ge $((nvidia_temp - 5)) ] && [ $temp -le $((nvidia_temp + 5)) ]; then
            echo "✅ FOUND: Register 0x$(printf %02X $reg) = $temp°C (matches nvidia-smi)"
            found=1
        elif [ $temp -gt 30 ] && [ $temp -lt 100 ]; then
            echo "   Candidate: 0x$(printf %02X $reg) = $temp°C"
        fi
    fi
done

if [ $found -eq 0 ]; then
    echo "No exact match found. Possible registers for GPU temp:"
    # Повторный проход с поиском значений в диапазоне 30-100°C
    for reg in $(seq 0x80 0x8F) $(seq 0xA0 0xAF) $(seq 0xC0 0xCF); do
        val=$(sudo dd if=/sys/kernel/debug/ec/ec0/io bs=1 skip=$reg count=1 2>/dev/null | xxd -p)
        if [ -n "$val" ]; then
            temp=$((0x$val))
            if [ $temp -gt 30 ] && [ $temp -lt 100 ]; then
                echo "   0x$(printf %02X $reg) = $temp°C"
            fi
        fi
    done
fi
