#include <U8g2lib.h>
#include "FS.h"
#include <LittleFS.h>
#include <time.h>
#include <Keypad.h>

/** Init LCD */

// SCK = 18, MOSI = 23 are the ESP32 VSPI defaults
// CS  = 5 , RST = 4
U8G2_ST7920_128X64_F_HW_SPI u8g2(U8G2_R0, /* CS=*/5, /* reset=*/4);


/** Init keypad */

const uint8_t ROWS = 4;
const uint8_t COLS = 4;
char keys[ROWS][COLS] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};

uint8_t col_pins[COLS] = { 27, 14, 12, 13 }; // C1, C2, C3, C4
uint8_t row_pins[ROWS] = { 32, 33, 25, 26 }; // R1, R2, R3, R4

Keypad keypad = Keypad(makeKeymap(keys), row_pins, col_pins, ROWS, COLS);
uint8_t KEYMAP[256] = {0};
char KEYSTR[16 + 1] = "0123456789ABCD*#";
int keystate[16] = {0};

void keypadEvent(KeypadEvent key) {
  switch (keypad.getState()){
    case PRESSED:
      keystate[KEYMAP[key]] = 1;
      break;

    case RELEASED:
      keystate[KEYMAP[key]] = 0;
      break;

    case HOLD:
      break;
  }
}


uint8_t ram[4096] = { // sprites 0-F
	0xF0, 0x90, 0x90, 0x90, 0xF0,
	0x20, 0x60, 0x20, 0x20, 0x70,
	0xF0, 0x10, 0xF0, 0x80, 0xF0,
	0xF0, 0x10, 0xF0, 0x10, 0xF0,
	0x90, 0x90, 0xF0, 0x10, 0x10,
	0xF0, 0x80, 0xF0, 0x10, 0xF0,
	0xF0, 0x80, 0xF0, 0x90, 0xF0,
	0xF0, 0x10, 0x20, 0x40, 0x40,
	0xF0, 0x90, 0xF0, 0x90, 0xF0,
	0xF0, 0x90, 0xF0, 0x10, 0xF0,
	0xF0, 0x90, 0xF0, 0x90, 0x90,
	0xE0, 0x90, 0xE0, 0x90, 0xE0,
	0xF0, 0x80, 0x80, 0x80, 0xF0,
	0xE0, 0x90, 0x90, 0x90, 0xE0,
	0xF0, 0x80, 0xF0, 0x80, 0xF0,
	0xF0, 0x80, 0xF0, 0x80, 0x80
};


int load_rom(String path) {
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS failed.");
    return false;
  }
  Serial.println("LittleFS mounted.");

  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println("Could not open file.");
    return false;
  }

  for (int i = 0x200; i < 4096 and file.available(); i++) {
    ram[i] = file.read();
  }
  file.close();

  LittleFS.end();

  Serial.print(path);
  Serial.println(" loaded");
  return true;
}


#define W(hextet) (hextet >> 12)
#define X(hextet) ((hextet & 0x0F00) >> 8)
#define Y(hextet) ((hextet & 0x00F0) >> 4)
#define Z(hextet) (hextet & 0x000F)
#define NN(hextet) (hextet & 0x00FF)
#define NNN(hextet) (hextet & 0x0FFF)

enum REGISTER {
	V0, V1, V2, V3, V4, V5, V6, V7, V8, V9, VA, VB, VC, VD, VE, VF
};
uint8_t reg[16];
uint16_t VI;

struct {
	uint16_t pc, sp;
	uint8_t delay, sound;
} cpu;

const int P_HAPTIC = 22;
const int P_GAME = 34;
const int P_BUZZER = 2;

void setup(void) {
  Serial.begin(115200);
  Serial.println("BEGIN");
  while (!Serial);

  pinMode(P_GAME, INPUT);
  pinMode(P_HAPTIC, OUTPUT);
  pinMode(P_BUZZER, OUTPUT);

  u8g2.begin();
  u8g2.setFont(u8g2_font_busdisplay8x5_tr);

  keypad.addEventListener(keypadEvent);

	// init keymap
	for (int i = 0; i < 16; i++) {
		KEYMAP[KEYSTR[i]] = i;
	}
}

