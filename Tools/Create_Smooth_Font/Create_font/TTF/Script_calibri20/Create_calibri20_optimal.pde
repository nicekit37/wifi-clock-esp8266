// Скрипт для создания шрифта Calibri20 с оптимальным набором символов
// Настройки: размер 20, цифры, английский и русский алфавит, основные знаки

import java.awt.Desktop;

// Настройки шрифта
String fontName = "calibri";
String fontType = ".ttf";
int fontSize = 20;  // Размер шрифта
int displayFontSize = 15;  // Размер для предварительного просмотра
boolean createHeaderFile = true;
boolean openFolder = false;  // Отключено для избежания ошибок

// Оптимальный набор символов
static final int[] unicodeBlocks = {
  0x0030, 0x0039, // Цифры 0-9
  0x0041, 0x005A, // Заглавные английские A-Z
  0x0061, 0x007A, // Строчные английские a-z
  0x0410, 0x044F, // Кириллица А-Я, а-я (включая Ё, ё)
};

static final int[] specificUnicodes = {
  0x0020, 0x0021, 0x0022, 0x0024, 0x0025, 0x0027, 0x0028, 0x0029, // пробел, !, ", $, %, ', (, )
  0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F, 0x003A, 0x003B, // *, +, ,, -, ., /, :, ;
  0x003D, 0x003F, 0x0040, 0x005B, 0x005D, 0x005F, 0x007B, 0x007D, 0x007E, // =, ?, @, [, ], _, {, }, ~
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
  println("Creating Calibri20 OPTIMAL font file...");
  println("Unicode blocks included     = " + (blockCount/2));
  println("Specific unicodes included  = " + specificUnicodes.length);
  println("Total number of characters  = " + count);

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

  String fontFileName = "FontFiles/" + fontName + str(fontSize) + ".vlw";

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
    print("saving header file to FontFile folder...");

    InputStream input = createInputRaw(fontFileName);
    PrintWriter output = createWriter("FontFiles/" + fontName + str(fontSize) + ".h");

    output.println("#include <pgmspace.h>");
    output.println();
    output.println("const uint8_t " + fontName + str(fontSize) + "[] PROGMEM = {");

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

    println("C header file created.");

  } catch(IOException e){
    println("Failed to create C header file");
  }
}