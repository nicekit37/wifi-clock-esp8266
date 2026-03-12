#!/usr/bin/env python3
"""
Скрипт для создания правильного digital75 шрифта из TTF файла
Использует Processing скетч Create_font.pde
"""

import os
import subprocess
import shutil
import time

def check_processing_installation():
    """Проверяет, установлен ли Processing"""
    
    print("=== ПРОВЕРКА УСТАНОВКИ PROCESSING ===")
    
    # Попробуем найти Processing в стандартных местах
    possible_paths = [
        r"C:\Program Files\Processing\processing.exe",
        r"C:\Program Files (x86)\Processing\processing.exe",
        r"C:\Users\{}\AppData\Local\Processing\processing.exe".format(os.getenv('USERNAME')),
        r"C:\Processing\processing.exe"
    ]
    
    for path in possible_paths:
        if os.path.exists(path):
            print(f"✅ Processing найден: {path}")
            return path
    
    print("❌ Processing не найден в стандартных местах")
    print("Пожалуйста, установите Processing с https://processing.org/")
    return None

def create_font_with_processing(processing_path):
    """Создает шрифт с помощью Processing"""
    
    print("\n=== СОЗДАНИЕ ШРИФТА С ПОМОЩЬЮ PROCESSING ===")
    
    # Путь к скетчу
    sketch_path = os.path.abspath("Create_font.pde")
    
    print(f"Скетч: {sketch_path}")
    print(f"Processing: {processing_path}")
    
    try:
        # Запускаем Processing скетч
        print("Запускаем Processing скетч...")
        process = subprocess.Popen([
            processing_path,
            "--sketch=" + os.path.dirname(sketch_path),
            "--run"
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        
        # Ждем завершения
        stdout, stderr = process.communicate(timeout=60)
        
        if process.returncode == 0:
            print("✅ Processing скетч выполнен успешно!")
            print("Вывод:", stdout)
            return True
        else:
            print("❌ Ошибка выполнения Processing скетча")
            print("Ошибка:", stderr)
            return False
            
    except subprocess.TimeoutExpired:
        print("❌ Таймаут выполнения Processing скетча")
        process.kill()
        return False
    except Exception as e:
        print(f"❌ Ошибка запуска Processing: {e}")
        return False

def check_generated_files():
    """Проверяет, какие файлы были созданы"""
    
    print("\n=== ПРОВЕРКА СОЗДАННЫХ ФАЙЛОВ ===")
    
    font_files_dir = "FontFiles"
    if not os.path.exists(font_files_dir):
        print("❌ Папка FontFiles не найдена")
        return False
    
    # Ищем файлы digital75
    digital75_files = []
    for file in os.listdir(font_files_dir):
        if "digital75" in file.lower():
            digital75_files.append(file)
    
    if digital75_files:
        print("✅ Найдены файлы digital75:")
        for file in digital75_files:
            file_path = os.path.join(font_files_dir, file)
            size = os.path.getsize(file_path)
            print(f"  {file} ({size:,} байт)")
        return True
    else:
        print("❌ Файлы digital75 не найдены")
        return False

def copy_font_to_ts_project():
    """Копирует созданный шрифт в проект TS"""
    
    print("\n=== КОПИРОВАНИЕ ШРИФТА В ПРОЕКТ TS ===")
    
    font_files_dir = "FontFiles"
    ts_src_dir = "../../TS/src"
    
    if not os.path.exists(ts_src_dir):
        print(f"❌ Папка проекта TS не найдена: {ts_src_dir}")
        return False
    
    # Ищем файл digital75.h
    digital75_h = None
    for file in os.listdir(font_files_dir):
        if file == "digital75.h":
            digital75_h = os.path.join(font_files_dir, file)
            break
    
    if not digital75_h:
        print("❌ Файл digital75.h не найден")
        return False
    
    # Копируем файл
    destination = os.path.join(ts_src_dir, "digital75_new.h")
    try:
        shutil.copy2(digital75_h, destination)
        print(f"✅ Файл скопирован: {destination}")
        
        # Показываем размер файла
        size = os.path.getsize(destination)
        print(f"Размер файла: {size:,} байт")
        
        return True
    except Exception as e:
        print(f"❌ Ошибка копирования: {e}")
        return False

def main():
    """Основная функция"""
    
    print("=== СОЗДАНИЕ ПРАВИЛЬНОГО DIGITAL75 ШРИФТА ===\n")
    
    # Проверяем установку Processing
    processing_path = check_processing_installation()
    if not processing_path:
        print("\n=== РУЧНОЕ СОЗДАНИЕ ШРИФТА ===")
        print("1. Откройте Processing IDE")
        print("2. Откройте файл Create_font.pde")
        print("3. Убедитесь, что настройки:")
        print("   - fontName = \"digital_7\"")
        print("   - fontSize = 75")
        print("   - Unicode блок: 0x0030, 0x0039 (цифры 0-9)")
        print("   - Дополнительные символы: 0x002E, 0x002D (. -)")
        print("4. Запустите скетч (Ctrl+R)")
        print("5. Проверьте папку FontFiles на наличие digital75.h")
        return
    
    # Создаем шрифт
    if create_font_with_processing(processing_path):
        # Проверяем созданные файлы
        if check_generated_files():
            # Копируем в проект TS
            copy_font_to_ts_project()
        else:
            print("❌ Файлы шрифта не были созданы")
    else:
        print("❌ Не удалось создать шрифт")

if __name__ == "__main__":
    main()
