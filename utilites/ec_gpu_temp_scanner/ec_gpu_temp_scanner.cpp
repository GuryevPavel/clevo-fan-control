#include <stdio.h>
#include <stdlib.h>
#include <sys/io.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>

#define EC_SC 0x66
#define EC_DATA 0x62
#define EC_SC_READ_CMD 0x80

// Известные регистры
#define EC_REG_CPU_TEMP 0x07
#define EC_REG_AMBIENT_TEMP 0xCA
#define EC_REG_FAN_CPU_DUTY 0xCE
#define EC_REG_FAN_GPU_DUTY 0xCF

// Диапазоны для сканирования
#define SCAN_START 0x00
#define SCAN_END 0xFF
#define SCAN_STEP 1

// Количество циклов
#define NUM_CYCLES 3

// Структура для хранения результатов
struct ScanResult {
    int reg;
    int values[NUM_CYCLES];
    int matches;
    float avg_diff;
    int min_val;
    int max_val;
};

// Структура для NVIDIA
static struct {
    int available;
    int temps[NUM_CYCLES];
    char driver_version[64];
    void *handle;  // Добавляем handle для библиотеки
} nvidia = {0, {0}, "", NULL};

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

//============================================================================
// Функции NVIDIA (ИСПРАВЛЕННЫЕ)
//============================================================================
static void nvidia_init() {
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
            printf("✅ NVIDIA NVML library found: %s\n", lib_paths[i]);
            break;
        }
    }

    if (!nvidia.handle) {
        printf("❌ NVIDIA NVML library not found\n");
        return;
    }

    typedef int (*nvmlInit_t)(void);
    typedef int (*nvmlShutdown_t)(void);
    typedef int (*nvmlDeviceGetHandleByIndex_t)(int, void**);
    typedef int (*nvmlDeviceGetTemperature_t)(void*, int, unsigned int*);
    typedef int (*nvmlSystemGetDriverVersion_t)(char*, int);

    nvmlInit_t nvmlInit = (nvmlInit_t)dlsym(nvidia.handle, "nvmlInit_v2");
    nvmlDeviceGetHandleByIndex_t nvmlDeviceGetHandleByIndex =
    (nvmlDeviceGetHandleByIndex_t)dlsym(nvidia.handle, "nvmlDeviceGetHandleByIndex_v2");
    nvmlDeviceGetTemperature_t nvmlDeviceGetTemperature =
    (nvmlDeviceGetTemperature_t)dlsym(nvidia.handle, "nvmlDeviceGetTemperature");
    nvmlSystemGetDriverVersion_t nvmlSystemGetDriverVersion =
    (nvmlSystemGetDriverVersion_t)dlsym(nvidia.handle, "nvmlSystemGetDriverVersion");
    nvmlShutdown_t nvmlShutdown = (nvmlShutdown_t)dlsym(nvidia.handle, "nvmlShutdown");

    if (!nvmlInit || !nvmlDeviceGetHandleByIndex || !nvmlDeviceGetTemperature || !nvmlShutdown) {
        printf("❌ Failed to get NVML functions\n");
        dlclose(nvidia.handle);
        nvidia.handle = NULL;
        return;
    }

    if (nvmlInit() != 0) {
        printf("❌ NVML init failed\n");
        dlclose(nvidia.handle);
        nvidia.handle = NULL;
        return;
    }

    if (nvmlSystemGetDriverVersion) {
        nvmlSystemGetDriverVersion(nvidia.driver_version, sizeof(nvidia.driver_version));
        printf("✅ NVIDIA driver version: %s\n", nvidia.driver_version);
    }

    void *device;
    if (nvmlDeviceGetHandleByIndex(0, &device) != 0) {
        printf("❌ No NVIDIA GPU found\n");
        nvmlShutdown();
        dlclose(nvidia.handle);
        nvidia.handle = NULL;
        return;
    }

    // Проверяем чтение температуры
    unsigned int temp = 0;
    if (nvmlDeviceGetTemperature(device, 0, &temp) == 0) {
        printf("✅ NVIDIA GPU detected, current temp: %d°C\n", (int)temp);
        nvidia.available = 1;
    }

    nvmlShutdown();
    // Не закрываем handle, оставляем для последующих чтений
}

