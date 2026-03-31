/*
============================================================================
 Name        : nvme_diag.cpp
 Description : Диагностика NVMe дисков для Clevo fan control
============================================================================
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <glob.h>
#include <unistd.h>
#include <sys/stat.h>

int main() {
    printf("\n╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║              NVMe Diagnostic Tool v1.0                          ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n\n");
    
    // Проверяем наличие NVMe устройств в системе
    printf("📁 Checking NVMe devices in /sys/class/nvme/:\n");
    DIR *dir = opendir("/sys/class/nvme");
    if (dir) {
        struct dirent *entry;
        int nvme_count = 0;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == 'n' && entry->d_name[1] == 'v') {
                printf("  Found: %s\n", entry->d_name);
                nvme_count++;
            }
        }
        closedir(dir);
        if (nvme_count == 0) {
            printf("  No NVMe devices found in /sys/class/nvme/\n");
        }
    } else {
        printf("  Cannot open /sys/class/nvme/\n");
    }
    
    printf("\n🔍 Searching for NVMe temperature files:\n");
    
    // Используем glob для поиска всех возможных путей
    const char* patterns[] = {
        "/sys/class/nvme/nvme*/device/temp*_input",
        "/sys/class/nvme/nvme*/temperature",
        "/sys/class/nvme/nvme*/hwmon/hwmon*/temp*_input",
        "/sys/class/nvme/nvme*/nvme*/temp*_input",
        "/sys/devices/pci*/nvme/nvme*/temperature",
        "/sys/devices/pci*/nvme/nvme*/hwmon/hwmon*/temp*_input",
        NULL
    };
    
    for (int p = 0; patterns[p] != NULL; p++) {
        glob_t glob_result;
        memset(&glob_result, 0, sizeof(glob_result));
        
        int ret = glob(patterns[p], 0, NULL, &glob_result);
        if (ret == 0) {
            for (size_t i = 0; i < glob_result.gl_pathc; i++) {
                printf("  %s\n", glob_result.gl_pathv[i]);
                
                // Читаем значение
                FILE *f = fopen(glob_result.gl_pathv[i], "r");
                if (f) {
                    int val;
                    if (fscanf(f, "%d", &val) == 1) {
                        int temp = (val > 1000) ? val / 1000 : val;
                        printf("    Value: %d (%d°C)\n", val, temp);
                    } else {
                        printf("    Cannot read value\n");
                    }
                    fclose(f);
                }
            }
        }
        globfree(&glob_result);
    }
    
    printf("\n🔍 Searching for NVMe temperature files using find (system command):\n");
    system("find /sys -name \"temp*_input\" -path \"*nvme*\" 2>/dev/null | head -20");
    
    printf("\n📊 Trying to read temperatures from known paths:\n");
    
    // Прямая проверка для nvme0
    const char* test_paths[] = {
        "/sys/class/nvme/nvme0/device/temp1_input",
        "/sys/class/nvme/nvme0/temperature",
        "/sys/class/nvme/nvme0/hwmon/hwmon0/temp1_input",
        "/sys/class/nvme/nvme0/hwmon/hwmon1/temp1_input",
        "/sys/class/nvme/nvme1/device/temp1_input",
        "/sys/class/nvme/nvme1/temperature",
        "/sys/class/nvme/nvme1/hwmon/hwmon0/temp1_input",
        "/sys/class/nvme/nvme1/hwmon/hwmon1/temp1_input",
        NULL
    };
    
    for (int i = 0; test_paths[i] != NULL; i++) {
        FILE *f = fopen(test_paths[i], "r");
        if (f) {
            int val;
            if (fscanf(f, "%d", &val) == 1) {
                int temp = (val > 1000) ? val / 1000 : val;
                printf("  %s = %d°C\n", test_paths[i], temp);
            } else {
                printf("  %s exists but cannot read\n", test_paths[i]);
            }
            fclose(f);
        }
    }
    
    printf("\n📊 Trying to read temperatures using smartctl (if available):\n");
    system("which smartctl > /dev/null 2>&1 && sudo smartctl -A /dev/nvme0 | grep -i temperature || echo 'smartctl not installed'");
    system("which smartctl > /dev/null 2>&1 && sudo smartctl -A /dev/nvme1 | grep -i temperature || echo 'smartctl not installed'");
    
    printf("\n💡 Recommendations:\n");
    printf("  If temperature files exist but show 0, you may need to:\n");
    printf("  1. Load the nvme module: sudo modprobe nvme\n");
    printf("  2. Check if drives are mounted: lsblk\n");
    printf("  3. Try reading with: cat /sys/class/nvme/nvme0/device/temp1_input\n");
    printf("  4. Install smartctl for alternative reading: sudo apt install smartmontools\n");
    
    return 0;
}