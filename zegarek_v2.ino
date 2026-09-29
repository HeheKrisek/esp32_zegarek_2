#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <string.h>

// ============================================================
// OLED
// ============================================================

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

// ============================================================
// PRZYCISKI
//
// B1 = GPIO13
// B2 = GPIO14
// B3 = GPIO27
// B4 = GPIO26
// B5 = GPIO25
// B6 = GPIO33
// ============================================================

const uint8_t buttonPins[6] = {
  13,
  14,
  27,
  26,
  25,
  33
};

const unsigned long DEBOUNCE_MS = 35;

// Po ilu ms zaczyna się szybkie przewijanie
const unsigned long HOLD_DELAY = 500;

// Co ile ms następuje kolejna zmiana podczas trzymania
const unsigned long HOLD_REPEAT = 100;

struct ButtonState {
  bool raw = HIGH;
  bool stable = HIGH;

  unsigned long lastChange = 0;
  unsigned long pressedAt = 0;
  unsigned long lastRepeat = 0;

  bool pressEvent = false;
  bool repeatEvent = false;
};

ButtonState buttons[6];

// ============================================================
// TRYBY
// ============================================================

enum Mode {
  CLOCK_MODE,
  CALENDAR_MODE,
  TETRIS_MODE
};

Mode currentMode = CLOCK_MODE;

// ============================================================
// ZEGAR I DATA
// ============================================================

int hour = 0;
int minute = 0;
int second = 0;

int day = 1;
int month = 1;
int year = 2027;

unsigned long lastTick = 0;

// ============================================================
// POMOCNICZE FUNKCJE DATY
// ============================================================

bool isLeapYear(int y) {
  if (y % 400 == 0) return true;
  if (y % 100 == 0) return false;

  return (y % 4 == 0);
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

  if (day > maxDay) {
    day = maxDay;
  }
}

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

void changeHour(int delta) {
  hour += delta;

  if (hour > 23) hour = 0;
  if (hour < 0) hour = 23;
}

void changeMinute(int delta) {
  minute += delta;

  if (minute > 59) minute = 0;
  if (minute < 0) minute = 59;
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

  if (year < 1) {
    year = 1;
  }

  clampDay();
}

// ============================================================
// PRZYCISKI + DEBOUNCE + PRZYTRZYMANIE
// ============================================================

void updateButtons() {
  unsigned long now = millis();

  for (int i = 0; i < 6; i++) {
    buttons[i].pressEvent = false;
    buttons[i].repeatEvent = false;

    bool reading = digitalRead(buttonPins[i]);

    // Zmiana surowego stanu
    if (reading != buttons[i].raw) {
      buttons[i].raw = reading;
      buttons[i].lastChange = now;
    }

    // Stan jest stabilny dostatecznie długo
    if (
      now - buttons[i].lastChange >= DEBOUNCE_MS &&
      reading != buttons[i].stable
    ) {
      buttons[i].stable = reading;

      if (reading == LOW) {
        // Właśnie naciśnięto
        buttons[i].pressEvent = true;
        buttons[i].pressedAt = now;
        buttons[i].lastRepeat = now;
      }
    }

    // Przytrzymanie
    if (
      buttons[i].stable == LOW &&
      now - buttons[i].pressedAt >= HOLD_DELAY &&
      now - buttons[i].lastRepeat >= HOLD_REPEAT
    ) {
      buttons[i].repeatEvent = true;
      buttons[i].lastRepeat = now;
    }
  }
}

// ============================================================
// CENTROWANIE TEKSTU
// ============================================================

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

// ============================================================
// TETRIS
// ============================================================

const int TETRIS_W = 10;
const int TETRIS_H = 16;

const int CELL = 3;

const int BOARD_X = 2;
const int BOARD_Y = 8;

uint8_t tetrisBoard[TETRIS_H][TETRIS_W];

// 7 klocków x 4 obroty
//
// Każda liczba reprezentuje siatkę 4x4.
//
// Kolejność:
// I, O, T, S, Z, J, L

const uint16_t pieceMasks[7][4] = {
  // I
  {
    0x00F0,
    0x4444,
    0x0F00,
    0x2222
  },

  // O
  {
    0x0066,
    0x0066,
    0x0066,
    0x0066
  },

  // T
  {
    0x0072,
    0x0262,
    0x0270,
    0x0232
  },

  // S
  {
    0x0036,
    0x0462,
    0x0036,
    0x0462
  },

  // Z
  {
    0x0063,
    0x0264,
    0x0063,
    0x0264
  },

  // J
  {
    0x0071,
    0x0226,
    0x0470,
    0x0322
  },

  // L
  {
    0x0074,
    0x0622,
    0x0170,
    0x0223
  }
};

