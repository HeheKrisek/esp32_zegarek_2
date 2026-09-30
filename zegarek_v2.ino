#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <string.h>

// ============================================================
// SPRZET: ESP32 + OLED 128x64 I2C + 6 przyciskow do GND
// OLED: SDA=21, SCK/SCL=22, VDD=3V3, GND=GND
// B1..B6: 13, 14, 27, 26, 25, 33
// ============================================================
constexpr uint8_t OLED_SDA = 21;
constexpr uint8_t OLED_SCL = 22;
constexpr uint8_t BUTTON_PINS[6] = {13, 14, 27, 26, 25, 33};

Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ============================================================
// PRZYCISKI: debouncing, pojedyncze klikniecie, autorepeat,
// oddzielne rozpoznanie krotkiego i dlugiego B5 w grach.
// ============================================================
constexpr unsigned long DEBOUNCE_MS = 35;
constexpr unsigned long REPEAT_START_MS = 450;
constexpr unsigned long REPEAT_EVERY_MS = 95;
constexpr unsigned long RESTART_HOLD_MS = 800;

struct Button {
  bool raw = HIGH;
  bool stable = HIGH;
  unsigned long rawChangedAt = 0;
  unsigned long pressedAt = 0;
  unsigned long repeatedAt = 0;
  bool longSent = false;
  bool pressEvent = false;
  bool releaseEvent = false;
  bool repeatEvent = false;
  bool longEvent = false;
};
Button buttons[6];

void readButtons() {
  const unsigned long now = millis();
  for (uint8_t i = 0; i < 6; ++i) {
    Button &b = buttons[i];
    b.pressEvent = false;
    b.releaseEvent = false;
    b.repeatEvent = false;
    b.longEvent = false;

    const bool reading = digitalRead(BUTTON_PINS[i]);
    if (reading != b.raw) {
      b.raw = reading;
      b.rawChangedAt = now;
    }
    if (reading != b.stable && now - b.rawChangedAt >= DEBOUNCE_MS) {
      b.stable = reading;
      if (reading == LOW) {
        b.pressEvent = true;
        b.pressedAt = now;
        b.repeatedAt = now;
        b.longSent = false;
      } else {
        b.releaseEvent = true;
      }
    }
    if (b.stable == LOW) {
      if (i == 4 && !b.longSent && now - b.pressedAt >= RESTART_HOLD_MS) {
        b.longSent = true;
        b.longEvent = true;
      }
      if (now - b.pressedAt >= REPEAT_START_MS &&
          now - b.repeatedAt >= REPEAT_EVERY_MS) {
        b.repeatEvent = true;
        b.repeatedAt = now;
      }
    }
  }
}

// ============================================================
// CZAS I KALENDARZ: dzialaja niezaleznie od wyswietlanego trybu
// ============================================================
enum Mode { CLOCK_MODE, CALENDAR_MODE, TETRIS_MODE, SNAKE_MODE, SOLITAIRE_MODE };
Mode mode = CLOCK_MODE;

int hours = 0, minutes = 0, seconds = 0;
int day = 1, month = 1, year = 2027;
unsigned long lastTick = 0;

