#include <stdio.h>
#include <stdlib.h>
#include <sys/io.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>
#include <sys/stat.h>

#define EC_SC 0x66
#define EC_DATA 0x62
#define EC_SC_READ_CMD 0x80

// EC регистры (из предыдущих тестов)
#define EC_REG_CPU_TEMP 0x07
#define EC_REG_GPU_TEMP 0xCD
#define EC_REG_AMBIENT_TEMP 0xCA
#define EC_REG_FAN_CPU_DUTY 0xCE
#define EC_REG_FAN_GPU_DUTY 0xCF
#define EC_REG_FAN_CPU_RPMS_HI 0xD0
#define EC_REG_FAN_CPU_RPMS_LO 0xD1
#define EC_REG_FAN_GPU_RPMS_HI 0xD2
#define EC_REG_FAN_GPU_RPMS_LO 0xD3

// Команды управления
#define EC_CMD_FAN 0x99
#define EC_PORT_CPU 0x01
#define EC_PORT_GPU 0x02

// Безопасные значения
#define MIN_SAFE_DUTY 20
#define MAX_TEST_DUTY 100
#define SAFE_TEMP_LIMIT 85
#define WARN_TEMP_LIMIT 75

// Структура для информации о NVIDIA
struct NvidiaInfo {
    int available;
    int temp;
    int gpu_util;
    int mem_util;
    int power_usage;
    char driver_version[64];
};

static NvidiaInfo nvidia = {0, 0, 0, 0, 0, ""};

