# Clevo Fan Control

Интеллектуальная система управления вентиляторами для ноутбуков Clevo (N960KPx / Hasee TX9-CA5DP)

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Version](https://img.shields.io/badge/version-1.0.2-green.svg)](https://github.com/GuryevPavel/clevo-fan-control.git)

## 🌟 Особенности

- **Интеллектуальное управление** - автоматическая регулировка скорости вентиляторов на основе температур CPU/GPU/NVMe
- **Раздельные кривые** - индивидуальные настройки для CPU и GPU
- **Защита NVMe дисков** - принудительное охлаждение при 60°C (90%) и 64°C (100%)
- **Приоритет NVML** - использование точных данных NVIDIA GPU при наличии драйвера
- **Фильтрация данных** - медианный фильтр для устранения выбросов
- **Автозапуск** - интеграция с systemd
- **XDG-совместимость** - конфиги и логи в стандартных директориях

## 📋 Требования

- Linux с поддержкой `ioperm` (доступ к портам 0x62/0x66)
- Права root (sudo)
- Опционально: NVIDIA драйвер для точного мониторинга GPU

## 🔧 Установка

### Быстрая установка

```bash
chmod +x install.sh
sudo ./install.sh
```

### Ручная установка из исходников

```bash
git clone https://github.com/GuryevPavel/clevo-fan-control.git
cd clevo-fan-control
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
sudo systemctl daemon-reload
sudo systemctl enable clevo-fan-control
sudo systemctl start clevo-fan-control
```

### Проверка статуса

```bash
sudo clevo-fan-control status
```

## 📖 Использование

```bash
sudo clevo-fan-control           # Нормальный запуск
sudo clevo-fan-control test      # С диагностическим тестом
sudo clevo-fan-control log       # С непрерывным логированием
sudo clevo-fan-control status    # Показать статус
sudo clevo-fan-control stop      # Остановить демон
sudo clevo-fan-control help      # Справка
```

## ⚙️ Конфигурация

### Файл конфигурации

После первого запуска создается файл:
```
~/.config/clevo-fan-control/fan_curve.conf
```

### Формат кривых

```ini
[CPU]
35 0      # ниже 35°C → 0%
40 10     # 35-40°C → 10%
42 20     # 40-42°C → 20%
45 30     # 42-45°C → 30%
47 40     # 45-47°C → 40%
50 50     # 47-50°C → 50%
55 60     # 50-55°C → 60%
60 70     # 55-60°C → 70%
62 80     # 60-62°C → 80%
65 90     # 62-65°C → 90%
100 100   # выше 65°C → 100%

[GPU]
35 0      # GPU кривая более щадящая (макс 90%)
40 5
42 10
45 20
47 30
50 40
55 50
60 60
62 70
65 80
100 90
```

## 🛡️ Защита NVMe

| Температура | Действие |
|-------------|----------|
| 55-60°C     | Буст скорости до +20% |
| ≥60°C       | Вентиляторы → 90% (приоритет над CPU/GPU) |
| ≥64°C       | Вентиляторы → 100% (критический режим) |

## 📊 Мониторинг

### Датчики температуры

- **CPU** - EC регистр 0x07
- **GPU** - NVML (NVIDIA) или EC регистр 0xFB
- **NVMe** - автоматическое определение через sysfs
- **Ambient** - усреднение доверенных датчиков системы

### Фильтрация

- Медианный фильтр (5 семплов) - удаление выбросов
- Анализ стабильности (20 семплов)
- Отбрасывание скачков >30°C

## 📝 Логирование

Логи сохраняются в:
```
~/.local/share/clevo-fan-control/logs/clevo_fan_control_YYYYMMDD_HHMMSS.log
```

Формат строки лога:
```
[HH:MM:SS] CPU:XX | GPU:XX | GPU_EC:XX | AMB:XX | NVMe:XX/XX | FAN_CPU:XXXX/XX% | FAN_GPU:XXXX/XX% | причина
```

## 🛠️ Вспомогательные утилиты (для разработки)

В директории `utilites/` находятся инструменты, использовавшиеся при разработке:

### Диагностические утилиты

| Файл | Назначение |
|------|------------|
| `ec_gpu_temp_scanner.cpp` | Сканирование EC регистров для поиска GPU температуры |
| `thermal_scanner.cpp` | Сканирование тепловых датчиков системы |
| `nvme_diag.cpp` | Диагностика NVMe дисков и поиск путей к температуре |
| `gpu_fan_test_fixed.cpp` | Тестирование управления GPU вентилятором |

### Документация

| Файл | Содержание |
|------|------------|
| `DEVELOPMENT_NOTES.md` | Заметки по разработке и найденные EC регистры |
| `EC_REGISTERS.txt` | Полная карта EC регистров для Clevo N960KPx |
| `QUICK_REFERENCE.txt` | Краткая справка по EC командам |

**Важно:** Эти утилиты не компилируются в основном проекте и предназначены только для отладки и исследования оборудования.

## 📚 Известные EC регистры

| Регистр | Назначение | Доступ |
|---------|------------|--------|
| 0x07 | CPU Temperature | RO |
| 0xCA | Ambient Temperature | RO |
| 0xFB | GPU Temperature (CORRECT!) | RO |
| 0xCE | CPU Fan Duty | RW |
| 0xCF | GPU Fan Duty | RW |
| 0xD0/D1 | CPU Fan RPM | RO |
| 0xD2/D3 | GPU Fan RPM | RO |

### EC Команды

| Команда | Порт | Назначение |
|---------|------|------------|
| 0x99 | 0x01 | Установка CPU вентилятора |
| 0x99 | 0x02 | Установка GPU вентилятора |
| 0x99 | 0xFF | Возврат в AUTO режим |

### Формула RPM

```
RPM = 2156220 / ((high_byte << 8) + low_byte)
```

## 🐛 Диагностика

### Проверка EC доступа

```bash
sudo clevo-fan-control status
```

### Диагностический тест вентиляторов

```bash
sudo clevo-fan-control test
```

### Просмотр логов в реальном времени

```bash
tail -f ~/.local/share/clevo-fan-control/logs/clevo_fan_control_*.log
```

## 🔧 Устранение неполадок

### Конфликт с tuxedo_io/clevo_acpi

Если модули `tuxedo_io` или `clevo_acpi` загружены, программа все равно работает через прямой доступ к портам. Конфликт не влияет на функциональность.

### NVMe диски не отображаются

Программа автоматически ищет пути к NVMe температурам при запуске. Если диски не найдены, используйте утилиту `nvme_diag` для диагностики:

```bash
cd utilites
g++ -o nvme_diag nvme_diag.cpp
sudo ./nvme_diag
```

### Задержки USB аудио

Оптимизированная версия не использует `popen()` в цикле, что исключает микро-задержки. Пути к NVMe файлам определяются один раз при старте.

## 📁 Структура проекта

```
clevo-fan-control/
├── src/
│   └── main.cpp                 # Основной исходный код
├── utilites/                    # Вспомогательные утилиты (для разработки)
│   ├── ec_gpu_temp_scanner.cpp
│   ├── thermal_scanner.cpp
│   ├── nvme_diag.cpp
│   ├── gpu_fan_test_fixed.cpp
│   ├── DEVELOPMENT_NOTES.md
│   ├── EC_REGISTERS.txt
│   └── QUICK_REFERENCE.txt
├── drivers/                     # Драйверы (опционально)
│   ├── Install_tuxedo_drivers.md
│   └── update-tuxedo-drivers.sh
├── CMakeLists.txt               # Конфигурация сборки
├── clevo-fan-control.service.in # systemd сервис
├── install.sh                   # Скрипт быстрой установки
└── README.md                    # Документация
```

## 🤝 Благодарности

- **Agramian** - оригинальный проект [clevo-fan-control](https://github.com/agramian/clevo-fan-control)
- **DeepSeek AI** - помощь в разработке и оптимизации
- Сообщество Clevo Linux - тестирование и обратная связь

## 📄 Лицензия

GPL v3. Подробности в файле LICENSE.

## 👤 Автор

Guryev Pavel (pilatnet@gmail.com)

---

**Версия 1.0.2** - Финальный релиз с полной поддержкой NVMe и оптимизацией производительности.