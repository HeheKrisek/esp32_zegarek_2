#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ========================================
// OLED
// ========================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_RESET -1

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

// ========================================
// PRZYCISKI
// ========================================

const uint8_t buttonPins[6] = {
  13, // przycisk 1
  14, // przycisk 2
  27, // przycisk 3
  26, // przycisk 4
  25, // przycisk 5
  33  // przycisk 6
};

bool lastReading[6];
bool stableState[6];

unsigned long lastDebounceTime[6];

const unsigned long DEBOUNCE_MS = 40;


// ========================================
// TRYBY
// ========================================

enum Mode {
  CLOCK_MODE,
  CALENDAR_MODE
};

Mode currentMode = CLOCK_MODE;


// ========================================
// DATA I CZAS
// ========================================

int hour = 0;
int minute = 0;
int second = 0;

int day = 1;
int month = 1;
int year = 2027;

unsigned long lastTick = 0;


// ========================================
// POMOCNICZE
// ========================================

bool isLeapYear(int y) {
  if (y % 400 == 0)
    return true;

  if (y % 100 == 0)
    return false;

  return y % 4 == 0;
}


int daysInMonth(int m, int y) {
  switch (m) {

    case 1:
    case 3:
    case 5:
    case 7:
    case 8:
    case 10:
    case 12:
      return 31;

    case 4:
    case 6:
    case 9:
    case 11:
      return 30;

    case 2:
      return isLeapYear(y) ? 29 : 28;
  }

  return 30;
}


void clampDay() {
  int maxDay = daysInMonth(month, year);

  if (day > maxDay)
    day = maxDay;
}


// ========================================
// AUTOMATYCZNE TYKANIE CZASU
// ========================================

void incrementDate() {
  day++;

  if (day > daysInMonth(month, year)) {
    day = 1;
    month++;

    if (month > 12) {
      month = 1;
      year++;
    }
  }
}


void incrementSecond() {
  second++;

  if (second >= 60) {
    second = 0;
    minute++;
  }

  if (minute >= 60) {
    minute = 0;
    hour++;
  }

  if (hour >= 24) {
    hour = 0;
    incrementDate();
  }
}


// ========================================
// RĘCZNE USTAWIANIE ZEGARA
// ========================================

void changeHour(int delta) {
  hour += delta;

  if (hour > 23)
    hour = 0;

  if (hour < 0)
    hour = 23;
}


void changeMinute(int delta) {
  minute += delta;

  if (minute > 59)
    minute = 0;

  if (minute < 0)
    minute = 59;
}


// ========================================
// RĘCZNE USTAWIANIE DATY
// ========================================

void nextDay() {
  incrementDate();
}


void nextMonth() {
  month++;

  if (month > 12) {
    month = 1;
    year++;
  }

  clampDay();
}


void changeYear(int delta) {
  year += delta;

  // zabezpieczenie przed jakimiś absurdami
  if (year < 1)
    year = 1;

  clampDay();
}


// ========================================
// OBSŁUGA PRZYCISKÓW
// ========================================

bool buttonPressed(int index) {

  bool reading = digitalRead(buttonPins[index]);

  if (reading != lastReading[index]) {
    lastDebounceTime[index] = millis();
    lastReading[index] = reading;
  }

  if (millis() - lastDebounceTime[index] > DEBOUNCE_MS) {

    if (reading != stableState[index]) {

      stableState[index] = reading;

      // INPUT_PULLUP:
      // LOW oznacza wciśnięcie
      if (stableState[index] == LOW) {
        return true;
      }
    }
  }

  return false;
}


// ========================================
// REAKCJE NA PRZYCISKI
// ========================================

void handleButton(int button) {

  if (currentMode == CLOCK_MODE) {

    switch (button) {

      case 0:
        changeHour(+1);
        break;

      case 1:
        changeHour(-1);
        break;

      case 2:
        changeMinute(+1);
        break;

      case 3:
        changeMinute(-1);
        break;

      case 4:
        second = 0;

        // dzięki temu pełna sekunda minie
        // zanim pokaże się 01
        lastTick = millis();
        break;

      case 5:
        currentMode = CALENDAR_MODE;
        break;
    }
  }

  else {

    switch (button) {

      case 0:
        nextDay();
        break;

      case 1:
        nextMonth();
        break;

      case 2:
        changeYear(+1);
        break;

      case 3:
        changeYear(-1);
        break;

      case 4:
        changeYear(+5);
        break;

      case 5:
        currentMode = CLOCK_MODE;
        break;
    }
  }
}


// ========================================
// CENTROWANIE TEKSTU
// ========================================

void printCentered(const String &text, int y, int size) {

  display.setTextSize(size);

  int16_t x1;
  int16_t y1;

  uint16_t w;
  uint16_t h;

  display.getTextBounds(
    text,
    0,
    y,
    &x1,
    &y1,
    &w,
    &h
  );

  int x = (SCREEN_WIDTH - w) / 2;

  display.setCursor(x, y);
  display.print(text);
}


// ========================================
// EKRAN ZEGARA
// ========================================

void drawClock() {

  char timeBuffer[9];

  snprintf(
    timeBuffer,
    sizeof(timeBuffer),
    "%02d:%02d:%02d",
    hour,
    minute,
    second
  );


  char dateBuffer[11];

  snprintf(
    dateBuffer,
    sizeof(dateBuffer),
    "%02d.%02d.%04d",
    day,
    month,
    year
  );


  display.clearDisplay();

  printCentered("ZEGAR", 0, 1);

  printCentered(
    String(timeBuffer),
    20,
    2
  );

  printCentered(
    String(dateBuffer),
    50,
    1
  );

  display.display();
}


// ========================================
// EKRAN KALENDARZA
// ========================================

void drawCalendar() {

  char dateBuffer[11];

  snprintf(
    dateBuffer,
    sizeof(dateBuffer),
    "%02d.%02d.%04d",
    day,
    month,
    year
  );


  char timeBuffer[9];

  snprintf(
    timeBuffer,
    sizeof(timeBuffer),
    "%02d:%02d:%02d",
    hour,
    minute,
    second
  );


  display.clearDisplay();

  printCentered(
    "KALENDARZ",
    0,
    1
  );

  printCentered(
    String(dateBuffer),
    20,
    2
  );

  printCentered(
    String(timeBuffer),
    50,
    1
  );

  display.display();
}


// ========================================
// SETUP
// ========================================

void setup() {

  Serial.begin(115200);

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(
        SSD1306_SWITCHCAPVCC,
        0x3C
      )) {

    Serial.println("Blad OLED!");

    while (true) {
      delay(100);
    }
  }


  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);


  // przyciski
  for (int i = 0; i < 6; i++) {

    pinMode(
      buttonPins[i],
      INPUT_PULLUP
    );

    lastReading[i] =
      digitalRead(buttonPins[i]);

    stableState[i] =
      lastReading[i];

    lastDebounceTime[i] = 0;
  }


  lastTick = millis();
}


// ========================================
// LOOP
// ========================================

void loop() {

  // ----------------------------------------
  // ZEGAR
  // ----------------------------------------

  unsigned long now = millis();

  while (now - lastTick >= 1000) {
    lastTick += 1000;
    incrementSecond();
  }


  // ----------------------------------------
  // PRZYCISKI
  // ----------------------------------------

  for (int i = 0; i < 6; i++) {

    if (buttonPressed(i)) {
      handleButton(i);
    }
  }


  // ----------------------------------------
  // OLED
  // ----------------------------------------

  if (currentMode == CLOCK_MODE) {
    drawClock();
  }
  else {
    drawCalendar();
  }


  delay(10);
}