int currentPiece = 0;
int pieceRotation = 0;
int pieceX = 3;
int pieceY = 0;

bool gameRunning = false;
bool gamePaused = false;
bool gameOver = false;

uint32_t tetrisScore = 0;
int tetrisLines = 0;

unsigned long lastFall = 0;

// ============================================================
// TETRIS - SPRAWDZANIE KLOCKA
// ============================================================

bool pieceCell(
  int piece,
  int rotation,
  int x,
  int y
) {
  uint16_t mask = pieceMasks[piece][rotation];

  int bit = y * 4 + x;

  return (mask & (1U << bit)) != 0;
}

// ============================================================
// TETRIS - KOLIZJE
// ============================================================

bool pieceCollides(
  int newX,
  int newY,
  int newRotation
) {
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {

      if (!pieceCell(
        currentPiece,
        newRotation,
        x,
        y
      )) {
        continue;
      }

      int bx = newX + x;
      int by = newY + y;

      if (bx < 0 || bx >= TETRIS_W) {
        return true;
      }

      if (by >= TETRIS_H) {
        return true;
      }

      if (
        by >= 0 &&
        tetrisBoard[by][bx]
      ) {
        return true;
      }
    }
  }

  return false;
}

// ============================================================
// TETRIS - NOWY KLOCEK
// ============================================================

void spawnPiece() {
  currentPiece = random(0, 7);

  pieceRotation = 0;
  pieceX = 3;
  pieceY = 0;

  if (
    pieceCollides(
      pieceX,
      pieceY,
      pieceRotation
    )
  ) {
    gameRunning = false;
    gamePaused = false;
    gameOver = true;
  }
}

// ============================================================
// TETRIS - START
// ============================================================

void startTetris() {
  memset(
    tetrisBoard,
    0,
    sizeof(tetrisBoard)
  );

  tetrisScore = 0;
  tetrisLines = 0;

  gameRunning = true;
  gamePaused = false;
  gameOver = false;

  spawnPiece();

  lastFall = millis();
}

// ============================================================
// TETRIS - KASOWANIE LINII
// ============================================================

int clearLines() {
  int cleared = 0;

  for (int y = TETRIS_H - 1; y >= 0; y--) {

    bool full = true;

    for (int x = 0; x < TETRIS_W; x++) {
      if (!tetrisBoard[y][x]) {
        full = false;
        break;
      }
    }

    if (!full) {
      continue;
    }

    // Przesuwamy wszystko powyżej o jeden w dół

    for (int yy = y; yy > 0; yy--) {
      memcpy(
        tetrisBoard[yy],
        tetrisBoard[yy - 1],
        TETRIS_W
      );
    }

    memset(
      tetrisBoard[0],
      0,
      TETRIS_W
    );

    cleared++;

    // Sprawdź ten sam rząd ponownie,
    // bo właśnie wjechał tam rząd z góry
    y++;
  }

  return cleared;
}

// ============================================================
// TETRIS - ZAPISANIE KLOCKA DO PLANSZY
// ============================================================

void lockPiece() {
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {

      if (!pieceCell(
        currentPiece,
        pieceRotation,
        x,
        y
      )) {
        continue;
      }

      int bx = pieceX + x;
      int by = pieceY + y;

      if (
        bx >= 0 &&
        bx < TETRIS_W &&
        by >= 0 &&
        by < TETRIS_H
      ) {
        tetrisBoard[by][bx] = 1;
      }
    }
  }

  int cleared = clearLines();

  if (cleared > 0) {

    tetrisLines += cleared;

    switch (cleared) {
      case 1:
        tetrisScore += 100;
        break;

      case 2:
        tetrisScore += 300;
        break;

      case 3:
        tetrisScore += 500;
        break;

      case 4:
        tetrisScore += 800;
        break;
    }
  }

  spawnPiece();
}

// ============================================================
// TETRIS - RUCH
// ============================================================

void movePiece(int dx) {
  if (!gameRunning || gamePaused) {
    return;
  }

  int newX = pieceX + dx;

  if (
    !pieceCollides(
      newX,
      pieceY,
      pieceRotation
    )
  ) {
    pieceX = newX;
  }
}

// ============================================================
// TETRIS - OBRÓT
// ============================================================

