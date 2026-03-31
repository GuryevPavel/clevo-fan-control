#include <stdio.h>
#include <stdlib.h>
#include <sys/io.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#define EC_SC 0x66
#define EC_DATA 0x62
#define EC_SC_READ_CMD 0x80

#define EC_REG_FAN_CPU_DUTY     0xCE
#define EC_REG_FAN_CPU_RPMS_HI  0xD0
#define EC_REG_FAN_CPU_RPMS_LO  0xD1
#define EC_REG_FAN_GPU_DUTY     0xCF
#define EC_REG_FAN_GPU_RPMS_HI  0xD2
#define EC_REG_FAN_GPU_RPMS_LO  0xD3
#define MIN_FAN_DUTY 16

static int ec_io_wait(const uint32_t port, const uint32_t flag, const char value) {
    uint8_t data = inb(port);
    int i = 0;
    while ((((data >> flag) & 0x1) != value) && (i++ < 100)) {
        usleep(1000);
        data = inb(port);
    }
    return (i < 100) ? 0 : -1;
}

static uint8_t ec_io_read(const uint32_t port) {
    if (ec_io_wait(EC_SC, 1, 0) != 0) return 0;
    outb(EC_SC_READ_CMD, EC_SC);

    if (ec_io_wait(EC_SC, 1, 0) != 0) return 0;
    outb(port, EC_DATA);

    if (ec_io_wait(EC_SC, 0, 1) != 0) return 0;
    return inb(EC_DATA);
}

static int ec_io_write(const uint32_t cmd, const uint32_t port, const uint8_t value) {
    if (ec_io_wait(EC_SC, 1, 0) != 0) return -1;
    outb(cmd, EC_SC);
    if (ec_io_wait(EC_SC, 1, 0) != 0) return -1;
    outb(port, EC_DATA);
    if (ec_io_wait(EC_SC, 1, 0) != 0) return -1;
    outb(value, EC_DATA);
    return ec_io_wait(EC_SC, 1, 0);
}

static int calculate_fan_duty(int raw_duty) {
    return (int)((double)raw_duty / 255.0 * 100.0);
}

static int calculate_fan_rpms(int raw_rpm_high, int raw_rpm_low) {
    int raw_rpm = (raw_rpm_high << 8) + raw_rpm_low;
    return raw_rpm > 0 ? (2156220 / raw_rpm) : 0;
}

static void print_fan_status() {
    uint8_t cpu_duty = ec_io_read(EC_REG_FAN_CPU_DUTY);
    uint8_t cpu_rpm_hi = ec_io_read(EC_REG_FAN_CPU_RPMS_HI);
    uint8_t cpu_rpm_lo = ec_io_read(EC_REG_FAN_CPU_RPMS_LO);
    int cpu_raw_rpm = (cpu_rpm_hi << 8) + cpu_rpm_lo;
    int cpu_rpm = cpu_raw_rpm > 0 ? (2156220 / cpu_raw_rpm) : 0;

    uint8_t gpu_duty = ec_io_read(EC_REG_FAN_GPU_DUTY);
    uint8_t gpu_rpm_hi = ec_io_read(EC_REG_FAN_GPU_RPMS_HI);
    uint8_t gpu_rpm_lo = ec_io_read(EC_REG_FAN_GPU_RPMS_LO);
    int gpu_raw_rpm = (gpu_rpm_hi << 8) + gpu_rpm_lo;
    int gpu_rpm = gpu_raw_rpm > 0 ? (2156220 / gpu_raw_rpm) : 0;

    uint8_t cpu_temp = ec_io_read(0x07);
    uint8_t gpu_temp = ec_io_read(0xCD);

    printf("\n=== CURRENT FAN STATUS ===\n");
    printf("CPU Temp: %d°C\n", cpu_temp);
    printf("GPU Temp: %d°C\n", gpu_temp);
    printf("\n");
    printf("CPU Fan: Duty=0x%02X (%d%%), RPM=%d\n",
           cpu_duty, calculate_fan_duty(cpu_duty), cpu_rpm);
    printf("GPU Fan: Duty=0x%02X (%d%%), RPM=%d\n",
           gpu_duty, calculate_fan_duty(gpu_duty), gpu_rpm);
    printf("==========================\n\n");
}