bool leapYear(int y) {
  return (y % 400 == 0) || (y % 4 == 0 && y % 100 != 0);
}
int daysInMonth(int m, int y) {
  const uint8_t lengths[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  return (m == 2 && leapYear(y)) ? 29 : lengths[m - 1];
}
void fixDay() {
  if (day > daysInMonth(month, year)) day = daysInMonth(month, year);
}
void nextDay() {
  if (++day > daysInMonth(month, year)) {
    day = 1;
    if (++month > 12) {
      month = 1;
      year = year == 9999 ? 1 : year + 1;
    }
  }
}
void nextMonth() {
  if (++month > 12) {
    month = 1;
    year = year == 9999 ? 1 : year + 1;
  }
  fixDay();
}
void changeYear(int delta) {
  year = constrain(year + delta, 1, 9999);
  fixDay();
}
void tickSecond() {
  if (++seconds >= 60) {
    seconds = 0;
    if (++minutes >= 60) {
      minutes = 0;
      if (++hours >= 24) {
        hours = 0;
        nextDay();
      }
    }
  }
}

void centered(const char *text, int y, uint8_t size) {
  display.setTextSize(size);
  int16_t x1, y1;
  uint16_t width, height;
  display.getTextBounds(text, 0, y, &x1, &y1, &width, &height);
  display.setCursor((128 - width) / 2, y);
  display.print(text);
}
void drawClock() {
  char timeText[12], dateText[16];
  snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d", hours, minutes, seconds);
  snprintf(dateText, sizeof(dateText), "%02d.%02d.%04d", day, month, year);
  display.clearDisplay();
  centered("ZEGAR", 0, 1);
  centered(timeText, 20, 2);
  centered(dateText, 50, 1);
  display.display();
}
void drawCalendar() {
  char timeText[12], dateText[16];
  snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d", hours, minutes, seconds);
  snprintf(dateText, sizeof(dateText), "%02d.%02d.%04d", day, month, year);
  display.clearDisplay();
  centered("KALENDARZ", 0, 1);
  centered(dateText, 20, 2);
  centered(timeText, 50, 1);
  display.display();
}

// ============================================================
// TETRIS: plansza 10x16, kratka 4x4 px = 40x64 px.
// Plansza ma PELNA wysokosc wyswietlacza (bez dolnego marginesu).
// ============================================================
constexpr uint8_t BW = 10;
constexpr uint8_t BH = 16;
constexpr uint8_t CELL = 4;
constexpr uint8_t BX = 2;
constexpr uint8_t BY = 0;
constexpr unsigned long SOFT_DROP_MS = 55;
uint8_t board[BH][BW];

// Maska: bit (y*4 + x) oznacza zajete pole siatki 4x4.
// Klocki: I, O, T, S, Z, J, L; po 4 obroty.
const uint16_t pieces[7][4] = {
  {0x00F0, 0x4444, 0x0F00, 0x2222},
  {0x0066, 0x0066, 0x0066, 0x0066},
  {0x0072, 0x0262, 0x0270, 0x0232},
  {0x0036, 0x0462, 0x0036, 0x0462},
  {0x0063, 0x0264, 0x0063, 0x0264},
  {0x0071, 0x0226, 0x0470, 0x0322},
  {0x0074, 0x0622, 0x0170, 0x0223}
};

int currentPiece = 0, nextPiece = 0;
int rotation = 0, pieceX = 3, pieceY = 0;
bool playing = false, paused = false, gameOver = false;
uint32_t score = 0;
uint16_t lines = 0;
unsigned long lastFall = 0, lastSoftDrop = 0;

bool occupied(int which, int rot, int x, int y) {
  return (pieces[which][rot] & (uint16_t(1) << (y * 4 + x))) != 0;
}
bool collides(int newX, int newY, int newRot) {
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      if (!occupied(currentPiece, newRot, x, y)) continue;
      const int bx = newX + x, by = newY + y;
      if (bx < 0 || bx >= BW || by >= BH) return true;
      if (by >= 0 && board[by][bx]) return true;
    }
  }
  return false;
}
void spawnPiece() {
  currentPiece = nextPiece;
  nextPiece = random(7);
  rotation = 0;
  pieceX = 3;
  pieceY = 0;
  if (collides(pieceX, pieceY, rotation)) {
    playing = false;
    paused = false;
    gameOver = true;
  }
}
void newGame() {
  memset(board, 0, sizeof(board));
  score = 0;
  lines = 0;
  playing = true;
  paused = false;
  gameOver = false;
  nextPiece = random(7);
  spawnPiece();
  lastFall = millis();
  lastSoftDrop = millis();
}
int removeLines() {
  int removed = 0;
  for (int y = BH - 1; y >= 0; --y) {
    bool full = true;
    for (int x = 0; x < BW; ++x) {
      if (!board[y][x]) { full = false; break; }
    }
    if (!full) continue;
    for (int yy = y; yy > 0; --yy) {
      memcpy(board[yy], board[yy - 1], sizeof(board[yy]));
    }
    memset(board[0], 0, sizeof(board[0]));
    ++removed;
    ++y;  // Ponownie sprawdz rzad, do ktorego spadl rzad z gory.
  }
  return removed;
}
void lockPiece() {
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      if (!occupied(currentPiece, rotation, x, y)) continue;
      const int bx = pieceX + x, by = pieceY + y;
      if (bx >= 0 && bx < BW && by >= 0 && by < BH) board[by][bx] = 1;
    }
  }
  const int removed = removeLines();
  lines += removed;
  const uint16_t reward[5] = {0, 100, 300, 500, 800};
  if (removed >= 1 && removed <= 4) score += reward[removed];
  spawnPiece();
}
void movePiece(int dx) {
  if (!playing || paused) return;
  if (!collides(pieceX + dx, pieceY, rotation)) pieceX += dx;
}
void rotatePiece() {
  if (!playing || paused) return;
  const int newRot = (rotation + 1) % 4;
  // Prosty wall-kick: obrot w miejscu, potem lekkie odsuniecie od sciany.
  const int offsets[] = {0, -1, 1, -2, 2};
  for (int dx : offsets) {
    if (!collides(pieceX + dx, pieceY, newRot)) {
      pieceX += dx;
      rotation = newRot;
      return;
    }
  }
}
// Przesuwa o 1 rzad lub osadza klocek, gdy nie ma miejsca.
void fallOneRow() {
  if (!playing || paused) return;
  if (!collides(pieceX, pieceY + 1, rotation)) ++pieceY;
  else lockPiece();
  lastFall = millis();
}
unsigned long fallInterval() {
  const long ms = 500L - (long)lines * 12L;
  return (unsigned long)max(120L, ms);
}
void updateTetris() {
  if (!playing || paused) return;
  const unsigned long now = millis();
  // B4: jeden rzad po nacisnieciu, potem szybko tak dlugo, jak jest trzymany.
  if (buttons[3].stable == LOW && now - lastSoftDrop >= SOFT_DROP_MS) {
    fallOneRow();
    lastSoftDrop = now;
  } else if (now - lastFall >= fallInterval()) {
    fallOneRow();
  }
}
void cell(int x, int y) {
  // 3x3 piksele w kratce 4x4 daja cienka siatke miedzy klockami.
  display.fillRect(BX + x * CELL, BY + y * CELL,
                   CELL - 1, CELL - 1, SSD1306_WHITE);
}
void drawTetris() {
  display.clearDisplay();
  // Ramka tylko z bokow: przy pelnej wysokosci nie ma juz miejsca
  // na dodatkowy poziomy margines.
  display.drawFastVLine(0, 0, 64, SSD1306_WHITE);
  display.drawFastVLine(43, 0, 64, SSD1306_WHITE);

  for (int y = 0; y < BH; ++y) {
    for (int x = 0; x < BW; ++x) {
      if (board[y][x]) cell(x, y);
    }
  }
  if (playing) {
    for (int y = 0; y < 4; ++y) {
      for (int x = 0; x < 4; ++x) {
        if (!occupied(currentPiece, rotation, x, y)) continue;
        const int bx = pieceX + x, by = pieceY + y;
        if (bx >= 0 && bx < BW && by >= 0 && by < BH) cell(bx, by);
      }
    }
  }

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  // Na dwukolorowym OLED pierwsze 16 wierszy sa fizycznie zolte.
  // Tylko napis TETRIS lezy w zoltym pasku bocznego panelu.
  // Pozostale informacje sa ponizej, w niebieskiej czesci ekranu.
  display.setCursor(49, 3);  display.print("TETRIS");
  display.setCursor(49, 18); display.print("PKT:");
  display.setCursor(74, 18); display.print(score);
  display.setCursor(49, 30); display.print("LIN:");
  display.setCursor(74, 30); display.print(lines);
  display.setCursor(49, 42); display.print("NEXT");
  // Podglad nastepnego klocka: calkowicie w niebieskiej czesci.
  if (playing) {
    for (int y = 0; y < 4; ++y) {
      for (int x = 0; x < 4; ++x) {
        if (occupied(nextPiece, 0, x, y)) {
          display.fillRect(91 + x * 3, 44 + y * 3, 2, 2, SSD1306_WHITE);
        }
      }
    }
  }
  display.setCursor(49, 56);
  if (gameOver)         display.print("KONIEC");
  else if (!playing)   display.print("B5 START");
  else if (paused)     display.print("PAUZA");
  else                 display.print("GRA");
  display.display();
}

