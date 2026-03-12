// Скрипт для создания шрифта Calibri10
// Настройки: размер 10, символы: цифры 0-9, латинские d, u, w, знаки : + - % . !, кириллица

import java.awt.Desktop;

// Настройки шрифта
String fontName = "calibri";
String fontType = ".ttf";
int fontSize = 10;  // Размер шрифта
int displayFontSize = 10;  // Размер для предварительного просмотра
boolean createHeaderFile = true;
boolean openFolder = false;  // Отключено для избежания ошибок

// Символы для включения в шрифт
static final int[] unicodeBlocks = {
  0x0030, 0x0039, // Цифры 0-9
};

static final int[] specificUnicodes = {
  // Специальные знаки (в порядке ASCII)
  0x0020,  // пробел (на всякий случай)
  0x0021,  // ! (восклицательный знак)
  0x0025,  // % (процент)
  0x002B,  // + (плюс)
  0x002D,  // - (минус/дефис)
  0x002E,  // . (точка)
  0x003A,  // : (двоеточие)
  // Латинские буквы
  0x0064,  // d
  0x0075,  // u
  0x0077,  // w
  // Кириллические заглавные буквы (в алфавитном порядке Unicode)
  0x0410,  // А
  0x0412,  // В
  0x0414,  // Д
  0x0415,  // Е
  0x0406,  // І (украинская буква I)
  0x0418,  // И (русская буква, на случай если І не поддерживается)
  0x041B,  // Л
  0x041C,  // М
  0x041D,  // Н
  0x041E,  // О
  0x0420,  // Р
  0x0421,  // С
  0x0422,  // Т
  0x042F,  // Я
  0x0417,  // З
  // Кириллические строчные буквы (в алфавитном порядке Unicode)
  0x0430,  // а
  0x0432,  // в
  0x0438,  // и
  0x0439,  // й
  0x043A,  // к
  0x043D,  // н
  0x0441,  // с
  0x0442,  // т
  0x0443,  // у
  0x0447,  // ч
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
  println("Creating Calibri10 font file...");
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
  String headerFilePath = "c:\\proshivki\\TS_TSB_compensation2\\TS_TSB_compensation\\src\\";

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
    PrintWriter output = createWriter(headerFilePath + "calibri10.h");

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
