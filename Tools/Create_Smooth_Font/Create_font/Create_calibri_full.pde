// Скрипт для создания шрифта Roboto_Regular с оптимальным набором символов
// Поддерживает: английский (латиница), русский (кириллица), украинский (кириллица + специфические символы)
// Настройки: размер настраиваемый, только используемые в программе символы

import java.awt.Desktop;

// Настройки шрифта
String fontName = "Robot";
String fontType = ".ttf";
int fontSize = 35;  // Размер шрифта (изменить по необходимости: 10, 12, 15, 20, 40 и т.д.)
int displayFontSize = 35;  // Размер для предварительного просмотра
boolean createHeaderFile = true;
boolean openFolder = false;  // Отключено для избежания ошибок

// Символы для включения в шрифт (только используемые с AA_FONT_CALIBRI20)
static final int[] unicodeBlocks = {
  // Цифры
  0x0030, 0x0039, // Цифры 0-9
};

static final int[] specificUnicodes = {
  // Специальные знаки (используемые с calibri20)

  0x00B0,  // ° градус Цельсия
  0x002B,  // + плюс
  0x002D,  // - минус
  0x0025,  // % процент
  0x0043,  // C латинская буква C (для °C)
  
};

PFont myFont;
PrintWriter logOutput;

void setup() {
  logOutput = createWriter("FontFiles/System_Font_List.txt"); 
  size(1000, 800);

  // Список доступных шрифтов
  String[] fontList = PFont.list();
  printArray(fontList);

  // Сохранение списка шрифтов
  for (int x = 0; x < fontList.length; x++) {
    logOutput.print("[" + x + "] ");
    logOutput.println(fontList[x]);
  }
  logOutput.flush();
  logOutput.close();

  // Подготовка символов
  char[] charset;
  int index = 0, count = 0;

  int blockCount = unicodeBlocks.length;

  for (int i = 0; i < blockCount; i+=2) {
    int firstUnicode = unicodeBlocks[i];
    int lastUnicode = unicodeBlocks[i+1];
    if (lastUnicode < firstUnicode) {
      delay(100);
      System.err.println("ERROR: Bad Unicode range specified, last < first!");
      System.err.print("first in range = 0x" + hex(firstUnicode, 4));
      System.err.println(", last in range  = 0x" + hex(lastUnicode, 4));
      while (true);
    }
    count += (lastUnicode - firstUnicode + 1);
  }

  count += specificUnicodes.length;

  println();
  println("=====================");
  println("Creating Roboto_Regular" + fontSize + " font file with OPTIMAL charset...");
  println("Unicode blocks included     = " + (blockCount/2));
  println("Specific unicodes included  = " + specificUnicodes.length);
  println("Total number of characters  = " + count);
  println();
  println("Supported texts with calibri20:");
  println("  - \"АВАРIЯ!\" - А, В, Р, І, Я, !");
  println("  - \"Датчик\" - Д, а, т, ч, и, к");
  println("  - \"відсутній!\" - в, і, д, с, у, т, н, й, !");
  println("  - \"Оновлення\" - О, н, о, в, л, е, н, н, я");
  println("  - Дни недели: Пн, Вт, Ср, Чт, Пт, Сб, Вс");
  println("  - Описания погоды: Ясно, Малооблачно, Облачно, Дождь, Ливень, Гроза, Снег, Туман");
  println("  - Влажность: цифры 0-9, %");
  println("  - Коррекция: цифры 0-9, +, -, .");
  println("  - Время: цифры 0-9, :");
  println("  - Uptime: цифры 0-9, :, d, w, +");
  println();
  println("Included symbols:");
  println("  - Numbers: 0-9");
  println("  - Signs: пробел, !, %, +, -, ., :, ° (градус), C (для °C)");
  println("  - Latin: d, w (для uptime), C (для °C)");
  println("  - Cyrillic: А, В, Р, І, Я, Д, О, а, т, ч, и, к, в, і, д, с, у, н, й, о, л, е, я");
  println("  - Дни недели: П, н, В, т, С, р, Ч, б, с");
  println("  - Описания погоды: Я, а, л, о, М, к, в, ь, О, ч, Д, ж, д, Л, и, е, Г, з, г, С, Т, у, м");

  if (count == 0) {
    delay(100);
    System.err.println("ERROR: No Unicode range or specific codes have been defined!");
    while (true);
  }

  // Выделение памяти
  charset = new char[count];

  for (int i = 0; i < blockCount; i+=2) {
    int firstUnicode = unicodeBlocks[i];
    int lastUnicode = unicodeBlocks[i+1];

    for (int code = firstUnicode; code <= lastUnicode; code++) {
      charset[index] = Character.toChars(code)[0];
      index++;
    }
  }

  // Загрузка специфических символов
  for (int i = 0; i < specificUnicodes.length; i++) {
    charset[index] = Character.toChars(specificUnicodes[i])[0];
    index++;
  }

  // Создание шрифта
  boolean smooth = true;
  myFont = createFont(fontName+fontType, displayFontSize, smooth, charset);

  // Отображение символов в окне
  fill(0, 0, 0);
  textFont(myFont);

  int margin = displayFontSize;
  translate(margin/2, margin);

  int gapx = displayFontSize*10/8;
  int gapy = displayFontSize*10/8;
  index = 0;
  fill(0);

  textSize(displayFontSize);

  for (int y = 0; y < height-gapy; y += gapy) {
    int x = 0;
    while (x < width) {
      int unicode = charset[index];
      float cwidth = textWidth((char)unicode) + 2;
      if ( (x + cwidth) > (width - gapx) ) break;

      text(new String(Character.toChars(unicode)), x, y);
      x += cwidth;
      index++;
      if (index >= count) break;
    }
    if (index >= count) break;
  }

  // Создание шрифта для сохранения
  PFont font = createFont(fontName+fontType, fontSize, smooth, charset);

  println("Created font " + fontName + str(fontSize) + ".vlw");

  // Исправляем имя файла (убираем дефис из имени для файла)
  String fontFileName = "FontFiles/Robot" + str(fontSize) + ".vlw";
  String headerFilePath = "g:\\proshifki\\2025\\WiFi Clock\\src\\";
  String headerFileName = "Robot" + str(fontSize) + ".h";

  // Сохранение файла
  try {
    print("Saving to sketch FontFiles folder... ");

    OutputStream output = createOutput(fontFileName);
    font.save(output);
    output.close();

    println("OK!");
    delay(100);

    System.err.println("All done! Note: Rectangles are displayed for non-existant characters.");
  }
  catch(IOException e) {
    println("Doh! Failed to create the file");
  }

  // Создание заголовочного файла
  if(!createHeaderFile) return;
  
  try{
    print("saving header file to TS/src folder...");

    InputStream input = createInputRaw(fontFileName);
    PrintWriter output = createWriter(headerFilePath + headerFileName);

    output.println("#include <pgmspace.h>");
    output.println();
    output.println("const uint8_t Robot" + str(fontSize) + "[] PROGMEM = {");

    int i = 0;
    int data = input.read();
    while(data != -1){
      output.print("0x");
      output.print(hex(data, 2));
      if(i++ < 15){
        output.print(", ");
      } else {
        output.println(",");
        i = 0;
      }
      data = input.read();
    }
    output.println("\n};");

    output.close();
    input.close();

    println("C header file created: " + headerFileName);

  } catch(IOException e){
    println("Failed to create C header file");
  }
}