// ============================================================
// SNAKE: zolty naglowek (y=0..15) + CALE niebieskie pole gry.
// Siatka 32x12, komorka 4x4: 128 x 48 pikseli (y=16..63).
// B1/B2 = skret o 90 stopni WZGLEDEM kierunku weza.
// B3 = przyspieszenie tylko przez czas przytrzymania.
// ============================================================
constexpr uint8_t SNAKE_CELL = 4;
constexpr uint8_t SNAKE_W = 32;
constexpr uint8_t SNAKE_H = 12;
constexpr uint8_t SNAKE_TOP = 16;
constexpr uint16_t SNAKE_MAX = SNAKE_W * SNAKE_H;
constexpr unsigned long SNAKE_STEP_MS = 180;
constexpr unsigned long SNAKE_FAST_STEP_MS = 65;

struct SnakePoint {
  uint8_t x, y;
};
SnakePoint snakeBody[SNAKE_MAX]; // glowa: element 0
SnakePoint snakeFood = {0, 0};
uint16_t snakeLength = 0;
uint16_t snakeScore = 0;
uint8_t snakeDirection = 1; // 0=gora, 1=prawo, 2=dol, 3=lewo
bool snakeRunning = false, snakePaused = false;
bool snakeGameOver = false, snakeWon = false;
bool snakeTurnQueued = false; // najwyzej jeden skret miedzy krokami
unsigned long lastSnakeStep = 0;

bool snakeAt(uint8_t x, uint8_t y) {
  for (uint16_t i = 0; i < snakeLength; ++i) {
    if (snakeBody[i].x == x && snakeBody[i].y == y) return true;
  }
  return false;
}

void placeSnakeFood() {
  const uint16_t freeCount = SNAKE_MAX - snakeLength;
  if (freeCount == 0) {
    snakeRunning = false;
    snakeWon = true;
    return;
  }
  // Losujemy rownomiernie sposrod pustych pol, bez petli bez konca.
  uint16_t target = random(freeCount);
  for (uint8_t y = 0; y < SNAKE_H; ++y) {
    for (uint8_t x = 0; x < SNAKE_W; ++x) {
      if (snakeAt(x, y)) continue;
      if (target-- == 0) {
        snakeFood = {x, y};
        return;
      }
    }
  }
}

void newSnakeGame() {
  snakeLength = 4;
  snakeScore = 0;
  snakeDirection = 1;
  snakeRunning = true;
  snakePaused = false;
  snakeGameOver = false;
  snakeWon = false;
  snakeTurnQueued = false;
  const uint8_t headX = SNAKE_W / 2;
  const uint8_t headY = SNAKE_H / 2;
  for (uint16_t i = 0; i < snakeLength; ++i) {
    snakeBody[i] = {(uint8_t)(headX - i), headY};
  }
  placeSnakeFood();
  lastSnakeStep = millis();
}

void snakeTurn(int8_t change) {
  if (!snakeRunning || snakePaused || snakeTurnQueued) return;
  // Lewo: -1 (np. prawo -> gora). Prawo: +1 (prawo -> dol).
  snakeDirection = (snakeDirection + 4 + change) % 4;
  snakeTurnQueued = true;
}

