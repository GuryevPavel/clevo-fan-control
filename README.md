# Clevo Fan Control

## Описание

**Clevo Fan Control** — это демон для управления вентиляторами ноутбуков Clevo, обеспечивающий интеллектуальный контроль температуры процессора, графического процессора и накопителей.

## Возможности

* Интеллектуальное управление вентиляторами на основе температурных кривых
* Мониторинг температуры CPU, GPU, NVMe и окружающей среды
* Диагностический тест вентиляторов
* Логирование данных в реальном времени
* Фильтрация показаний температуры
* Автоматическая синхронизация вентиляторов

## Требования

* Права суперпользователя для доступа к EC-порту
* Linux-система с поддержкой XDG-путей
* NVIDIA драйвер (опционально для точного мониторинга GPU)

## Установка зависимостей


## Сборка и установка

### Клонирование репозитория
```bash
git clone https://github.com/GuryevPavel/clevo-fan-control.git
cd clevo-fan-control
```
### Сборка с помощью CMake
#### Очистка и создание директории для сборки
```bash
rm -rf build && mkdir build
```

#### Конфигурация (автоматически проверит и установит зависимости)
```bash
cmake -S . -B build
```

#### Сборка
```bash
cmake --build build -- -j$(nproc) \
&& sudo chown root ./build/clevo-fan-control \
&& sudo chmod u+s ./build/clevo-fan-control
```

### Сборка с помощью Make
```bash
mkdir build && cd build
cmake ..
make -j$(nproc) \
&& sudo chown root ./build/clevo-fan-control \
&& sudo chmod u+s ./build/clevo-fan-control
```

### Установка (запросит sudo для setuid root)
```bash
sudo make install
```

### Установка с помощью скрипта

#### 1. Делаем скрипт установки исполняемым
chmod +x install.sh

#### 2. Запускаем установку
./install.sh



## Использование

### Запускаем программу
```bash
sudo clevo-fan-control           # Нормальный запуск
sudo clevo-fan-control test      # С диагностическим тестом
sudo clevo-fan-control log       # С непрерывным логированием
sudo clevo-fan-control status    # Показать статус
sudo clevo-fan-control stop      # Остановить
sudo clevo-fan-control help      # Справка
```

#### Доступные опции

* Без опций — запуск в обычном режиме
* test — запуск диагностического теста вентиляторов
* status — отображение текущего статуса
* top — остановка демона
* log — запуск с логированием
* help — отображение справки

#### Конфигурация

* Расположение: ~/.config/clevo-fan-control/fan_curve.conf
* Формат:

```ini
[CPU]
35 0     # Температура 35°C → 0%
40 10    # Температура 40°C → 10%
...

[GPU]
35 0
40 5     # Более консервативная кривая для GPU
...
```

## Мониторинг

### Доступные команды

```bash
clevo-fan-control status # показать текущий статус
clevo-fan-control stop # остановить демон
clevo-fan-control test # запустить тест вентиляторов
```

## Примечания

* Для корректной работы требуется запуск от имени суперпользователя
* Демон автоматически создает логи в директории ~/.local/share/clevo-fan-control/logs
* Конфигурация сохраняется в ~/.config/clevo-fan-control

## ToDo

 - [] Добавить скрипт для полной автоматической установки
 - [] Устранить проблему с отображением температуры NVMe накопителей
 - [] Изменить кривую охлаждения GPU в шаблоне для более активного охлаждения GPU
      и NVMe накопителей

## Разработчики
============================================================================
 Name        : clevo-fan-control
 Author      : Guryev Pavel (pilatnet@gmail.com)
 Description : Clevo fan control daemon
============================================================================

 Based on original work by Agramian:
   https://github.com/agramian/clevo-fan-control

 Enhanced with intelligent fan control, diagnostic testing,
 and XDG-compliant paths.

 This utility was developed with assistance from DeepSeek AI.
 
 License: GPL v3
============================================================================