static int nvidia_read_temp() {
    if (!nvidia.available || !nvidia.handle) return -1;

    typedef int (*nvmlInit_t)(void);
    typedef int (*nvmlShutdown_t)(void);
    typedef int (*nvmlDeviceGetHandleByIndex_t)(int, void**);
    typedef int (*nvmlDeviceGetTemperature_t)(void*, int, unsigned int*);

    nvmlInit_t nvmlInit = (nvmlInit_t)dlsym(nvidia.handle, "nvmlInit_v2");
    nvmlDeviceGetHandleByIndex_t nvmlDeviceGetHandleByIndex =
    (nvmlDeviceGetHandleByIndex_t)dlsym(nvidia.handle, "nvmlDeviceGetHandleByIndex_v2");
    nvmlDeviceGetTemperature_t nvmlDeviceGetTemperature =
    (nvmlDeviceGetTemperature_t)dlsym(nvidia.handle, "nvmlDeviceGetTemperature");
    nvmlShutdown_t nvmlShutdown = (nvmlShutdown_t)dlsym(nvidia.handle, "nvmlShutdown");

    if (!nvmlInit || !nvmlDeviceGetHandleByIndex || !nvmlDeviceGetTemperature || !nvmlShutdown) {
        return -1;
    }

    if (nvmlInit() != 0) return -1;

    void *device;
    int temp = -1;
    if (nvmlDeviceGetHandleByIndex(0, &device) == 0) {
        unsigned int t = 0;
        if (nvmlDeviceGetTemperature(device, 0, &t) == 0) {
            temp = (int)t;
        }
    }

    nvmlShutdown();
    return temp;
}

static void nvidia_cleanup() {
    if (nvidia.handle) {
        dlclose(nvidia.handle);
        nvidia.handle = NULL;
    }
}

//============================================================================
// Функции анализа
//============================================================================
static int is_valid_temperature(int val) {
    // Температура должна быть в разумных пределах (0-110°C)
    return (val >= 0 && val <= 110);
}

static int is_ambient_temperature(int val) {
    // Ambient обычно 20-40°C
    return (val >= 20 && val <= 40);
}

static int is_cpu_temperature(int val, int cpu_ref) {
    // CPU температура обычно близка к известной CPU temp
    return (abs(val - cpu_ref) <= 10);
}

static void print_scan_header() {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════════╗\n");
    printf("║                    EC GPU Temperature Scanner v3                    ║\n");
    printf("║               Scanning registers 0x00-0xFF for GPU temp             ║\n");
    printf("╚══════════════════════════════════════════════════════════════════════╝\n\n");
}

static void print_cycle_header(int cycle, int nvidia_temp) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

    printf("\n");
    printf("┌────────────────────────────────────────────────────────────────┐\n");
    printf("│ CYCLE %d │ NVIDIA GPU: %2d°C │ %s\n",
           cycle, nvidia_temp, time_str);
    printf("└────────────────────────────────────────────────────────────────┘\n");
    printf("Reg    Val  Valid  Type        Diff  Notes\n");
    printf("-----  ---  -----  ----------  ----  ------------------------------\n");
}