void stepSnake() {
  if (!snakeRunning || snakePaused) return;
  const int8_t dx[] = {0, 1, 0, -1};
  const int8_t dy[] = {-1, 0, 1, 0};
  const int nextX = (int)snakeBody[0].x + dx[snakeDirection];
  const int nextY = (int)snakeBody[0].y + dy[snakeDirection];
  snakeTurnQueued = false;

  if (nextX < 0 || nextX >= SNAKE_W || nextY < 0 || nextY >= SNAKE_H) {
    snakeRunning = false;
    snakeGameOver = true;
    return;
  }
  const bool eating = (nextX == snakeFood.x && nextY == snakeFood.y);
  // Wolno wejsc na dawne pole ogona, o ile ogon w tym kroku sie przesunie.
  const uint16_t checked = eating ? snakeLength : snakeLength - 1;
  for (uint16_t i = 0; i < checked; ++i) {
    if (snakeBody[i].x == nextX && snakeBody[i].y == nextY) {
      snakeRunning = false;
      snakeGameOver = true;
      return;
    }
  }
  if (eating && snakeLength < SNAKE_MAX) ++snakeLength;
  for (int i = snakeLength - 1; i > 0; --i) snakeBody[i] = snakeBody[i - 1];
  snakeBody[0] = {(uint8_t)nextX, (uint8_t)nextY};
  if (eating) {
    ++snakeScore;
    placeSnakeFood();
  }
}

void updateSnake() {
  if (!snakeRunning || snakePaused) return;
  const unsigned long now = millis();
  const unsigned long interval = buttons[2].stable == LOW
                               ? SNAKE_FAST_STEP_MS : SNAKE_STEP_MS;
  if (now - lastSnakeStep >= interval) {
    lastSnakeStep = now;
    stepSnake();
  }
}

void snakeMessage(const char *message) {
  // Komunikat nakladany tylko wtedy, gdy gra nie jest aktywna.
  const uint8_t x = 32, y = 34, w = 64, h = 16;
  display.fillRect(x, y, w, h, SSD1306_BLACK);
  display.drawRect(x, y, w, h, SSD1306_WHITE);
  display.setTextSize(1);
  int16_t textX, textY;
  uint16_t textW, textH;
  display.getTextBounds(message, 0, 0, &textX, &textY, &textW, &textH);
  display.setCursor(x + (w - textW) / 2, y + 4);
  display.print(message);
}

void drawSnake() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  // Fizycznie zolty pas OLED (0..15). Obydwa napisy CALKOWICIE w pasie.
  display.setCursor(0, 3);
  display.print("SNAKE");
  display.setCursor(75, 3);
  display.print("PKT:");
  display.print(snakeScore);
  display.drawFastHLine(0, 15, 128, SSD1306_WHITE);

  // Wszystkie pola gry sa niebieskie (y=16..63). Bez marginesow.
  for (uint16_t i = 0; i < snakeLength; ++i) {
    const uint8_t px = snakeBody[i].x * SNAKE_CELL;
    const uint8_t py = SNAKE_TOP + snakeBody[i].y * SNAKE_CELL;
    if (i == 0) {
      display.fillRect(px, py, 4, 4, SSD1306_WHITE); // glowa
    } else {
      display.fillRect(px, py, 3, 3, SSD1306_WHITE); // segmenty
    }
  }
  if (snakeRunning || snakePaused) {
    // Jedzenie: kwadrat z pustym srodkiem, odroznialny od weza.
    display.drawRect(snakeFood.x * 4, SNAKE_TOP + snakeFood.y * 4,
                     4, 4, SSD1306_WHITE);
  }
  if (snakeWon) snakeMessage("WYGRANA!");
  else if (snakeGameOver) snakeMessage("KONIEC");
  else if (!snakeRunning) snakeMessage("B5 START");
  else if (snakePaused) snakeMessage("PAUZA");
  display.display();
}

// ============================================================
// PASJANS KLONDIKE (DOBIERANIE PO 1 KARCIE, 52 KARTY)
// 7 kolumn, 4 stosy docelowe, talon z ponownym rozdaniem.
// Oznaczenia kart: C=trefl, D=karo, H=kier, S=pik.
// Ekran dwukolorowy: zolty naglowek y=0..15, cala reszta niebieska.
// ============================================================
constexpr uint8_t SOL_STOCK = 0;
constexpr uint8_t SOL_WASTE = 1;
constexpr uint8_t SOL_FIRST_FOUNDATION = 2;
constexpr uint8_t SOL_FIRST_TABLEAU = 6;
constexpr uint8_t SOL_PILE_COUNT = 13;
constexpr uint8_t SOL_CARD_W = 16;
constexpr uint8_t SOL_CARD_H = 11;
constexpr uint8_t SOL_TABLEAU_Y = 38;

struct SolColumn {
  uint8_t cards[52];
  uint8_t count = 0;
  uint8_t faceUpFrom = 0;  // Wszystkie karty od tego indeksu sa odkryte.
};
SolColumn solColumns[7];
uint8_t solStock[52], solWaste[52];
uint8_t solStockCount = 0, solWasteCount = 0;
uint8_t solFoundationRank[4] = {0, 0, 0, 0};
uint8_t solCursor = SOL_STOCK;
uint8_t solRun = 1;       // Liczba kart z konca kolumny do przeniesienia.
int8_t solSource = -1;    // -1 = nic nie trzymamy.
uint8_t solHeldCount = 0;
bool solStarted = false, solWon = false;
uint16_t solMoves = 0;

