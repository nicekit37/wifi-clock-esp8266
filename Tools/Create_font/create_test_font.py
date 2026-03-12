#!/usr/bin/env python3
"""
Скрипт для создания тестового шрифта digital75
Создает простой TFT_eSPI шрифт для тестирования RLE-прокси
"""

import os
import struct

def create_test_digital75_font():
    """Создает тестовый digital75 шрифт"""
    
    print("=== СОЗДАНИЕ ТЕСТОВОГО DIGITAL75 ШРИФТА ===\n")
    
    # Параметры шрифта
    font_height = 75
    symbol_count = 12  # 0-9, точка, минус
    char_width = 50
    char_height = 75
    
    # Создаем заголовок (16 байт)
    header = struct.pack('>IIII', 
        0,  # размер данных (заполним позже)
        symbol_count,  # количество символов
        font_height,   # высота шрифта
        0   # флаги
    )
    
    # Создаем таблицу символов (16 байт на символ)
    symbol_table = bytearray()
    data_offset = 16 + (symbol_count * 16)  # начало данных символов
    
    # Символы: 0-9, точка, минус
    symbols = [
        (0x30, '0'), (0x31, '1'), (0x32, '2'), (0x33, '3'), (0x34, '4'),
        (0x35, '5'), (0x36, '6'), (0x37, '7'), (0x38, '8'), (0x39, '9'),
        (0x2E, '.'), (0x2D, '-')
    ]
    
    for char_code, char_name in symbols:
        # Записываем информацию о символе
        symbol_info = struct.pack('>IIII',
            char_code,      # код символа
            char_width,     # ширина
            char_height,    # высота
            data_offset     # смещение данных
        )
        symbol_table.extend(symbol_info)
        data_offset += char_width * char_height
    
    # Создаем данные символов
    font_data = bytearray()
    
    for i, (char_code, char_name) in enumerate(symbols):
        print(f"Создание символа '{char_name}' (код {char_code})...")
        
        # Создаем простой паттерн для каждого символа
        char_data = create_char_pattern(char_name, char_width, char_height)
        font_data.extend(char_data)
    
    # Обновляем размер данных в заголовке
    data_size = len(font_data)
    header = struct.pack('>IIII', 
        data_size,     # размер данных
        symbol_count,  # количество символов
        font_height,   # высота шрифта
        0   # флаги
    )
    
    # Собираем полный файл
    full_font = header + symbol_table + font_data
    
    # Сохраняем файл
    output_file = "FontFiles/digital75_test.h"
    os.makedirs("FontFiles", exist_ok=True)
    
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write("#include <pgmspace.h>\n\n")
        f.write("const uint8_t digital75[] PROGMEM = {\n")
        
        # Записываем данные в hex формате
        for i in range(0, len(full_font), 16):
            chunk = full_font[i:i+16]
            hex_str = ', '.join(f'0x{b:02X}' for b in chunk)
            f.write(f"{hex_str},\n")
        
        f.write("};\n")
    
    print(f"\n✅ Тестовый шрифт создан: {output_file}")
    print(f"Размер файла: {len(full_font):,} байт")
    print(f"Количество символов: {symbol_count}")
    print(f"Высота шрифта: {font_height} пикселей")
    
    return output_file

def create_char_pattern(char_name, width, height):
    """Создает простой паттерн для символа"""
    
    data = bytearray()
    
    for y in range(height):
        for x in range(width):
            # Создаем простой паттерн в зависимости от символа
            if char_name == '0':
                # Рамка для цифры 0
                if x == 0 or x == width-1 or y == 0 or y == height-1:
                    data.append(0xFF)  # Белый
                else:
                    data.append(0x00)  # Черный
            elif char_name == '1':
                # Вертикальная линия для цифры 1
                if x == width // 2:
                    data.append(0xFF)  # Белый
                else:
                    data.append(0x00)  # Черный
            elif char_name == '.':
                # Точка в центре
                if x == width // 2 and y == height // 2:
                    data.append(0xFF)  # Белый
                else:
                    data.append(0x00)  # Черный
            elif char_name == '-':
                # Горизонтальная линия для минуса
                if y == height // 2:
                    data.append(0xFF)  # Белый
                else:
                    data.append(0x00)  # Черный
            else:
                # Для остальных цифр создаем простой паттерн
                if (x + y) % (ord(char_name) - ord('0') + 1) == 0:
                    data.append(0xFF)  # Белый
                else:
                    data.append(0x00)  # Черный
    
    return data

def main():
    """Основная функция"""
    
    # Создаем тестовый шрифт
    font_file = create_test_digital75_font()
    
    if font_file:
        print(f"\n=== СЛЕДУЮЩИЕ ШАГИ ===")
        print(f"1. Проверьте созданный файл: {font_file}")
        print("2. Скопируйте в проект TS: python check_and_copy_font.py")
        print("3. Создайте RLE-версию: cd ../../TS && python create_simple_rle_font.py")

if __name__ == "__main__":
    main()