static void test_gpu_control_fixed() {
    printf("GPU FAN CONTROL TEST (FIXED VERSION)\n");
    printf("=====================================\n\n");

    uint8_t orig_cpu = ec_io_read(EC_REG_FAN_CPU_DUTY);
    uint8_t orig_gpu = ec_io_read(EC_REG_FAN_GPU_DUTY);

    printf("Original values:\n");
    printf("  CPU reg 0xCE: %d (0x%02X)\n", orig_cpu, orig_cpu);
    printf("  GPU reg 0xCF: %d (0x%02X)\n\n", orig_gpu, orig_gpu);

    print_fan_status();

    // Тест 1: Установка GPU на 50% через правильную команду
    printf("\nTEST 1: Setting GPU to 50%% (cmd=0x99, port=0x02, value=128)\n");
    ec_io_write(0x99, 0x02, 128);
    sleep(2);
    print_fan_status();

    // Тест 2: Установка GPU на 25%
    printf("\nTEST 2: Setting GPU to 25%% (cmd=0x99, port=0x02, value=64)\n");
    ec_io_write(0x99, 0x02, 64);
    sleep(2);
    print_fan_status();

    // Тест 3: Установка GPU на 75%
    printf("\nTEST 3: Setting GPU to 75%% (cmd=0x99, port=0x02, value=192)\n");
    ec_io_write(0x99, 0x02, 192);
    sleep(2);
    print_fan_status();

    // Тест 4: Установка GPU на 100%
    printf("\nTEST 4: Setting GPU to 100%% (cmd=0x99, port=0x02, value=255)\n");
    ec_io_write(0x99, 0x02, 255);
    sleep(2);
    print_fan_status();

    // Тест 5: Установка GPU на минимальные безопасные обороты
    printf("\nTEST 5: Setting GPU to MIN SAFE (%d%%)\n",
           calculate_fan_duty(MIN_FAN_DUTY));
    ec_io_write(0x99, 0x02, MIN_FAN_DUTY);
    sleep(2);
    print_fan_status();

    // Восстановление
    printf("\nRestoring original values...\n");
    ec_io_write(0x99, 0x01, orig_cpu);
    ec_io_write(0x99, 0x02, orig_gpu);
    sleep(2);
    print_fan_status();

    printf("\nTest complete!\n");
}

int main(int argc, char *argv[]) {
    printf("Clevo N960KPx GPU Fan Control Test (Fixed Version)\n");
    printf("==================================================\n\n");

    if (ioperm(EC_DATA, 1, 1) != 0 || ioperm(EC_SC, 1, 1) != 0) {
        printf("ERROR: Need root privileges. Run with sudo.\n");
        return 1;
    }

    if (argc > 1 && strcmp(argv[1], "test") == 0) {
        test_gpu_control_fixed();
    } else if (argc > 1 && strcmp(argv[1], "gpu") == 0 && argc > 2) {
        int percent = atoi(argv[2]);
        if (percent >= 0 && percent <= 100) {
            int value = (percent * 255) / 100;
            printf("Setting GPU fan to %d%% (0x%02X) via cmd=0x99, port=0x02\n",
                   percent, value);
            ec_io_write(0x99, 0x02, value);
            sleep(1);
            print_fan_status();
        }
    } else {
        print_fan_status();
        printf("\nUsage:\n");
        printf("  %s test      - Run automatic tests\n", argv[0]);
        printf("  %s gpu <%%>   - Set GPU fan to percentage\n", argv[0]);
    }

    return 0;
}