uint8_t solRank(uint8_t card) { return card % 13 + 1; }
uint8_t solSuit(uint8_t card) { return card / 13; } // C D H S
bool solRed(uint8_t card) {
  const uint8_t suit = solSuit(card);
  return suit == 1 || suit == 2;
}
char solRankChar(uint8_t card) {
  const uint8_t r = solRank(card);
  if (r == 1) return 'A';
  if (r <= 9) return '0' + r;
  if (r == 10) return 'T';
  if (r == 11) return 'J';
  if (r == 12) return 'Q';
  return 'K';
}
char solSuitChar(uint8_t card) {
  const char suits[] = "CDHS";
  return suits[solSuit(card)];
}
void solCardText(uint8_t card, char *out) {
  out[0] = solRankChar(card);
  out[1] = solSuitChar(card);
  out[2] = '\0';
}

void newSolitaire() {
  uint8_t deck[52];
  for (uint8_t i = 0; i < 52; ++i) deck[i] = i;
  for (int i = 51; i > 0; --i) {
    const uint8_t j = random(i + 1);
    const uint8_t tmp = deck[i];
    deck[i] = deck[j];
    deck[j] = tmp;
  }
  for (uint8_t c = 0; c < 7; ++c) {
    solColumns[c].count = c + 1;
    solColumns[c].faceUpFrom = c;
  }
  uint8_t n = 0;
  // Rozkladamy karty w siedmiu kolumnach: 1,2,...,7.
  for (uint8_t c = 0; c < 7; ++c) {
    for (uint8_t i = 0; i <= c; ++i) {
      solColumns[c].cards[i] = deck[n++];
    }
  }
  solStockCount = 0;
  while (n < 52) solStock[solStockCount++] = deck[n++];
  solWasteCount = 0;
  memset(solFoundationRank, 0, sizeof(solFoundationRank));
  solCursor = SOL_STOCK;
  solRun = 1;
  solSource = -1;
  solHeldCount = 0;
  solStarted = true;
  solWon = false;
  solMoves = 0;
}

// Ruchome sa tylko kolejne odkryte karty o malejacych rangach
// i naprzemiennych kolorach. Zwracamy najdluzszy poprawny ciag
// zakonczony karta na wierzchu wybranej kolumny.
uint8_t solMaxRun(uint8_t c) {
  const SolColumn &col = solColumns[c];
  if (!col.count) return 0;
  uint8_t count = 1;
  for (int i = col.count - 1; i > col.faceUpFrom; --i) {
    const uint8_t upper = col.cards[i - 1];
    const uint8_t lower = col.cards[i];
    if (solRank(upper) != solRank(lower) + 1 ||
        solRed(upper) == solRed(lower)) break;
    ++count;
  }
  return count;
}

void solSetCursor(int8_t change) {
  solCursor = (solCursor + SOL_PILE_COUNT + change) % SOL_PILE_COUNT;
  if (solSource < 0) solRun = 1;
}

void solDrawStock() {
  if (!solStarted || solWon || solSource >= 0) return;
  if (solStockCount) {
    solWaste[solWasteCount++] = solStock[--solStockCount];
    ++solMoves;
  } else if (solWasteCount) {
    // Wyczerpany talon: przekladamy caly stos kart odrzuconych
    // na spod, w odwrotnej kolejnosci (nie tasujemy).
    uint8_t n = 0;
    while (solWasteCount) solStock[n++] = solWaste[--solWasteCount];
    solStockCount = n;
    ++solMoves;
  }
}

bool solTopCard(uint8_t pile, uint8_t &card) {
  if (pile == SOL_WASTE) {
    if (!solWasteCount) return false;
    card = solWaste[solWasteCount - 1];
    return true;
  }
  if (pile >= SOL_FIRST_FOUNDATION && pile < SOL_FIRST_TABLEAU) {
    const uint8_t suit = pile - SOL_FIRST_FOUNDATION;
    if (!solFoundationRank[suit]) return false;
    card = suit * 13 + solFoundationRank[suit] - 1;
    return true;
  }
  if (pile >= SOL_FIRST_TABLEAU && pile < SOL_PILE_COUNT) {
    const SolColumn &col = solColumns[pile - SOL_FIRST_TABLEAU];
    if (!col.count) return false;
    card = col.cards[col.count - 1];
    return true;
  }
  return false;
}

// Dla kolumny zwraca pierwsza karte (spod) zabieranego ciagu.
uint8_t solHeldFirst() {
  if (solSource >= SOL_FIRST_TABLEAU) {
    const SolColumn &c = solColumns[solSource - SOL_FIRST_TABLEAU];
    return c.cards[c.count - solHeldCount];
  }
  uint8_t card = 0;
  solTopCard(solSource, card);
  return card;
}

void solCancel() {
  solSource = -1;
  solHeldCount = 0;
  solRun = 1;
}

void solCheckWon() {
  solWon = true;
  for (uint8_t s = 0; s < 4; ++s) {
    if (solFoundationRank[s] != 13) solWon = false;
  }
}

void solRemoveFromSource() {
  if (solSource == SOL_WASTE) {
    --solWasteCount;
  } else if (solSource >= SOL_FIRST_FOUNDATION &&
             solSource < SOL_FIRST_TABLEAU) {
    --solFoundationRank[solSource - SOL_FIRST_FOUNDATION];
  } else if (solSource >= SOL_FIRST_TABLEAU) {
    SolColumn &col = solColumns[solSource - SOL_FIRST_TABLEAU];
    col.count -= solHeldCount;
    if (!col.count) {
      col.faceUpFrom = 0;
    } else if (col.faceUpFrom >= col.count) {
      // Odkrywamy nowa najwyzsza karte po zabraniu odkrytego ciagu.
      col.faceUpFrom = col.count - 1;
    }
  }
}