const int NUMBER_OF_GAMES = 6;

char *game_names[NUMBER_OF_GAMES + 1] = { // 0 index heart_monitor
  "0. Heart Monitor",
  "1. DX Ball",
  "2. Snake",
  "3. Tetris",
  "4. Ping-Pong",
  "5. Bomber",
  "6. Cavern",
};

char *game_filenames[NUMBER_OF_GAMES + 1] = {
  "/heart_monitor.ch8",
  "/brix.ch8",
  "/snake.ch8",
  "/tetris.ch8",
  "/pong.ch8",
  "/blitz.ch8",
  "/cavern.ch8",
};

int game_inst_per_frame[NUMBER_OF_GAMES + 1] = {
  20,
  15,
  20,
  30,
  20,
  20,
  15,
};

void loop(void) {
  // reset globals
  memset(keystate, 0, sizeof(keystate));
  memset(reg, 0, sizeof(reg));

  // menu
  u8g2.clearBuffer();
  for (int i = 1; i <= NUMBER_OF_GAMES; i++) {
    u8g2.drawStr(3, 10*i, game_names[i]);
  }
  u8g2.sendBuffer();

  char key = keypad.getKey();
  while (key == NO_KEY) {
    delay(10);
    key = keypad.getKey();
  }
  if (key < '1' or key > '6') return;

	int inst_per_frame = game_inst_per_frame[key - '0'];
  if (!load_rom(game_filenames[key - '0'])) {
    return;
  }

	uint8_t pixels[32][64] = {0};

  int ms_per_frame = 1000/60.0; // 60 FPS
  int frame_start_time = millis();

	cpu.pc = 0x200;
	cpu.sp = 0x200;

	int inst_count = 0;

	bool quit = false;
  bool buzzer = false;

	for (; !quit; ) {
    int current_time = millis();
    int elapsed_ms = current_time - frame_start_time;
		if (elapsed_ms >= ms_per_frame) {

			if (cpu.delay > 0) cpu.delay--;
			if (cpu.sound > 0) cpu.sound--;
			inst_count = 0;

      u8g2.clearBuffer();
      for (int y = 0; y < 64; y += 2) {
        for (int x = 0; x < 128; x += 2) if (pixels[y/2][x/2]) {
          u8g2.drawPixel(x,   y);
          u8g2.drawPixel(x+1, y);
          u8g2.drawPixel(x,   y+1);
          u8g2.drawPixel(x+1, y+1);
        }
      }
      u8g2.sendBuffer();

      frame_start_time = millis();
		}

    if (cpu.sound > 1) {
      if (!buzzer) {
        tone(P_BUZZER, 360);
        buzzer = true;
      }
      digitalWrite(P_HAPTIC, HIGH);
    } else {
      if (buzzer) {
        tone(P_BUZZER, 0);
        buzzer = false;
      }
      digitalWrite(P_HAPTIC, LOW);
    }

    char key = keypad.getKey();
    if (key == 'D') return;

    if (inst_count >= inst_per_frame) {
      continue;
      // delay(ms_per_frame - elapsed_ms);
    }

		uint16_t inst = (ram[cpu.pc] << 8) + ram[cpu.pc+1];
		uint8_t w = W(inst);
		uint8_t VX = X(inst);
		uint8_t VY = Y(inst);
		uint8_t z = Z(inst);
		uint8_t nn = NN(inst);
		uint16_t nnn = NNN(inst);

		if (inst == 0x00E0) {
			for (int i = 0; i < 32; i++) {
				for (int j = 0; j < 64; j++) {
					pixels[i][j] = 0;
				}
			}
		}

		else if (inst == 0x00EE) {
			cpu.pc = *(uint16_t*)(ram + cpu.sp) - 2;
			cpu.sp += 2;
		}

		else switch (w) {
		case 0:
			cpu.sp -= 2;
			*(uint16_t*)(ram + cpu.sp) = cpu.pc + 2;
			cpu.pc = nnn - 2;
			break;

		case 1:
			cpu.pc = nnn - 2;
			break;

		case 2:
			cpu.sp -= 2;
			*(uint16_t*)(ram + cpu.sp) = cpu.pc + 2;
			cpu.pc = nnn - 2;
			break;

		case 3:
			if (reg[VX] == nn)
				cpu.pc += 2;
			break;

		case 4:
			if (reg[VX] != nn)
				cpu.pc += 2;
			break;

		case 5:
			if (reg[VX] == reg[VY])
				cpu.pc += 2;
			break;

		case 6:
			reg[VX] = nn;
			break;

		case 7:
			reg[VX] += nn;
			break;

		case 8:
			switch (z) {
			case 0:
				reg[VX] = reg[VY];
				break;
			case 1:
				reg[VX] |= reg[VY];
				break;
			case 2:
				reg[VX] &= reg[VY];
				break;
			case 3:
				reg[VX] ^= reg[VY];
				break;
			case 4: {
				uint8_t sum = reg[VX] + reg[VY];
				reg[VF] = sum < reg[VX] || sum < reg[VY];
				reg[VX] = sum;
      } break;
			case 5:
				reg[VF] = reg[VX] >= reg[VY];
				reg[VX] = reg[VX] - reg[VY];
				break;
			case 6:
				reg[VX] = reg[VY] >> 1;
				reg[VF] = reg[VY] & 0b00000001;
				break;
			case 7:
				reg[VF] = reg[VY] >= reg[VX];
				reg[VX] = reg[VY] - reg[VX];
				break;
			case 0xE:
				reg[VX] = reg[VY] << 1;
				reg[VF] = reg[VY] & 0b10000000;
				break;
			} break;

		case 9:
			if (reg[VX] != reg[VY])
				cpu.pc += 2;
			break;

		case 0xA:
			VI = nnn;
			break;

		case 0xB:
			cpu.pc = nnn + reg[V0] - 2;
			break;

		case 0xC:
			srand(time(NULL));
			reg[VX] = (rand() % 0xFF) & nn;
			break;

		case 0xD:
			reg[VX] %= 64;
			reg[VY] %= 32;
			reg[VF] = 0;
			for (int i = 0; i < z && reg[VY] + i < 32; i++) {
				for (int j = 0; j < 8 && reg[VX] + j < 64; j++) {
					if (1 & (ram[VI + i] >> (7 - j))) {
						if (pixels[reg[VY] + i][reg[VX] + j]) {
							reg[VF] = 0x01;
							pixels[reg[VY] + i][reg[VX] + j] = 0;
						} else {
							pixels[reg[VY] + i][reg[VX] + j] = 1;
						}
					}
				}
			}
			break;

		case 0xE:
			switch (nn) {
			case 0x9E:
				if (keystate[reg[VX]])
					cpu.pc += 2;
				break;
			case 0xA1:
				if (!keystate[reg[VX]])
					cpu.pc += 2;
				break;
			}
			break;

		case 0xF:
			switch (nn) {
			case 0x07:
				reg[VX] = cpu.delay;
				break;

			case 0x0A: {
				// TODO: input
				int f = 1;
				for (int i = 0; i < 16; i++) {
					if (keystate[i]) {
						reg[VX] = i;
						f = 0;
						break;
					}
				}
				if (!f) break;
        char key;
        while ((key = keypad.getKey()) == NO_KEY) delay(10);
        reg[VX] = KEYMAP[key];
      } break;

			case 0x15:
				cpu.delay = reg[VX];
				break;

			case 0x18:
				cpu.sound = reg[VX];
				break;

			case 0x1E:
				VI += reg[VX];
				break;

			case 0x29:
				VI = reg[VX] * 5;
				break;

			case 0x33:
				ram[VI] = reg[VX] / 100;
				ram[VI+1] = (reg[VX] / 10) % 10;
				ram[VI+2] = (reg[VX] % 100) % 10;
				break;

			case 0x55:
				for (int i = V0; i <= VX; i++)
					ram[VI + i] = reg[i];
				VI += VX + 1;
				break;

			case 0x65:
				for (int i = V0; i <= VX; i++)
					reg[i] = ram[VI + i];
				VI += VX + 1;
				break;
			}
			break;

		default:
			Serial.println("INVALID INSTRUCTION");
			return;
			break;
		}

		cpu.pc += 2;
		inst_count += 1;
	}
}
