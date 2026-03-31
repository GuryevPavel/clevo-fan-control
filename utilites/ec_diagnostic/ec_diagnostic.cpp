#include <stdio.h>
#include <stdlib.h>
#include <sys/io.h>
#include <unistd.h>
#include <stdint.h>  // Добавлено для uint8_t, uint32_t
#include <string.h>

#define EC_SC 0x66
#define EC_DATA 0x62
#define EC_SC_READ_CMD 0x80

// Функции для работы с EC (как в вашем коде)
static int ec_io_wait(const uint32_t port, const uint32_t flag, const char value) {
    uint8_t data = inb(port);
    int i = 0;
    while ((((data >> flag) & 0x1) != value) && (i++ < 100)) {
        usleep(1000);
        data = inb(port);
    }
    if (i >= 100) {
        printf("wait_ec error on port 0x%x, data=0x%x, flag=0x%x, value=0x%x\n",
               port, data, flag, value);
        return -1;
    }
    return 0;
}

static uint8_t ec_io_read(const uint32_t port) {
    if (ec_io_wait(EC_SC, 1, 0) != 0) return 0;  // IBF
    outb(EC_SC_READ_CMD, EC_SC);

    if (ec_io_wait(EC_SC, 1, 0) != 0) return 0;
    outb(port, EC_DATA);

    if (ec_io_wait(EC_SC, 0, 1) != 0) return 0;  // OBF
    return inb(EC_DATA);
}

// Функция для записи в EC (для тестирования)
static int ec_io_write(const uint32_t cmd, const uint32_t port, const uint8_t value) {
    if (ec_io_wait(EC_SC, 1, 0) != 0) return -1;
    outb(cmd, EC_SC);

    if (ec_io_wait(EC_SC, 1, 0) != 0) return -1;
    outb(port, EC_DATA);

    if (ec_io_wait(EC_SC, 1, 0) != 0) return -1;
    outb(value, EC_DATA);

    return ec_io_wait(EC_SC, 1, 0);
}

// Функция для тестирования записи в GPU вентилятор
static void test_gpu_write() {
    printf("\n");
    printf("TESTING GPU FAN WRITE COMMANDS\n");
    printf("==============================\n");

    // Сохраняем текущие значения
    uint8_t orig_duty_cpu = ec_io_read(0xCE);
    uint8_t orig_duty_gpu1 = ec_io_read(0xCF);
    uint8_t orig_duty_gpu2 = ec_io_read(0xE4);

    printf("Original values:\n");
    printf("  CPU Duty (0xCE): 0x%02X\n", orig_duty_cpu);
    printf("  GPU Duty1 (0xCF): 0x%02X\n", orig_duty_gpu1);
    printf("  GPU Duty2 (0xE4): 0x%02X\n", orig_duty_gpu2);

    // Тест 1: Команда 0x99 с портом 0x01 (CPU)
    printf("\nTest 1: cmd=0x99, port=0x01, value=0x80 (CPU)\n");
    ec_io_write(0x99, 0x01, 0x80);
    usleep(100000);  // 100ms

    uint8_t new_duty_cpu = ec_io_read(0xCE);
    uint8_t new_duty_gpu1 = ec_io_read(0xCF);
    uint8_t new_duty_gpu2 = ec_io_read(0xE4);

    printf("  CPU Duty (0xCE): 0x%02X (changed: %s)\n",
           new_duty_cpu, (new_duty_cpu != orig_duty_cpu) ? "YES" : "NO");
    printf("  GPU Duty1 (0xCF): 0x%02X (changed: %s)\n",
           new_duty_gpu1, (new_duty_gpu1 != orig_duty_gpu1) ? "YES" : "NO");
    printf("  GPU Duty2 (0xE4): 0x%02X (changed: %s)\n",
           new_duty_gpu2, (new_duty_gpu2 != orig_duty_gpu2) ? "YES" : "NO");

    // Восстанавливаем CPU
    ec_io_write(0x99, 0x01, orig_duty_cpu);

    // Тест 2: Команда 0x99 с портом 0x02 (возможно GPU)
    printf("\nTest 2: cmd=0x99, port=0x02, value=0x80 (GPU?)\n");
    ec_io_write(0x99, 0x02, 0x80);
    usleep(100000);

    new_duty_gpu1 = ec_io_read(0xCF);
    new_duty_gpu2 = ec_io_read(0xE4);

    printf("  GPU Duty1 (0xCF): 0x%02X (changed: %s)\n",
           new_duty_gpu1, (new_duty_gpu1 != orig_duty_gpu1) ? "YES" : "NO");
    printf("  GPU Duty2 (0xE4): 0x%02X (changed: %s)\n",
           new_duty_gpu2, (new_duty_gpu2 != orig_duty_gpu2) ? "YES" : "NO");

    // Тест 3: Команда 0x9A с портом 0x01 (альтернативная)
    printf("\nTest 3: cmd=0x9A, port=0x01, value=0x80 (GPU?)\n");
    ec_io_write(0x9A, 0x01, 0x80);
    usleep(100000);

    new_duty_gpu1 = ec_io_read(0xCF);
    new_duty_gpu2 = ec_io_read(0xE4);

    printf("  GPU Duty1 (0xCF): 0x%02X (changed: %s)\n",
           new_duty_gpu1, (new_duty_gpu1 != orig_duty_gpu1) ? "YES" : "NO");
    printf("  GPU Duty2 (0xE4): 0x%02X (changed: %s)\n",
           new_duty_gpu2, (new_duty_gpu2 != orig_duty_gpu2) ? "YES" : "NO");

    // Тест 4: Прямая запись в регистр 0xE4
    printf("\nTest 4: Direct write to 0xE4 using cmd=0x99, port=0xE4, value=0x80\n");
    ec_io_write(0x99, 0xE4, 0x80);
    usleep(100000);

    new_duty_gpu2 = ec_io_read(0xE4);
    printf("  GPU Duty2 (0xE4): 0x%02X (changed: %s)\n",
           new_duty_gpu2, (new_duty_gpu2 != orig_duty_gpu2) ? "YES" : "NO");

    // Восстанавливаем исходные значения
    printf("\nRestoring original values...\n");
    ec_io_write(0x99, 0x01, orig_duty_cpu);
    if (orig_duty_gpu1 != new_duty_gpu1) {
        ec_io_write(0x99, 0x02, orig_duty_gpu1);
    }
    if (orig_duty_gpu2 != new_duty_gpu2) {
        ec_io_write(0x99, 0xE4, orig_duty_gpu2);
    }
}