static void print_scan_result(int reg, int val, int nvidia_temp,
                              int cpu_temp, int ambient_temp) {
    char type[20] = "Unknown";
    char notes[50] = "";
    int diff = abs(val - nvidia_temp);

    // Определяем тип регистра
    if (reg == EC_REG_CPU_TEMP) {
        strcpy(type, "CPU TEMP");
        snprintf(notes, sizeof(notes), "Known CPU register");
    }
    else if (reg == EC_REG_AMBIENT_TEMP) {
        strcpy(type, "AMBIENT");
        snprintf(notes, sizeof(notes), "Known ambient register");
    }
    else if (is_valid_temperature(val)) {
        if (is_cpu_temperature(val, cpu_temp)) {
            strcpy(type, "CPU-like");
            snprintf(notes, sizeof(notes), "Matches CPU temp");
        }
        else if (is_ambient_temperature(val)) {
            strcpy(type, "AMBIENT-like");
            snprintf(notes, sizeof(notes), "Matches ambient");
        }
        else if (diff <= 3) {
            strcpy(type, "★ GPU CAND ★");
            snprintf(notes, sizeof(notes), "EXCELLENT match!");
        }
        else if (diff <= 5) {
            strcpy(type, "GPU CANDIDATE");
            snprintf(notes, sizeof(notes), "Good match");
        }
        else if (diff <= 10) {
            strcpy(type, "Possible GPU");
            snprintf(notes, sizeof(notes), "Close match");
        }
        else {
            strcpy(type, "Valid temp");
        }
    }

    printf("0x%02X   %3d   %s  %-10s  %3d   %s\n",
           reg, val,
           is_valid_temperature(val) ? "✓" : "✗",
           type,
           diff,
           notes);
                              }

                              static void print_final_results(ScanResult results[], int num_results,
                                                              int nvidia_temps[]) {
                                  printf("\n");
                                  printf("╔══════════════════════════════════════════════════════════════════════╗\n");
                                  printf("║                         FINAL RESULTS                                ║\n");
                                  printf("╚══════════════════════════════════════════════════════════════════════╝\n\n");

                                  printf("NVIDIA temperatures across cycles:\n");
                                  for (int c = 0; c < NUM_CYCLES; c++) {
                                      printf("  Cycle %d: %d°C\n", c+1, nvidia_temps[c]);
                                  }
                                  printf("\n");

                                  printf("Top candidates for GPU temperature register:\n");
                                  printf("Reg    Values                 AvgDiff  Range   Consistency\n");
                                  printf("-----  ---------------------  -------  ------  -----------\n");

                                  // Сортируем по качеству совпадения
                                  for (int i = 0; i < num_results; i++) {
                                      if (results[i].matches >= 2) {  // Минимум 2 совпадения из 3
                                          int range = results[i].max_val - results[i].min_val;
                                          const char* consistency =
                                          (range <= 2) ? "Excellent" :
                                          (range <= 5) ? "Good" :
                                          (range <= 10) ? "Fair" : "Poor";

                                          printf("0x%02X    %d/%d/%d            %5.1f    %2d-%2d   %s\n",
                                                 results[i].reg,
                                                 results[i].values[0],
                                                 results[i].values[1],
                                                 results[i].values[2],
                                                 results[i].avg_diff,
                                                 results[i].min_val,
                                                 results[i].max_val,
                                                 consistency);
                                      }
                                  }

                                  printf("\n");
                                  printf("Recommendations:\n");
                                  printf("  1. Check registers marked '★ GPU CAND ★' first\n");
                                  printf("  2. Verify with GPU under load (run stress test)\n");
                                  printf("  3. Most likely GPU temp register is one that:\n");
                                  printf("     - Shows valid temperature (0-110°C)\n");
                                  printf("     - Changes with GPU load\n");
                                  printf("     - Doesn't match CPU or ambient temps\n");
                                  printf("     - Consistent across multiple reads\n");
                                                              }

                                                              //============================================================================
                                                              // Основная функция
                                                              //============================================================================
                                                              int main() {
                                                                  print_scan_header();

                                                                  // Получаем права на порты
                                                                  if (ioperm(EC_DATA, 1, 1) != 0 || ioperm(EC_SC, 1, 1) != 0) {
                                                                      printf("❌ ERROR: Need root privileges. Run with sudo.\n");
                                                                      return 1;
                                                                  }
                                                                  printf("✅ EC I/O permissions granted\n");

                                                                  // Инициализация NVIDIA
                                                                  nvidia_init();
                                                                  if (!nvidia.available) {
                                                                      printf("❌ NVIDIA not available - cannot proceed\n");
                                                                      return 1;
                                                                  }

                                                                  // Массивы для хранения результатов
                                                                  ScanResult results[256];
                                                                  int num_results = 0;
                                                                  int nvidia_temps[NUM_CYCLES];

                                                                  // Выполняем 3 цикла сканирования
                                                                  for (int cycle = 0; cycle < NUM_CYCLES; cycle++) {
                                                                      // Читаем эталонную температуру NVIDIA
                                                                      int nvidia_temp = nvidia_read_temp();
                                                                      if (nvidia_temp < 0) {
                                                                          printf("❌ Failed to read NVIDIA temperature\n");
                                                                          nvidia_cleanup();
                                                                          return 1;
                                                                      }
                                                                      nvidia_temps[cycle] = nvidia_temp;

                                                                      // Читаем известные температуры для контекста
                                                                      int cpu_temp = ec_io_read(EC_REG_CPU_TEMP);
                                                                      int ambient_temp = ec_io_read(EC_REG_AMBIENT_TEMP);

                                                                      print_cycle_header(cycle + 1, nvidia_temp);

                                                                      // Сканируем все регистры
                                                                      for (int reg = 0; reg <= 0xFF; reg++) {
                                                                          int val = ec_io_read(reg);

                                                                          // Пропускаем регистры которые явно не температура
                                                                          if (reg == EC_REG_FAN_CPU_DUTY || reg == EC_REG_FAN_GPU_DUTY) {
                                                                              continue;
                                                                          }

                                                                          // Пропускаем регистры с очень большими значениями (>110)
                                                                          if (val > 110 && val < 140) {  // Могут быть Duty регистры
                                                                              continue;
                                                                          }

                                                                          print_scan_result(reg, val, nvidia_temp, cpu_temp, ambient_temp);

                                                                          // Сохраняем результаты для анализа
                                                                          int found = -1;
                                                                          for (int i = 0; i < num_results; i++) {
                                                                              if (results[i].reg == reg) {
                                                                                  found = i;
                                                                                  break;
                                                                              }
                                                                          }

                                                                          if (found == -1) {
                                                                              // Новый регистр
                                                                              ScanResult r;
                                                                              r.reg = reg;
                                                                              for (int c = 0; c < NUM_CYCLES; c++) r.values[c] = 0;
                                                                              r.values[cycle] = val;
                                                                              r.matches = 0;
                                                                              r.avg_diff = 0;
                                                                              r.min_val = val;
                                                                              r.max_val = val;
                                                                              results[num_results++] = r;
                                                                          } else {
                                                                              // Существующий регистр
                                                                              results[found].values[cycle] = val;
                                                                              if (val < results[found].min_val) results[found].min_val = val;
                                                                              if (val > results[found].max_val) results[found].max_val = val;
                                                                          }
                                                                      }

                                                                      if (cycle < NUM_CYCLES - 1) {
                                                                          printf("\nWaiting 3 seconds before next cycle...\n");
                                                                          sleep(3);
                                                                      }
                                                                  }

                                                                  // Анализируем результаты
                                                                  for (int i = 0; i < num_results; i++) {
                                                                      int matches = 0;
                                                                      float total_diff = 0;

                                                                      for (int c = 0; c < NUM_CYCLES; c++) {
                                                                          int val = results[i].values[c];
                                                                          int nvidia_temp = nvidia_temps[c];

                                                                          if (is_valid_temperature(val)) {
                                                                              int diff = abs(val - nvidia_temp);
                                                                              total_diff += diff;
                                                                              if (diff <= 5) matches++;
                                                                          }
                                                                      }

                                                                      results[i].matches = matches;
                                                                      if (matches > 0) {
                                                                          results[i].avg_diff = total_diff / NUM_CYCLES;
                                                                      } else {
                                                                          results[i].avg_diff = 999;
                                                                      }
                                                                  }

                                                                  // Выводим финальные результаты
                                                                  print_final_results(results, num_results, nvidia_temps);

                                                                  nvidia_cleanup();
                                                                  return 0;
                                                              }
