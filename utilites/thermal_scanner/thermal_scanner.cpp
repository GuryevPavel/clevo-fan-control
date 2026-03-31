/*
 = *===========================================================================
 Name        : thermal_scanner.cpp
 Author      : Guryev Pavel (pilatnet@gmail.com)
 Version     : 1.3.0
 Description : Thermal scanner for Clevo laptops - быстрая версия (15 мин)
 Расчет Ambient температуры из доверенных сенсоров
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/io.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <math.h>
#include <signal.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>

//============================================================================
// Константы
//============================================================================
#define EC_SC 0x66
#define EC_DATA 0x62
#define EC_SC_READ_CMD 0x80
#define EC_REG_CPU_TEMP 0x07
#define EC_REG_FAN_CPU_DUTY 0xCE
#define EC_REG_FAN_GPU_DUTY 0xCF
#define EC_REG_FAN_CPU_RPMS_HI 0xD0
#define EC_REG_FAN_CPU_RPMS_LO 0xD1
#define EC_REG_FAN_GPU_RPMS_HI 0xD2
#define EC_REG_FAN_GPU_RPMS_LO 0xD3

#define MIN_FAN_DUTY 20
#define MAX_FAN_DUTY 100
#define MAX_SENSORS 32
#define HISTORY_SIZE 100

// Параметры тестирования
#define CALIBRATION_TIME 15    // 15 секунд калибровка
#define TEST_DURATION 120      // 2 минуты на тест
#define SAMPLE_INTERVAL 2      // Сэмпл каждые 2 секунды
#define COOLDOWN_TIME 30       // 30 секунд между тестами

// Пороги для валидации
#define TEMP_MIN_VALID 10      // Минимальная разумная температура
#define TEMP_MAX_VALID 90      // Максимальная разумная температура
#define TEMP_CONSISTENCY 5     // Максимальное отклонение от среднего для доверия

//============================================================================
// Структуры данных
//============================================================================
struct SensorInfo {
    char name[64];
    char path[256];
    char type[64];
    int current_temp;
    int trusted;                // Доверенный сенсор
    int calibration_readings[10];
    int cal_idx;
};

struct NvidiaInfo {
    int available;
    int temp;
    char driver_version[64];
    void *handle;
};

struct FanStatus {
    int cpu_duty;
    int cpu_rpm;
    int gpu_duty;
    int gpu_rpm;
};

struct TestPoint {
    int cpu_speed;
    int gpu_speed;
    const char* description;
};

//============================================================================
// Глобальные переменные
//============================================================================
static SensorInfo sensors[MAX_SENSORS];
static int sensor_count = 0;
static NvidiaInfo nvidia = {0};
static FanStatus fan_status = {0};
static int running = 1;
static FILE *log_file = NULL;
static int ambient_temp = 0;
static int trusted_sensors_count = 0;

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

static int ec_write_fan(int port, int percent) {
    int value = (percent * 255) / 100;
    if (ec_io_wait(EC_SC, 1, 0) != 0) return -1;
    outb(0x99, EC_SC);
    if (ec_io_wait(EC_SC, 1, 0) != 0) return -1;
    outb(port, EC_DATA);
    if (ec_io_wait(EC_SC, 1, 0) != 0) return -1;
    outb(value, EC_DATA);
    return ec_io_wait(EC_SC, 1, 0);
}

static int calculate_rpm(int hi, int lo) {
    int raw = (hi << 8) + lo;
    return raw > 0 ? (2156220 / raw) : 0;
}

static void read_fan_status(void) {
    fan_status.cpu_duty = (ec_io_read(EC_REG_FAN_CPU_DUTY) * 100) / 255;
    fan_status.cpu_rpm = calculate_rpm(
        ec_io_read(EC_REG_FAN_CPU_RPMS_HI),
                                       ec_io_read(EC_REG_FAN_CPU_RPMS_LO)
    );
    fan_status.gpu_duty = (ec_io_read(EC_REG_FAN_GPU_DUTY) * 100) / 255;
    fan_status.gpu_rpm = calculate_rpm(
        ec_io_read(EC_REG_FAN_GPU_RPMS_HI),
                                       ec_io_read(EC_REG_FAN_GPU_RPMS_LO)
    );
}

//============================================================================
// NVIDIA NVML функции
//============================================================================
static void nvidia_init(void) {
    const char* lib_paths[] = {
        "libnvidia-ml.so.1",
        "libnvidia-ml.so",
        "/usr/lib/x86_64-linux-gnu/libnvidia-ml.so.1",
        "/usr/lib/x86_64-linux-gnu/libnvidia-ml.so",
        NULL
    };

    for (int i = 0; lib_paths[i] != NULL; i++) {
        nvidia.handle = dlopen(lib_paths[i], RTLD_LAZY);
        if (nvidia.handle) {
            printf("  ✓ Found NVML library: %s\n", lib_paths[i]);
            break;
        }
    }

    if (!nvidia.handle) return;

    typedef int (*nvmlInit_t)(void);
    typedef int (*nvmlShutdown_t)(void);
    typedef int (*nvmlDeviceGetHandleByIndex_t)(int, void**);
    typedef int (*nvmlDeviceGetTemperature_t)(void*, int, unsigned int*);

    nvmlInit_t nvmlInit = (nvmlInit_t)dlsym(nvidia.handle, "nvmlInit_v2");
    nvmlDeviceGetHandleByIndex_t nvmlDeviceGetHandleByIndex =
    (nvmlDeviceGetHandleByIndex_t)dlsym(nvidia.handle, "nvmlDeviceGetHandleByIndex_v2");
    nvmlDeviceGetTemperature_t nvmlDeviceGetTemperature =
    (nvmlDeviceGetTemperature_t)dlsym(nvidia.handle, "nvmlDeviceGetTemperature");

    if (!nvmlInit || !nvmlDeviceGetHandleByIndex || !nvmlDeviceGetTemperature) {
        dlclose(nvidia.handle);
        return;
    }

    if (nvmlInit() != 0) {
        dlclose(nvidia.handle);
        return;
    }

    void *device;
    if (nvmlDeviceGetHandleByIndex(0, &device) == 0) {
        unsigned int temp = 0;
        if (nvmlDeviceGetTemperature(device, 0, &temp) == 0) {
            nvidia.temp = (int)temp;
            nvidia.available = 1;
            printf("  ✓ NVIDIA GPU detected: %d°C\n", nvidia.temp);
        }
    }
}

static void nvidia_update(void) {
    if (!nvidia.available || !nvidia.handle) return;

    typedef int (*nvmlInit_t)(void);
    typedef int (*nvmlDeviceGetHandleByIndex_t)(int, void**);
    typedef int (*nvmlDeviceGetTemperature_t)(void*, int, unsigned int*);

    nvmlInit_t nvmlInit = (nvmlInit_t)dlsym(nvidia.handle, "nvmlInit_v2");
    nvmlDeviceGetHandleByIndex_t nvmlDeviceGetHandleByIndex =
    (nvmlDeviceGetHandleByIndex_t)dlsym(nvidia.handle, "nvmlDeviceGetHandleByIndex_v2");
    nvmlDeviceGetTemperature_t nvmlDeviceGetTemperature =
    (nvmlDeviceGetTemperature_t)dlsym(nvidia.handle, "nvmlDeviceGetTemperature");

    if (!nvmlInit || !nvmlDeviceGetHandleByIndex || !nvmlDeviceGetTemperature) return;

    if (nvmlInit() != 0) return;

    void *device;
    if (nvmlDeviceGetHandleByIndex(0, &device) == 0) {
        unsigned int temp = 0;
        if (nvmlDeviceGetTemperature(device, 0, &temp) == 0) {
            nvidia.temp = (int)temp;
        }
    }
}

//============================================================================
// Сканирование сенсоров
//============================================================================
static void scan_sensors(void) {
    printf("\n🔍 Scanning temperature sensors...\n");

    // Сканируем thermal zones
    for (int zone = 0; zone < 30; zone++) {
        char path[256];
        snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp", zone);

        FILE *f = fopen(path, "r");
        if (!f) continue;

        int temp = 0;
        if (fscanf(f, "%d", &temp) != 1) {
            fclose(f);
            continue;
        }
        fclose(f);

        char type_path[256];
        snprintf(type_path, sizeof(type_path), "/sys/class/thermal/thermal_zone%d/type", zone);

        char type[64] = "unknown";
        FILE *tf = fopen(type_path, "r");
        if (tf) {
            if (fgets(type, sizeof(type), tf)) {
                type[strcspn(type, "\n")] = 0;
            }
            fclose(tf);
        }

        snprintf(sensors[sensor_count].name, sizeof(sensors[sensor_count].name),
                 "thermal_zone%d", zone);
        snprintf(sensors[sensor_count].type, sizeof(sensors[sensor_count].type),
                 "%s", type);
        snprintf(sensors[sensor_count].path, sizeof(sensors[sensor_count].path),
                 "%s", path);
        sensors[sensor_count].current_temp = temp / 1000;
        sensors[sensor_count].trusted = 0;
        sensors[sensor_count].cal_idx = 0;
        sensor_count++;
    }

    // Сканируем hwmon (только acpitz, iwlwifi и nvme Composite)
    for (int hwmon = 0; hwmon < 20; hwmon++) {
        char name_path[256];
        snprintf(name_path, sizeof(name_path), "/sys/class/hwmon/hwmon%d/name", hwmon);

        FILE *nf = fopen(name_path, "r");
        if (!nf) continue;

        char hwmon_name[64];
        if (!fgets(hwmon_name, sizeof(hwmon_name), nf)) {
            fclose(nf);
            continue;
        }
        hwmon_name[strcspn(hwmon_name, "\n")] = 0;
        fclose(nf);

        // Интересуемся только acpitz, iwlwifi и nvme
        if (strstr(hwmon_name, "acpitz") ||
            strstr(hwmon_name, "iwlwifi") ||
            strstr(hwmon_name, "nvme")) {

            // Берем только Composite сенсор (temp1)
            char temp_path[256];
        snprintf(temp_path, sizeof(temp_path),
                 "/sys/class/hwmon/hwmon%d/temp1_input", hwmon);

        FILE *tf = fopen(temp_path, "r");
        if (!tf) continue;

        int temp = 0;
            if (fscanf(tf, "%d", &temp) != 1) {
                fclose(tf);
                continue;
            }
            fclose(tf);

            snprintf(sensors[sensor_count].name, sizeof(sensors[sensor_count].name),
                     "hwmon%d", hwmon);
            snprintf(sensors[sensor_count].type, sizeof(sensors[sensor_count].type),
                     "%s", hwmon_name);
            snprintf(sensors[sensor_count].path, sizeof(sensors[sensor_count].path),
                     "%s", temp_path);
            sensors[sensor_count].current_temp = temp / 1000;
            sensors[sensor_count].trusted = 0;
            sensors[sensor_count].cal_idx = 0;
            sensor_count++;
            }
    }

    printf("  Found %d sensors\n", sensor_count);
}

//============================================================================
// Калибровка - определение доверенных сенсоров
//============================================================================
static void calibration_phase(void) {
    printf("\n🔧 Calibration phase (%d seconds)...\n", CALIBRATION_TIME);
    printf("  Setting fans to 30%% for stable readings\n");

    // Устанавливаем вентиляторы на 30% для стабильности
    ec_write_fan(0x01, 30);
    ec_write_fan(0x02, 30);

    printf("  Reading sensors");
    fflush(stdout);

    // Собираем калибровочные данные
    for (int sec = 0; sec < CALIBRATION_TIME; sec++) {
        for (int i = 0; i < sensor_count; i++) {
            FILE *f = fopen(sensors[i].path, "r");
            if (f) {
                int raw = 0;
                if (fscanf(f, "%d", &raw) == 1) {
                    int temp = raw / 1000;
                    if (sensors[i].cal_idx < 10) {
                        sensors[i].calibration_readings[sensors[i].cal_idx++] = temp;
                    }
                }
                fclose(f);
            }
        }
        printf(".");
        fflush(stdout);
        sleep(1);
    }
    printf(" done.\n");

    // Анализ калибровочных данных
    printf("\n  Analyzing sensor reliability:\n");
    trusted_sensors_count = 0;

    for (int i = 0; i < sensor_count; i++) {
        if (sensors[i].cal_idx == 0) continue;

        // Вычисляем среднее и стабильность
        int sum = 0;
        int min = 999, max = 0;
        for (int j = 0; j < sensors[i].cal_idx; j++) {
            int t = sensors[i].calibration_readings[j];
            sum += t;
            if (t < min) min = t;
            if (t > max) max = t;
        }
        int avg = sum / sensors[i].cal_idx;
        int stability = max - min;

        // Проверяем на достоверность
        if (avg >= TEMP_MIN_VALID && avg <= TEMP_MAX_VALID && stability <= TEMP_CONSISTENCY) {
            sensors[i].trusted = 1;
            trusted_sensors_count++;
            printf("    ✓ %s: avg=%d°C, stable=%d°C - TRUSTED\n",
                   sensors[i].type, avg, stability);
        } else {
            printf("    ✗ %s: avg=%d°C, stable=%d°C - UNRELIABLE\n",
                   sensors[i].type, avg, stability);
        }
    }

    printf("  Trusted sensors: %d/%d\n", trusted_sensors_count, sensor_count);
}

//============================================================================
// Вычисление Ambient температуры
//============================================================================
static int calculate_ambient_temp(void) {
    int sum = 0;
    int count = 0;

    // Используем только доверенные сенсоры
    for (int i = 0; i < sensor_count; i++) {
        if (sensors[i].trusted) {
            sum += sensors[i].current_temp;
            count++;
        }
    }

    if (count > 0) {
        return sum / count;
    }
    return 0;  // Если нет доверенных сенсоров
}

//============================================================================
// Обновление всех сенсоров
//============================================================================
static void update_all_sensors(void) {
    // Обновляем CPU
    int cpu_temp = ec_io_read(EC_REG_CPU_TEMP);

    // Обновляем NVIDIA
    nvidia_update();

    // Обновляем вентиляторы
    read_fan_status();

    // Обновляем все сенсоры
    for (int i = 0; i < sensor_count; i++) {
        FILE *f = fopen(sensors[i].path, "r");
        if (f) {
            int raw = 0;
            if (fscanf(f, "%d", &raw) == 1) {
                sensors[i].current_temp = raw / 1000;
            }
            fclose(f);
        }
    }

    // Пересчитываем Ambient
    ambient_temp = calculate_ambient_temp();
}

//============================================================================
// Проведение теста
//============================================================================
static void run_test(const TestPoint *test, int test_num, int total_tests) {
    printf("\n%s\n", test->description);
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║ TEST %d/%d: CPU=%d%%, GPU=%d%%\n",
           test_num, total_tests, test->cpu_speed, test->gpu_speed);
    printf("║ Duration: %d seconds\n", TEST_DURATION);
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    // Устанавливаем скорости
    if (test->cpu_speed >= 0) {
        ec_write_fan(0x01, test->cpu_speed);
    }
    if (test->gpu_speed >= 0) {
        if (test->cpu_speed < MIN_FAN_DUTY && test->gpu_speed > 0) {
            ec_write_fan(0x01, MIN_FAN_DUTY);
        }
        ec_write_fan(0x02, test->gpu_speed);
    }

    // Заголовок таблицы
    printf("\nTime  CPU°C GPU°C AMB°C CPU%% GPU%%  Notes\n");
    printf("----- ----- ----- ----- ---- ----  -----\n");

    // Сбор данных
    int samples = TEST_DURATION / SAMPLE_INTERVAL;
    for (int i = 0; i < samples && running; i++) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);

        update_all_sensors();

        // Показываем каждую 10-ю точку
        if (i % 10 == 0) {
            printf("%02d:%02d  %3d°C  %3d°C  %3d°C  %3d%%  %3d%%  %s\n",
                   tm_info->tm_min, tm_info->tm_sec,
                   ec_io_read(EC_REG_CPU_TEMP), nvidia.temp, ambient_temp,
                   fan_status.cpu_duty, fan_status.gpu_duty,
                   test->description);
        }

        // Запись в лог
        if (log_file) {
            fprintf(log_file, "%ld, %d, %d, %d, %d, %d, %d, %d\n",
                    now,
                    ec_io_read(EC_REG_CPU_TEMP),
                    nvidia.temp,
                    ambient_temp,
                    fan_status.cpu_duty,
                    fan_status.cpu_rpm,
                    fan_status.gpu_duty,
                    fan_status.gpu_rpm);
            fflush(log_file);
        }

        sleep(SAMPLE_INTERVAL);
    }

    printf("\n✅ Test %d complete.\n", test_num);
}

//============================================================================
// Охлаждение между тестами
//============================================================================
static void cooldown(int test_num) {
    printf("\n🌡️  Cooldown (%d seconds)...\n", COOLDOWN_TIME);
    printf("  Returning to AUTO mode...\n");

    ec_write_fan(0x01, 255);
    ec_write_fan(0x02, 255);

    for (int i = 0; i < COOLDOWN_TIME; i += 5) {
        if (!running) break;
        printf(".");
        fflush(stdout);
        sleep(5);
    }
    printf(" done.\n\n");
}

//============================================================================
// Сохранение результатов
//============================================================================
static void save_results(void) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char filename[64];
    strftime(filename, sizeof(filename), "thermal_scan_%Y%m%d_%H%M%S.log", tm_info);

    log_file = fopen(filename, "w");
    if (!log_file) return;

    fprintf(log_file, "Clevo Thermal Scanner Results\n");
    fprintf(log_file, "==============================\n");
    fprintf(log_file, "Date: %s", ctime(&now));
    fprintf(log_file, "Trusted sensors: %d/%d\n\n", trusted_sensors_count, sensor_count);

    fprintf(log_file, "Timestamp, CPU_Temp, GPU_Temp, Ambient_Temp, CPU_Duty, CPU_RPM, GPU_Duty, GPU_RPM\n");

    printf("\n💾 Results will be saved to %s\n", filename);
}

//============================================================================
// Обработчик сигналов
//============================================================================
static void signal_handler(int signum) {
    printf("\n⚠️ Interrupted, stopping...\n");
    running = 0;
}

//============================================================================
// Главная функция
//============================================================================
int main() {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║           Clevo Thermal Scanner v1.3                      ║\n");
    printf("║     Быстрое тестирование (15 минут)                       ║\n");
    printf("║     Ambient температура из доверенных сенсоров           ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    // Получаем права на порты
    if (ioperm(EC_DATA, 1, 1) != 0 || ioperm(EC_SC, 1, 1) != 0) {
        printf("❌ Need root privileges. Run with sudo.\n");
        return 1;
    }
    printf("✓ EC I/O permissions granted\n");

    signal(SIGINT, signal_handler);

    // Инициализация NVIDIA
    printf("\n🔍 Initializing NVIDIA...\n");
    nvidia_init();

    // Сканируем сенсоры
    scan_sensors();

    // Калибровка
    calibration_phase();

    // Сохраняем результаты
    save_results();

    // Тестовые точки (сокращенные)
    TestPoint tests[] = {
        { -1,  -1, "AUTO mode" },
        { 20,  -1, "CPU 20%" },
        { 100, -1, "CPU 100%" },
        { -1,  20, "GPU 20%" },
        { -1, 100, "GPU 100%" },
        { 20,  20, "Both 20%" },
        { 100, 100, "Both 100%" }
    };

    int total_tests = sizeof(tests) / sizeof(tests[0]);

    printf("\n🔬 Starting tests (%d tests)...\n", total_tests);
    printf("   Each test: %d seconds\n", TEST_DURATION);
    printf("   Cooldown: %d seconds\n", COOLDOWN_TIME);
    printf("   Total time: ~%d minutes\n",
           (total_tests * (TEST_DURATION + COOLDOWN_TIME) + CALIBRATION_TIME) / 60);
    sleep(2);

    // Запускаем тесты
    for (int t = 0; t < total_tests && running; t++) {
        run_test(&tests[t], t+1, total_tests);

        if (t < total_tests - 1 && running) {
            cooldown(t+1);
        }
    }

    // Возвращаем в AUTO режим
    printf("\n🔄 Returning to AUTO mode...\n");
    ec_write_fan(0x01, 255);
    ec_write_fan(0x02, 255);

    if (log_file) {
        fclose(log_file);
    }

    printf("\n✅ Thermal scanning complete!\n");
    printf("   Ambient temperature calculation based on %d trusted sensors\n",
           trusted_sensors_count);

    return 0;
}