//============================================================================
// Низкоуровневые функции EC
//============================================================================
static int ec_io_wait(const uint32_t port, const uint32_t flag, const char value) {
    uint8_t data = inb(port);
    int i = 0;
    while ((((data >> flag) & 0x1) != value) && (i++ < 100)) {
        usleep(1000);
        data = inb(port);
    }
    if (i >= 100) {
        printf("⚠️ EC wait error on port 0x%x, data=0x%x\n", port, data);
        return -1;
    }
    return 0;
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

//============================================================================
// Функции для работы с NVIDIA NVML (ИСПРАВЛЕННАЯ ВЕРСИЯ)
//============================================================================
static void nvidia_init() {
    // Пробуем разные возможные пути для NVML библиотеки
    const char* lib_paths[] = {
        "libnvidia-ml.so",
        "libnvidia-ml.so.1",
        "/usr/lib/x86_64-linux-gnu/libnvidia-ml.so",
        "/usr/lib/x86_64-linux-gnu/libnvidia-ml.so.1",
        "/usr/lib/libnvidia-ml.so",
        "/usr/lib/libnvidia-ml.so.1",
        NULL
    };

    void *handle = NULL;
    for (int i = 0; lib_paths[i] != NULL; i++) {
        handle = dlopen(lib_paths[i], RTLD_LAZY);
        if (handle) {
            printf("✅ NVIDIA NVML library found: %s\n", lib_paths[i]);
            break;
        }
    }

    if (!handle) {
        printf("ℹ️ NVIDIA NVML library not found - will use EC only\n");
        nvidia.available = 0;
        return;
    }

    // Определяем функции NVML
    typedef int (*nvmlInit_t)(void);
    typedef int (*nvmlShutdown_t)(void);
    typedef int (*nvmlDeviceGetHandleByIndex_t)(int, void**);
    typedef int (*nvmlDeviceGetTemperature_t)(void*, int, unsigned int*);
    typedef int (*nvmlSystemGetDriverVersion_t)(char*, int);
    typedef int (*nvmlDeviceGetUtilizationRates_t)(void*, void*);
    typedef int (*nvmlDeviceGetPowerUsage_t)(void*, unsigned int*);

    nvmlInit_t nvmlInit = (nvmlInit_t)dlsym(handle, "nvmlInit_v2");
    nvmlShutdown_t nvmlShutdown = (nvmlShutdown_t)dlsym(handle, "nvmlShutdown");
    nvmlDeviceGetHandleByIndex_t nvmlDeviceGetHandleByIndex =
    (nvmlDeviceGetHandleByIndex_t)dlsym(handle, "nvmlDeviceGetHandleByIndex_v2");
    nvmlDeviceGetTemperature_t nvmlDeviceGetTemperature =
    (nvmlDeviceGetTemperature_t)dlsym(handle, "nvmlDeviceGetTemperature");
    nvmlSystemGetDriverVersion_t nvmlSystemGetDriverVersion =
    (nvmlSystemGetDriverVersion_t)dlsym(handle, "nvmlSystemGetDriverVersion");
    nvmlDeviceGetUtilizationRates_t nvmlDeviceGetUtilizationRates =
    (nvmlDeviceGetUtilizationRates_t)dlsym(handle, "nvmlDeviceGetUtilizationRates");
    nvmlDeviceGetPowerUsage_t nvmlDeviceGetPowerUsage =
    (nvmlDeviceGetPowerUsage_t)dlsym(handle, "nvmlDeviceGetPowerUsage");

    if (!nvmlInit || !nvmlShutdown || !nvmlDeviceGetHandleByIndex || !nvmlDeviceGetTemperature) {
        printf("⚠️ Failed to get NVML functions\n");
        dlclose(handle);
        nvidia.available = 0;
        return;
    }

    // Инициализируем NVML
    if (nvmlInit() != 0) {
        printf("⚠️ NVML init failed\n");
        dlclose(handle);
        nvidia.available = 0;
        return;
    }

    // Получаем версию драйвера
    if (nvmlSystemGetDriverVersion) {
        nvmlSystemGetDriverVersion(nvidia.driver_version, sizeof(nvidia.driver_version));
        printf("✅ NVIDIA driver version: %s\n", nvidia.driver_version);
    }

    // Получаем handle первого GPU
    void *device;
    if (nvmlDeviceGetHandleByIndex(0, &device) != 0) {
        printf("⚠️ No NVIDIA GPU found\n");
        nvmlShutdown();
        dlclose(handle);
        nvidia.available = 0;
        return;
    }

    // Получаем температуру для проверки
    unsigned int temp = 0;
    if (nvmlDeviceGetTemperature(device, 0, &temp) == 0) {
        nvidia.temp = (int)temp;
        printf("✅ NVIDIA GPU detected, initial temperature: %d°C\n", nvidia.temp);
    }

    // Получаем использование GPU
    if (nvmlDeviceGetUtilizationRates && nvmlDeviceGetPowerUsage) {
        // Можно добавить чтение других параметров
    }

    nvidia.available = 1;

    // Закрываем (при обновлении будем открывать заново)
    nvmlShutdown();
    dlclose(handle);
}

static void nvidia_update() {
    if (!nvidia.available) return;

    // Пробуем разные пути к библиотеке
    const char* lib_paths[] = {
        "/usr/lib/x86_64-linux-gnu/libnvidia-ml.so",
        "/usr/lib/x86_64-linux-gnu/libnvidia-ml.so.1",
        "libnvidia-ml.so",
        "libnvidia-ml.so.1",
        NULL
    };

    void *handle = NULL;
    for (int i = 0; lib_paths[i] != NULL; i++) {
        handle = dlopen(lib_paths[i], RTLD_LAZY);
        if (handle) break;
    }

    if (!handle) return;

    typedef int (*nvmlInit_t)(void);
    typedef int (*nvmlShutdown_t)(void);
    typedef int (*nvmlDeviceGetHandleByIndex_t)(int, void**);
    typedef int (*nvmlDeviceGetTemperature_t)(void*, int, unsigned int*);

    nvmlInit_t nvmlInit = (nvmlInit_t)dlsym(handle, "nvmlInit_v2");
    nvmlDeviceGetHandleByIndex_t nvmlDeviceGetHandleByIndex =
    (nvmlDeviceGetHandleByIndex_t)dlsym(handle, "nvmlDeviceGetHandleByIndex_v2");
    nvmlDeviceGetTemperature_t nvmlDeviceGetTemperature =
    (nvmlDeviceGetTemperature_t)dlsym(handle, "nvmlDeviceGetTemperature");
    nvmlShutdown_t nvmlShutdown = (nvmlShutdown_t)dlsym(handle, "nvmlShutdown");

    if (!nvmlInit || !nvmlDeviceGetHandleByIndex || !nvmlDeviceGetTemperature || !nvmlShutdown) {
        dlclose(handle);
        return;
    }

    if (nvmlInit() != 0) {
        dlclose(handle);
        return;
    }

    void *device;
    if (nvmlDeviceGetHandleByIndex(0, &device) == 0) {
        unsigned int temp = 0;
        if (nvmlDeviceGetTemperature(device, 0, &temp) == 0) {
            nvidia.temp = (int)temp;
        }
    }

    nvmlShutdown();
    dlclose(handle);
}

static void nvidia_cleanup() {
    // Ничего не делаем, так как библиотеки уже закрыты
}

//============================================================================
// Функции для работы с вентиляторами
//============================================================================
static int calculate_fan_duty(int raw) {
    return (raw * 100) / 255;
}

static int calculate_rpm(int hi, int lo) {
    int raw = (hi << 8) + lo;
    return raw > 0 ? (2156220 / raw) : 0;
}

static void read_fan_status(int *cpu_duty, int *cpu_rpm, int *gpu_duty, int *gpu_rpm) {
    uint8_t cpu_d = ec_io_read(EC_REG_FAN_CPU_DUTY);
    uint8_t cpu_hi = ec_io_read(EC_REG_FAN_CPU_RPMS_HI);
    uint8_t cpu_lo = ec_io_read(EC_REG_FAN_CPU_RPMS_LO);
    uint8_t gpu_d = ec_io_read(EC_REG_FAN_GPU_DUTY);
    uint8_t gpu_hi = ec_io_read(EC_REG_FAN_GPU_RPMS_HI);
    uint8_t gpu_lo = ec_io_read(EC_REG_FAN_GPU_RPMS_LO);

    *cpu_duty = calculate_fan_duty(cpu_d);
    *cpu_rpm = calculate_rpm(cpu_hi, cpu_lo);
    *gpu_duty = calculate_fan_duty(gpu_d);
    *gpu_rpm = calculate_rpm(gpu_hi, gpu_lo);
}

static void read_temperatures(int *cpu_temp, int *ec_gpu_temp, int *ambient) {
    *cpu_temp = ec_io_read(EC_REG_CPU_TEMP);
    *ec_gpu_temp = ec_io_read(EC_REG_GPU_TEMP);
    *ambient = ec_io_read(EC_REG_AMBIENT_TEMP);
}

static void set_fan_cpu(int percent) {
    int value = (percent * 255) / 100;
    printf("  Setting CPU fan to %d%% (0x%02X)\n", percent, value);
    ec_io_write(EC_CMD_FAN, EC_PORT_CPU, value);
}

static void set_fan_gpu(int percent) {
    int value = (percent * 255) / 100;
    printf("  Setting GPU fan to %d%% (0x%02X)\n", percent, value);
    // Важно: при установке GPU, CPU должен быть на безопасном минимуме
    int cpu_safe = (MIN_SAFE_DUTY * 255) / 100;
    ec_io_write(EC_CMD_FAN, EC_PORT_CPU, cpu_safe);
    usleep(50000);
    ec_io_write(EC_CMD_FAN, EC_PORT_GPU, value);
}

static void set_both_fans(int percent) {
    int value = (percent * 255) / 100;
    printf("  Setting both fans to %d%% (0x%02X)\n", percent, value);
    ec_io_write(EC_CMD_FAN, EC_PORT_CPU, value);
    usleep(50000);
    ec_io_write(EC_CMD_FAN, EC_PORT_GPU, value);
}

static void set_auto_mode() {
    printf("  Setting AUTO mode (Fn+1 state)\n");
    ec_io_write(EC_CMD_FAN, EC_PORT_CPU, 0xFF);
    usleep(50000);
    ec_io_write(EC_CMD_FAN, EC_PORT_GPU, 0xFF);
}

//============================================================================
// Функции для вывода и логирования
//============================================================================
static void print_header() {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

    printf("\n%s ", time_str);
    for (int i = 0; i < 60; i++) printf("=");
    printf("\n");
}

static void print_status(const char* stage, int elapsed,
                         int cpu_duty, int cpu_rpm, int cpu_temp,
                         int gpu_duty, int gpu_rpm, int ec_gpu_temp,
                         int nvidia_temp, int ambient) {

    printf("[%s] %s (%d sec):\n", stage, stage, elapsed);
    printf("  🌡️  CPU: %d°C | Ambient: %d°C\n", cpu_temp, ambient);

    if (nvidia.available) {
        printf("  🌡️  GPU: EC=%d°C | NVIDIA=%d°C %s\n",
               ec_gpu_temp,
               nvidia_temp,
               (nvidia_temp != ec_gpu_temp && ec_gpu_temp > 0) ? "⚠️ MISMATCH" : "");
    } else {
        printf("  🌡️  GPU: EC=%d°C (NVIDIA not available)\n", ec_gpu_temp);
    }

    printf("  💨 CPU Fan: %3d%% (%4d RPM) %s\n",
           cpu_duty, cpu_rpm,
           (cpu_duty < MIN_SAFE_DUTY && cpu_temp > 50) ? "⚠️ LOW!" : "");

    printf("  💨 GPU Fan: %3d%% (%4d RPM) %s\n",
           gpu_duty, gpu_rpm,
           (gpu_duty < MIN_SAFE_DUTY && ec_gpu_temp > 50) ? "⚠️ LOW!" : "");

    printf("  ----------------------------------------\n");
                         }

                         static int safety_check(int cpu_temp, int ec_gpu_temp, int cpu_duty, int gpu_duty) {
                             int issues = 0;

                             if (cpu_temp > SAFE_TEMP_LIMIT && cpu_duty < MIN_SAFE_DUTY) {
                                 printf("  ⚠️  DANGER: CPU %d°C with fan %d%%!\n", cpu_temp, cpu_duty);
                                 issues++;
                             }

                             if (ec_gpu_temp > SAFE_TEMP_LIMIT && gpu_duty < MIN_SAFE_DUTY && ec_gpu_temp > 0) {
                                 printf("  ⚠️  DANGER: GPU %d°C with fan %d%%!\n", ec_gpu_temp, gpu_duty);
                                 issues++;
                             }

                             if (nvidia.available && nvidia.temp > SAFE_TEMP_LIMIT && gpu_duty < MIN_SAFE_DUTY) {
                                 printf("  ⚠️  DANGER: NVIDIA GPU %d°C with fan %d%%!\n", nvidia.temp, gpu_duty);
                                 issues++;
                             }

                             return issues;
                         }

                         //============================================================================
                         // Тестовые циклы
                         //============================================================================
                         static void run_test_cycle(int cycle_num) {
                             printf("\n🔥 CYCLE %d 🔥\n", cycle_num);
                             printf("──────────────────────────────────────────\n");

                             int cpu_duty, cpu_rpm, gpu_duty, gpu_rpm;
                             int cpu_temp, ec_gpu_temp, ambient;

                             // Сохраняем исходные значения
                             read_temperatures(&cpu_temp, &ec_gpu_temp, &ambient);
                             read_fan_status(&cpu_duty, &cpu_rpm, &gpu_duty, &gpu_rpm);
                             nvidia_update();

                             int orig_cpu_duty = cpu_duty;
                             int orig_gpu_duty = gpu_duty;

                             printf("📊 Initial state:\n");
                             printf("  CPU fan: %d%%, GPU fan: %d%%\n", cpu_duty, gpu_duty);
                             printf("  CPU temp: %d°C, GPU temp (EC): %d°C", cpu_temp, ec_gpu_temp);
                             if (nvidia.available) {
                                 printf(", GPU temp (NVIDIA): %d°C", nvidia.temp);
                             }
                             printf("\n\n");

                             sleep(2);

                             // Тест 1: Управление только CPU
                             printf("\n📋 TEST 1: CPU-only control\n");
                             int test_values[] = {30, 50, 70, 100};
                             for (int i = 0; i < 4; i++) {
                                 set_fan_cpu(test_values[i]);

                                 for (int s = 1; s <= 3; s++) {
                                     sleep(1);
                                     read_temperatures(&cpu_temp, &ec_gpu_temp, &ambient);
                                     read_fan_status(&cpu_duty, &cpu_rpm, &gpu_duty, &gpu_rpm);
                                     nvidia_update();

                                     print_status("CPU", s, cpu_duty, cpu_rpm, cpu_temp,
                                                  gpu_duty, gpu_rpm, ec_gpu_temp, nvidia.temp, ambient);

                                     if (safety_check(cpu_temp, ec_gpu_temp, cpu_duty, gpu_duty) > 0) {
                                         printf("⚠️ EMERGENCY: Restoring safe values!\n");
                                         set_both_fans(MIN_SAFE_DUTY);
                                         return;
                                     }
                                 }
                             }

                             // Возврат в AUTO
                             set_auto_mode();
                             sleep(3);

                             // Тест 2: Управление только GPU
                             printf("\n📋 TEST 2: GPU-only control\n");
                             read_fan_status(&cpu_duty, &cpu_rpm, &gpu_duty, &gpu_rpm);
                             printf("  Note: CPU will drop to %d%% when controlling GPU\n", MIN_SAFE_DUTY);

                             for (int i = 0; i < 4; i++) {
                                 set_fan_gpu(test_values[i]);

                                 for (int s = 1; s <= 3; s++) {
                                     sleep(1);
                                     read_temperatures(&cpu_temp, &ec_gpu_temp, &ambient);
                                     read_fan_status(&cpu_duty, &cpu_rpm, &gpu_duty, &gpu_rpm);
                                     nvidia_update();

                                     print_status("GPU", s, cpu_duty, cpu_rpm, cpu_temp,
                                                  gpu_duty, gpu_rpm, ec_gpu_temp, nvidia.temp, ambient);

                                     if (safety_check(cpu_temp, ec_gpu_temp, cpu_duty, gpu_duty) > 0) {
                                         printf("⚠️ EMERGENCY: Restoring safe values!\n");
                                         set_both_fans(MIN_SAFE_DUTY);
                                         return;
                                     }
                                 }
                             }

                             // Возврат в AUTO
                             set_auto_mode();
                             sleep(3);

                             // Тест 3: Синхронное управление
                             printf("\n📋 TEST 3: Synchronous control (both fans)\n");
                             for (int i = 0; i < 4; i++) {
                                 set_both_fans(test_values[i]);

                                 for (int s = 1; s <= 3; s++) {
                                     sleep(1);
                                     read_temperatures(&cpu_temp, &ec_gpu_temp, &ambient);
                                     read_fan_status(&cpu_duty, &cpu_rpm, &gpu_duty, &gpu_rpm);
                                     nvidia_update();

                                     print_status("BOTH", s, cpu_duty, cpu_rpm, cpu_temp,
                                                  gpu_duty, gpu_rpm, ec_gpu_temp, nvidia.temp, ambient);

                                     if (safety_check(cpu_temp, ec_gpu_temp, cpu_duty, gpu_duty) > 0) {
                                         printf("⚠️ EMERGENCY: Restoring safe values!\n");
                                         set_both_fans(MIN_SAFE_DUTY);
                                         return;
                                     }
                                 }
                             }

                             // Восстановление исходных значений
                             printf("\n📋 Restoring original values...\n");
                             set_both_fans(orig_cpu_duty);
                             sleep(2);

                             // Финальное состояние
                             read_temperatures(&cpu_temp, &ec_gpu_temp, &ambient);
                             read_fan_status(&cpu_duty, &cpu_rpm, &gpu_duty, &gpu_rpm);
                             nvidia_update();

                             printf("\n📊 Final state:\n");
                             printf("  CPU fan: %d%% (was %d%%), GPU fan: %d%% (was %d%%)\n",
                                    cpu_duty, orig_cpu_duty, gpu_duty, orig_gpu_duty);
                             printf("  CPU temp: %d°C, GPU temp (EC): %d°C", cpu_temp, ec_gpu_temp);
                             if (nvidia.available) {
                                 printf(", GPU temp (NVIDIA): %d°C", nvidia.temp);
                             }
                             printf("\n");
                         }

                         //============================================================================
                         // Главная функция
                         //============================================================================
                         int main() {
                             printf("╔══════════════════════════════════════════════════════════╗\n");
                             printf("║     Clevo N960KPx Fan Control Test v2 (with NVIDIA)     ║\n");
                             printf("╚══════════════════════════════════════════════════════════╝\n\n");

                             // Получаем права на порты
                             if (ioperm(EC_DATA, 1, 1) != 0 || ioperm(EC_SC, 1, 1) != 0) {
                                 printf("❌ ERROR: Need root privileges. Run with sudo.\n");
                                 return 1;
                             }

                             printf("✅ EC I/O permissions granted\n");

                             // Инициализация NVIDIA
                             nvidia_init();

                             // Проверка наличия второго вентилятора
                             uint8_t test = ec_io_read(EC_REG_FAN_GPU_DUTY);
                             if (test > 0) {
                                 printf("✅ GPU fan register 0xCF = %d (0x%02X)\n", test, test);
                             } else {
                                 printf("⚠️ GPU fan register 0xCF = 0, but may be normal\n");
                             }

                             printf("\nPress Ctrl+C to stop\n");
                             printf("Starting test cycles in 3 seconds...\n");
                             sleep(3);

                             int cycle = 1;
                             while (1) {
                                 run_test_cycle(cycle++);

                                 printf("\n⏳ Cooling down for 10 seconds...\n");
                                 for (int i = 10; i > 0; i--) {
                                     printf("%d... ", i);
                                     fflush(stdout);
                                     sleep(1);
                                 }
                                 printf("\n");
                             }

                             nvidia_cleanup();
                             return 0;
                         }