void rotatePiece() {
  if (!gameRunning || gamePaused) {
    return;
  }

  int newRotation =
    (pieceRotation + 1) % 4;

  // Normalny obrót
  if (
    !pieceCollides(
      pieceX,
      pieceY,
      newRotation
    )
  ) {
    pieceRotation = newRotation;
    return;
  }

  // Prosty wall-kick w lewo
  if (
    !pieceCollides(
      pieceX - 1,
      pieceY,
      newRotation
    )
  ) {
    pieceX--;
    pieceRotation = newRotation;
    return;
  }

  // Wall-kick w prawo
  if (
    !pieceCollides(
      pieceX + 1,
      pieceY,
      newRotation
    )
  ) {
    pieceX++;
    pieceRotation = newRotation;
  }
}

// ============================================================
// TETRIS - PRĘDKOŚĆ
// ============================================================

unsigned long tetrisFallInterval() {
  int interval =
    500 - tetrisLines * 12;

  if (interval < 120) {
    interval = 120;
  }

  return interval;
}

// ============================================================
// TETRIS - AUTOMATYCZNY SPADEK
// ============================================================

void updateTetris() {
  if (
    !gameRunning ||
    gamePaused
  ) {
    return;
  }

  unsigned long now = millis();

  if (
    now - lastFall <
    tetrisFallInterval()
  ) {
    return;
  }

  lastFall = now;

  if (
    !pieceCollides(
      pieceX,
      pieceY + 1,
      pieceRotation
    )
  ) {
    pieceY++;
  }
  else {
    lockPiece();
  }
}

// ============================================================
// TETRIS - RYSOWANIE JEDNEJ KOMÓRKI
// ============================================================

void drawTetrisCell(
  int x,
  int y
) {
  int px =
    BOARD_X + x * CELL;

  int py =
    BOARD_Y + y * CELL;

  display.fillRect(
    px,
    py,
    CELL - 1,
    CELL - 1,
    SSD1306_WHITE
  );
}

// ============================================================
// TETRIS - EKRAN
// ============================================================

void drawTetris() {
  display.clearDisplay();

  // Ramka planszy

  display.drawRect(
    BOARD_X - 1,
    BOARD_Y - 1,
    TETRIS_W * CELL + 2,
    TETRIS_H * CELL + 2,
    SSD1306_WHITE
  );

  // Zablokowane klocki

  for (int y = 0; y < TETRIS_H; y++) {
    for (int x = 0; x < TETRIS_W; x++) {

      if (tetrisBoard[y][x]) {
        drawTetrisCell(x, y);
      }
    }
  }

  // Aktualnie spadający klocek

  if (gameRunning) {
    for (int y = 0; y < 4; y++) {
      for (int x = 0; x < 4; x++) {

        if (!pieceCell(
          currentPiece,
          pieceRotation,
          x,
          y
        )) {
          continue;
        }

        int bx = pieceX + x;
        int by = pieceY + y;

        if (
          bx >= 0 &&
          bx < TETRIS_W &&
          by >= 0 &&
          by < TETRIS_H
        ) {
          drawTetrisCell(bx, by);
        }
      }
    }
  }

  // Panel po prawej

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(39, 0);
  display.print("TETRIS");

  display.setCursor(39, 12);
  display.print("PUNKTY");

  display.setCursor(39, 21);
  display.print(tetrisScore);

  display.setCursor(39, 33);
  display.print("LINIE");

  display.setCursor(39, 42);
  display.print(tetrisLines);

  display.setCursor(39, 54);

  if (gameOver) {
    display.print("GAME OVER");
  }
  else if (!gameRunning) {
    display.print("B5 START");
  }
  else if (gamePaused) {
    display.print("PAUZA");
  }
  else {
    display.print("GRA");
  }

  display.display();
}

// ============================================================
// ZEGAR - EKRAN
// ============================================================

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

  printCentered(
    "ZEGAR",
    0,
    1
  );

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

// ============================================================
// KALENDARZ - EKRAN
// ============================================================

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

// ============================================================
// ZMIANA TRYBU
// ============================================================

void nextMode() {
  if (currentMode == CLOCK_MODE) {
    currentMode = CALENDAR_MODE;
  }
  else if (currentMode == CALENDAR_MODE) {
    currentMode = TETRIS_MODE;

    // Żeby klocek nie zrobił teleportu po wejściu
    lastFall = millis();
  }
  else {
    currentMode = CLOCK_MODE;
  }
}