int main() {
    printf("Clevo N960KPx EC Register Diagnostic\n");
    printf("====================================\n\n");

    // Получаем права на порты
    if (ioperm(EC_DATA, 1, 1) != 0 || ioperm(EC_SC, 1, 1) != 0) {
        printf("ERROR: Need root privileges. Run with sudo.\n");
        return 1;
    }

    printf("Reading EC registers...\n\n");

    // Читаем известные регистры CPU вентилятора
    uint8_t cpu_duty = ec_io_read(0xCE);
    uint8_t cpu_rpm_hi = ec_io_read(0xD0);
    uint8_t cpu_rpm_lo = ec_io_read(0xD1);
    int cpu_raw_rpm = (cpu_rpm_hi << 8) + cpu_rpm_lo;
    int cpu_rpm = cpu_raw_rpm > 0 ? (2156220 / cpu_raw_rpm) : 0;

    printf("CPU FAN:\n");
    printf("  Duty (0xCE): %d (0x%02X) -> %d%%\n",
           cpu_duty, cpu_duty, (int)((double)cpu_duty / 255.0 * 100.0));
    printf("  RPM raw: 0x%02X%02X = %d -> %d RPM\n",
           cpu_rpm_hi, cpu_rpm_lo, cpu_raw_rpm, cpu_rpm);

    printf("\nChecking potential GPU fan registers:\n");

    // Расширенный список регистров для проверки
    int registers[] = {
        0xCF, 0xD2, 0xD3, 0xD4, 0xE0, 0xE1, 0xE4, 0xE5,
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87  // Дополнительные регистры
    };

    const char* names[] = {
        "Duty (GPU1)", "RPM high (GPU1)", "RPM low (GPU1)", "Unknown",
        "RPM high (GPU2)", "RPM low (GPU2)", "Duty (GPU2)", "Unknown",
        "Reg 0x80", "Reg 0x81", "Reg 0x82", "Reg 0x83",
        "Reg 0x84", "Reg 0x85", "Reg 0x86", "Reg 0x87"
    };

    for (int i = 0; i < 16; i++) {
        uint8_t value = ec_io_read(registers[i]);
        printf("  0x%02X (%s): %d (0x%02X)",
               registers[i], names[i], value, value);

        // Если это RPM регистры, пытаемся вычислить скорость
        if (registers[i] == 0xD2 || registers[i] == 0xD3 ||
            registers[i] == 0xE0 || registers[i] == 0xE1) {
            // Нужна пара регистров для RPM
            if (registers[i] == 0xD2) {
                uint8_t low = ec_io_read(0xD3);
                int raw = (value << 8) + low;
                int rpm = raw > 0 ? (2156220 / raw) : 0;
                printf(" -> pair 0xD2-0xD3 = %d RPM", rpm);
            } else if (registers[i] == 0xE0) {
                uint8_t low = ec_io_read(0xE1);
                int raw = (value << 8) + low;
                int rpm = raw > 0 ? (2156220 / raw) : 0;
                printf(" -> pair 0xE0-0xE1 = %d RPM", rpm);
            }
            }
            printf("\n");
    }

    printf("\n");
    printf("TEMPERATURE SENSORS:\n");
    uint8_t cpu_temp = ec_io_read(0x07);
    uint8_t gpu_temp = ec_io_read(0xCD);
    uint8_t ambient_temp = ec_io_read(0xCA);

    printf("  CPU Temp (0x07): %d°C\n", cpu_temp);
    printf("  GPU Temp (0xCD): %d°C\n", gpu_temp);
    printf("  Ambient (0xCA): %d°C\n", ambient_temp);

    printf("\n");
    printf("TEST PROCEDURE:\n");
    printf("===============\n");
    printf("1. Run this diagnostic with normal fan speed\n");
    printf("2. Press Fn+1 to toggle max fan speed\n");
    printf("3. Run again with max fan speed and compare values\n");
    printf("4. Run with GPU under load (game/benchmark)\n");
    printf("5. Check which registers change with GPU fan speed\n\n");

    // Спрашиваем, хочет ли пользователь протестировать запись
    printf("Do you want to test GPU fan write commands? (y/n): ");
    char answer = getchar();
    if (answer == 'y' || answer == 'Y') {
        test_gpu_write();
    }

    return 0;
}