bool solPlace(uint8_t target) {
  if (solSource < 0 || target == solSource || solWon) return false;
  const uint8_t first = solHeldFirst();
  if (target >= SOL_FIRST_FOUNDATION && target < SOL_FIRST_TABLEAU) {
    const uint8_t suit = target - SOL_FIRST_FOUNDATION;
    if (solHeldCount != 1 || solSuit(first) != suit ||
        solRank(first) != solFoundationRank[suit] + 1) return false;
    solRemoveFromSource();
    ++solFoundationRank[suit];
  } else if (target >= SOL_FIRST_TABLEAU && target < SOL_PILE_COUNT) {
    SolColumn &dest = solColumns[target - SOL_FIRST_TABLEAU];
    if (!dest.count) {
      if (solRank(first) != 13) return false; // Tylko krol na puste pole.
    } else {
      const uint8_t top = dest.cards[dest.count - 1];
      if (solRank(top) != solRank(first) + 1 ||
          solRed(top) == solRed(first)) return false;
    }
    // Ochrona przed blednym wyjsciem poza tablice.
    if (dest.count + solHeldCount > 52) return false;
    uint8_t moved[52];
    if (solSource >= SOL_FIRST_TABLEAU) {
      const SolColumn &src = solColumns[solSource - SOL_FIRST_TABLEAU];
      for (uint8_t i = 0; i < solHeldCount; ++i)
        moved[i] = src.cards[src.count - solHeldCount + i];
    } else {
      moved[0] = first;
    }
    solRemoveFromSource();
    for (uint8_t i = 0; i < solHeldCount; ++i)
      dest.cards[dest.count++] = moved[i];
  } else {
    return false;
  }
  ++solMoves;
  solCancel();
  solCheckWon();
  return true;
}

void solSelectOrPlace() {
  if (!solStarted) { newSolitaire(); return; }
  if (solWon) return;
  if (solSource >= 0) {
    if (solCursor == solSource) solCancel();
    else solPlace(solCursor);
    return;
  }
  if (solCursor == SOL_STOCK) { solDrawStock(); return; }
  uint8_t card;
  if (!solTopCard(solCursor, card)) return;
  solSource = solCursor;
  solHeldCount = (solSource >= SOL_FIRST_TABLEAU)
               ? min(solRun, solMaxRun(solSource - SOL_FIRST_TABLEAU)) : 1;
}

void solButton4() {
  if (!solStarted || solWon) return;
  if (solSource >= 0) { solCancel(); return; }
  if (solCursor >= SOL_FIRST_TABLEAU) {
    const uint8_t maxRun = solMaxRun(solCursor - SOL_FIRST_TABLEAU);
    if (maxRun) solRun = solRun >= maxRun ? 1 : solRun + 1;
  } else {
    solDrawStock();
  }
}

void solAutoFoundation() {
  if (!solStarted || solWon) return;
  if (solSource >= 0 && solHeldCount != 1) return;
  uint8_t source = solSource >= 0 ? solSource : solCursor;
  uint8_t card;
  if (!solTopCard(source, card)) return;
  const uint8_t target = SOL_FIRST_FOUNDATION + solSuit(card);
  if (solSource < 0) {
    solSource = source;
    solHeldCount = 1;
  }
  if (!solPlace(target)) {
    // Automatyczne przeniesienie nie powiodlo sie: nic nie zabieramy.
    if (source == solCursor) solCancel();
  }
}

// Male, czytelne etykiety kart (np. AC = as trefl, TH = 10 kier).
void solMiniCard(int16_t x, int16_t y, uint8_t card, bool selected) {
  // Zamaluj poprzednia karte, aby stosy nie nakladaly tekstu na tekst.
  display.fillRect(x, y, SOL_CARD_W, SOL_CARD_H, SSD1306_BLACK);
  display.drawRect(x, y, SOL_CARD_W, SOL_CARD_H, SSD1306_WHITE);
  // Na monochromatycznym OLED mozemy oznaczyc czerwone kolory
  // dodatkowa kropka: karo/kier maja marker w prawym dolnym rogu.
  char label[3];
  solCardText(card, label);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(x + 2, y + 2);
  display.print(label);
  if (solRed(card)) display.drawPixel(x + 14, y + 9, SSD1306_WHITE);
  if (selected) display.drawFastHLine(x, y + 10, SOL_CARD_W, SSD1306_WHITE);
}

void solDrawPile(int16_t x, uint8_t pile) {
  const int16_t y = 17;
  uint8_t card;
  const bool hasCard = solTopCard(pile, card);
  if (pile == SOL_STOCK) {
    display.drawRect(x, y, SOL_CARD_W, SOL_CARD_H, SSD1306_WHITE);
    if (solStockCount) {
      display.drawLine(x + 3, y + 2, x + 12, y + 8, SSD1306_WHITE);
      display.drawLine(x + 3, y + 8, x + 12, y + 2, SSD1306_WHITE);
    } else {
      display.setCursor(x + 5, y + 2); display.print("O");
    }
  } else if (hasCard) {
    solMiniCard(x, y, card, false);
  } else {
    display.drawRect(x, y, SOL_CARD_W, SOL_CARD_H, SSD1306_WHITE);
    if (pile >= SOL_FIRST_FOUNDATION) {
      display.setCursor(x + 5, y + 2);
      display.print(solSuitChar((pile - SOL_FIRST_FOUNDATION) * 13));
    }
  }
  if (solCursor == pile) display.drawFastHLine(x, 29, SOL_CARD_W, SSD1306_WHITE);
  if (solSource == pile) display.fillRect(x + 6, 15, 4, 2, SSD1306_WHITE);
}