// ============================================================
// OBSŁUGA POJEDYNCZEGO NACIŚNIĘCIA
// ============================================================

void handlePress(int button) {

  // ----------------------------------------------------------
  // ZEGAR
  // ----------------------------------------------------------

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

        // Od tego momentu liczymy nową pełną sekundę
        lastTick = millis();
        break;

      case 5:
        nextMode();
        break;
    }

    return;
  }

  // ----------------------------------------------------------
  // KALENDARZ
  // ----------------------------------------------------------

  if (currentMode == CALENDAR_MODE) {

    switch (button) {

      case 0:
        incrementDate();
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
        nextMode();
        break;
    }

    return;
  }

  // ----------------------------------------------------------
  // TETRIS
  // ----------------------------------------------------------

  if (currentMode == TETRIS_MODE) {

    switch (button) {

      // Lewo
      case 0:
        movePiece(-1);
        break;

      // Prawo
      case 1:
        movePiece(+1);
        break;

      // Obrót
      case 2:
        rotatePiece();
        break;

      // Pauza
      case 3:
        if (gameRunning) {
          gamePaused = !gamePaused;

          // po wznowieniu nie chcemy natychmiastowego spadku
          lastFall = millis();
        }
        break;

      // Start / stop
      case 4:
        if (gameRunning) {
          gameRunning = false;
          gamePaused = false;
          gameOver = false;
        }
        else {
          startTetris();
        }
        break;

      // Zmiana trybu
      case 5:
        nextMode();
        break;
    }
  }
}

// ============================================================
// PRZYTRZYMANIE
// ============================================================

void handleRepeat(int button) {

  // ----------------------------------------------------------
  // ZEGAR
  //
  // B1-B4 przyspieszają.
  // B5 reset sekund nie potrzebuje powtarzania.
  // ----------------------------------------------------------

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
    }

    return;
  }

  // ----------------------------------------------------------
  // KALENDARZ
  //
  // Wszystkie B1-B5 mogą przewijać.
  // ----------------------------------------------------------

  if (currentMode == CALENDAR_MODE) {

    switch (button) {

      case 0:
        incrementDate();
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
    }

    return;
  }

  // ----------------------------------------------------------
  // TETRIS
  //
  // Przytrzymanie lewo/prawo.
  // Obrót, pauza, start i tryb nie powtarzają się.
  // ----------------------------------------------------------

  if (currentMode == TETRIS_MODE) {

    if (button == 0) {
      movePiece(-1);
    }

    if (button == 1) {
      movePiece(+1);
    }
  }
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);

  Wire.begin(
    OLED_SDA,
    OLED_SCL
  );

  if (
    !display.begin(
      SSD1306_SWITCHCAPVCC,
      0x3C
    )
  ) {
    Serial.println(
      "Blad inicjalizacji OLED!"
    );

    while (true) {
      delay(100);
    }
  }

  display.clearDisplay();
  display.setTextColor(
    SSD1306_WHITE
  );

  // Przyciski

  for (int i = 0; i < 6; i++) {

    pinMode(
      buttonPins[i],
      INPUT_PULLUP
    );

    buttons[i].raw =
      digitalRead(buttonPins[i]);

    buttons[i].stable =
      buttons[i].raw;
  }

  // Losowanie klocków

  randomSeed(
    micros()
  );

  lastTick = millis();
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  unsigned long now = millis();

  // ----------------------------------------------------------
  // ZEGAR TYKA NIEZALEŻNIE OD TRYBU
  // ----------------------------------------------------------

  while (
    now - lastTick >= 1000
  ) {
    lastTick += 1000;

    incrementSecond();
  }

  // ----------------------------------------------------------
  // PRZYCISKI
  // ----------------------------------------------------------

  updateButtons();

  for (int i = 0; i < 6; i++) {

    if (buttons[i].pressEvent) {
      handlePress(i);
    }

    if (buttons[i].repeatEvent) {
      handleRepeat(i);
    }
  }

  // ----------------------------------------------------------
  // TETRIS
  // ----------------------------------------------------------

  if (currentMode == TETRIS_MODE) {
    updateTetris();
  }

  // ----------------------------------------------------------
  // RYSOWANIE
  // ----------------------------------------------------------

  if (currentMode == CLOCK_MODE) {
    drawClock();
  }
  else if (
    currentMode == CALENDAR_MODE
  ) {
    drawCalendar();
  }
  else {
    drawTetris();
  }

  delay(10);
}