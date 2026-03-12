#!/usr/bin/env python3
"""
Скрипт для проверки и копирования созданного digital75 шрифта
"""

import os
import shutil
import struct

def check_font_structure(font_file):
    """Проверяет структуру TFT_eSPI шрифта"""
    
    print(f"=== ПРОВЕРКА СТРУКТУРЫ ШРИФТА {font_file} ===\n")
    
    if not os.path.exists(font_file):
        print(f"❌ Файл {font_file} не найден!")
        return False
    
    # Читаем hex данные из .h файла
    with open(font_file, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Извлекаем hex данные
    import re
    hex_matches = re.findall(r'0x([0-9A-Fa-f]{2})', content)
    data = bytes([int(match, 16) for match in hex_matches])
    
    print(f"Размер файла: {len(data):,} байт")
    
    if len(data) < 16:
        print("❌ Файл слишком мал для TFT_eSPI шрифта")
        return False
    
    # Читаем заголовок (big endian)
    header = struct.unpack('>IIII', data[:16])
    data_size = header[0]
    symbol_count = header[1]
    font_height = header[2]
    flags = header[3]
    
    print(f"Заголовок шрифта:")
    print(f"  Размер данных: {data_size:,} байт")
    print(f"  Количество символов: {symbol_count}")
    print(f"  Высота шрифта: {font_height} пикселей")
    print(f"  Флаги: {flags}")
    
    # Проверяем разумность значений
    if symbol_count > 1000 or font_height > 200:
        print("⚠️ Подозрительные значения в заголовке")
        return False
    
    if symbol_count != 12:
        print(f"⚠️ Ожидалось 12 символов (0-9, ., -), получено {symbol_count}")
    
    if font_height != 75:
        print(f"⚠️ Ожидалась высота 75 пикселей, получено {font_height}")
    
    # Анализируем таблицу символов
    print(f"\n=== АНАЛИЗ ТАБЛИЦЫ СИМВОЛОВ ===")
    symbol_table_start = 16
    
    for i in range(min(symbol_count, 15)):  # Показываем первые 15 символов
        offset = symbol_table_start + i * 16
        if offset + 16 <= len(data):
            symbol_info = struct.unpack('>IIII', data[offset:offset+16])
            char_code = symbol_info[0]
            width = symbol_info[1]
            height = symbol_info[2]
            x_offset = symbol_info[3]
            
            char_name = chr(char_code) if 32 <= char_code <= 126 else f"U+{char_code:04X}"
            print(f"  Символ {i}: код={char_code} ({char_name}), "
                  f"размер={width}x{height}, смещение={x_offset}")
    
    print("✅ Структура шрифта выглядит корректно!")
    return True

def copy_font_to_ts_project(font_file):
    """Копирует шрифт в проект TS"""
    
    print(f"\n=== КОПИРОВАНИЕ ШРИФТА В ПРОЕКТ TS ===")
    
    ts_src_dir = "../../../TS/src"
    
    if not os.path.exists(ts_src_dir):
        print(f"❌ Папка проекта TS не найдена: {ts_src_dir}")
        return False
    
    # Копируем файл
    destination = os.path.join(ts_src_dir, "digital75_new.h")
    try:
        shutil.copy2(font_file, destination)
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
    
    print("=== ПРОВЕРКА И КОПИРОВАНИЕ DIGITAL75 ШРИФТА ===\n")
    
    font_file = "FontFiles/digital75_test.h"
    
    # Проверяем структуру шрифта
    if check_font_structure(font_file):
        # Копируем в проект TS
        if copy_font_to_ts_project(font_file):
            print("\n✅ Шрифт успешно проверен и скопирован!")
            print("\n=== СЛЕДУЮЩИЕ ШАГИ ===")
            print("1. Перейдите в папку TS: cd ../../TS")
            print("2. Создайте RLE-сжатую версию: python create_simple_rle_font.py")
            print("3. Проверьте точность: python verify_rle_accuracy.py")
        else:
            print("\n❌ Ошибка копирования файла")
    else:
        print("\n❌ Структура шрифта некорректна")
        print("\n=== РЕКОМЕНДАЦИИ ===")
        print("1. Убедитесь, что Processing скетч выполнился без ошибок")
        print("2. Проверьте настройки в Create_font.pde")
        print("3. Попробуйте создать шрифт меньшего размера для тестирования")

if __name__ == "__main__":
    main()