void solDrawTableau(uint8_t colNumber) {
  const SolColumn &col = solColumns[colNumber];
  const int16_t x = colNumber * 18 + 1;
  const uint8_t pile = SOL_FIRST_TABLEAU + colNumber;
  display.setCursor(x + 5, 30);
  display.print(colNumber + 1);
  if (solCursor == pile) display.drawFastHLine(x, 37, SOL_CARD_W, SSD1306_WHITE);
  if (solSource == pile) display.fillRect(x + 11, 30, 3, 3, SSD1306_WHITE);
  if (!col.count) {
    display.drawRect(x, SOL_TABLEAU_Y, SOL_CARD_W, SOL_CARD_H, SSD1306_WHITE);
    return;
  }
  // Ograniczona wysokosc OLED: upakowanie stosu gwarantuje,
  // ze gorna (grana) karta zawsze bedzie widoczna przy dole ekranu.
  // Zakryte karty sa poziomymi kreskami, odkryte - minikartami.
  const uint8_t gaps = col.count - 1;
  const int maxTopY = 64 - SOL_CARD_H;
  uint8_t step = 3;
  if (gaps > 0 && gaps * step > maxTopY - SOL_TABLEAU_Y)
    step = max(1, (maxTopY - SOL_TABLEAU_Y) / gaps);
  // Bardzo wysokie stosy: wyswietlamy tylko ostatni fragment,
  // nie tracac mozliwosci przenoszenia kart z wybranego ciagu.
  const uint8_t shown = min(col.count, (uint8_t)(1 + (maxTopY - SOL_TABLEAU_Y) / step));
  const uint8_t first = col.count - shown;
  for (uint8_t i = first; i < col.count; ++i) {
    const int16_t y = SOL_TABLEAU_Y + (i - first) * step;
    if (i < col.faceUpFrom) {
      display.fillRect(x, y, SOL_CARD_W, SOL_CARD_H, SSD1306_BLACK);
      display.drawRect(x, y, SOL_CARD_W, SOL_CARD_H, SSD1306_WHITE);
      display.drawFastHLine(x + 3, y + 2, 10, SSD1306_WHITE);
    } else {
      solMiniCard(x, y, col.cards[i], false);
    }
  }
  // Liczba kart w wybranym ciagu (B4) i ich poczatek pokazywane
  // w zoltym naglowku, zeby male nakladajace sie karty nie przeszkadzaly.
}

void drawSolitaire() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 3);
  display.print("PASJANS");
  display.setCursor(51, 3);
  if (!solStarted) {
    display.print("B5 START");
  } else if (solWon) {
    display.print("WYGRANA!");
  } else if (solSource >= 0) {
    char text[3];
    solCardText(solHeldFirst(), text);
    display.print("TRZ:");
    display.print(text);
    if (solHeldCount > 1) {
      display.print("x");
      display.print(solHeldCount);
    }
  } else if (solCursor >= SOL_FIRST_TABLEAU &&
             solColumns[solCursor - SOL_FIRST_TABLEAU].count) {
    const SolColumn &col = solColumns[solCursor - SOL_FIRST_TABLEAU];
    char text[3];
    solCardText(col.cards[col.count - solRun], text);
    display.print("x");
    display.print(solRun);
    display.print(":");
    display.print(text);
  } else {
    display.print("B3 WYB");
  }
  // Stos dobierania, stos odkryty i cztery domowe.
  solDrawPile(1, SOL_STOCK);
  solDrawPile(19, SOL_WASTE);
  for (uint8_t i = 0; i < 4; ++i)
    solDrawPile(55 + 18 * i, SOL_FIRST_FOUNDATION + i);
  // Kolumny z kartami: 7 x 18 = 126 pikseli.
  for (uint8_t i = 0; i < 7; ++i) solDrawTableau(i);
  if (!solStarted || solWon) {
    display.fillRect(30, 38, 74, 20, SSD1306_BLACK);
    display.drawRect(30, 38, 74, 20, SSD1306_WHITE);
    display.setCursor(solWon ? 42 : 39, 44);
    display.print(solWon ? "WYGRANA!" : "B5 START");
  }
  display.display();
}

// ============================================================
// STEROWANIE I PRZELACZANIE TRYBOW
// ============================================================
void nextMode() {
  if (mode == CLOCK_MODE) mode = CALENDAR_MODE;
  else if (mode == CALENDAR_MODE) mode = TETRIS_MODE;
  else if (mode == TETRIS_MODE) mode = SNAKE_MODE;
  else if (mode == SNAKE_MODE) mode = SOLITAIRE_MODE;
  else mode = CLOCK_MODE;
  // Gry pozostaja zamrozone podczas korzystania z innych trybow.
  lastFall = millis();
  lastSoftDrop = millis();
  lastSnakeStep = millis();
}

void handlePress(uint8_t b) {
  if (b == 5) { nextMode(); return; }

  if (mode == CLOCK_MODE) {
    switch (b) {
      case 0: hours = (hours + 1) % 24; break;
      case 1: hours = (hours + 23) % 24; break;
      case 2: minutes = (minutes + 1) % 60; break;
      case 3: minutes = (minutes + 59) % 60; break;
      case 4: seconds = 0; lastTick = millis(); break;
    }
  } else if (mode == CALENDAR_MODE) {
    switch (b) {
      case 0: nextDay(); break;
      case 1: nextMonth(); break;
      case 2: changeYear(1); break;
      case 3: changeYear(-1); break;
      case 4: changeYear(5); break;
    }
  } else if (mode == TETRIS_MODE) {
    switch (b) {
      case 0: movePiece(-1); break;
      case 1: movePiece(1); break;
      case 2: rotatePiece(); break;
      case 3: if (playing && !paused) {
                fallOneRow();
                lastSoftDrop = millis();
              }
              break;
      // B5 (b==4) jest obslugiwany dopiero przy PUSZCZENIU,
      // aby odroznic krotkie klikniecie od dlugiego przytrzymania.
    }
  } else if (mode == SOLITAIRE_MODE) {
    switch (b) {
      case 0: solSetCursor(-1); break;
      case 1: solSetCursor(+1); break;
      case 2: solSelectOrPlace(); break;
      case 3: solButton4(); break;
      // B5: automat do stosu domowego, dlugie: nowa rozgrywka.
    }
  } else if (mode == SNAKE_MODE) {
    switch (b) {
      case 0: snakeTurn(-1); break;  // Skret wzgledem kierunku jazdy
      case 1: snakeTurn(1);  break;
      // B3 przyspiesza, dopoki jest trzymany: updateSnake().
      // B4 celowo nic nie robi.
      // B5: klik po puszczeniu; przytrzymanie: restart.
    }
  }
}

void handleRepeat(uint8_t b) {
  if (mode == CLOCK_MODE) {
    if (b <= 3) handlePress(b);
  } else if (mode == CALENDAR_MODE) {
    if (b <= 4) handlePress(b);
  } else if (mode == SOLITAIRE_MODE) {
    if (b == 0) solSetCursor(-1);
    if (b == 1) solSetCursor(+1);
  } else if (mode == TETRIS_MODE) {
    if (b == 0) movePiece(-1);
    if (b == 1) movePiece(1);
    // B4 ma wlasny szybki zegar opadania w updateTetris().
  }
}

void shortSnakeButton5() {
  if (!snakeRunning) {
    newSnakeGame(); // Start lub nowa gra po porazce.
  } else {
    snakePaused = !snakePaused;
    lastSnakeStep = millis(); // Pelny odstep po wznowieniu.
  }
}

void shortSolitaireButton5() {
  if (!solStarted) newSolitaire();
  else solAutoFoundation();
}

void shortTetrisButton5() {
  if (!playing) {
    newGame();               // Start lub nowa gra po koncu.
  } else {
    paused = !paused;        // Pauza / wznowienie.
    lastFall = millis();     // Po wznowieniu pelny okres na kolejny spadek.
    lastSoftDrop = millis();
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Nie udalo sie uruchomic OLED!");
    while (true) delay(100);
  }
  display.setTextColor(SSD1306_WHITE);
  for (uint8_t i = 0; i < 6; ++i) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    buttons[i].raw = digitalRead(BUTTON_PINS[i]);
    buttons[i].stable = buttons[i].raw;
    buttons[i].rawChangedAt = millis();
  }
  randomSeed(micros());
  lastTick = millis();
}

void loop() {
  const unsigned long now = millis();
  while (now - lastTick >= 1000) {
    lastTick += 1000;
    tickSecond();
  }

  readButtons();
  // B5 w grach: restart na przytrzymanie, klik dopiero po puszczeniu.
  // Przycisk zmiany trybu B6 dziala na pojedyncze nacisniecie.
  for (uint8_t i = 0; i < 6; ++i) {
    if (i == 4 && (mode == TETRIS_MODE || mode == SNAKE_MODE || mode == SOLITAIRE_MODE)) {
      if (mode == TETRIS_MODE) {
        if (buttons[i].longEvent) newGame();
        if (buttons[i].releaseEvent && !buttons[i].longSent) shortTetrisButton5();
      } else if (mode == SNAKE_MODE) {
        if (buttons[i].longEvent) newSnakeGame();
        if (buttons[i].releaseEvent && !buttons[i].longSent) shortSnakeButton5();
      } else {
        if (buttons[i].longEvent) newSolitaire();
        if (buttons[i].releaseEvent && !buttons[i].longSent) shortSolitaireButton5();
      }
      continue;
    }
    if (buttons[i].pressEvent) handlePress(i);
    if (buttons[i].repeatEvent) handleRepeat(i);
  }

  if (mode == TETRIS_MODE) updateTetris();
  else if (mode == SNAKE_MODE) updateSnake();

  // Okolo 20 FPS. Nie wysylamy 1024 bajtow do OLED bez przerwy,
  // wiec odczyt przyciskow i szybkie opadanie pozostaja plynne.
  static unsigned long lastDraw = 0;
  if (millis() - lastDraw >= 50) {
    lastDraw = millis();
    if (mode == CLOCK_MODE) drawClock();
    else if (mode == CALENDAR_MODE) drawCalendar();
    else if (mode == TETRIS_MODE) drawTetris();
    else if (mode == SNAKE_MODE) drawSnake();
    else drawSolitaire();
  }
  delay(1);
}
