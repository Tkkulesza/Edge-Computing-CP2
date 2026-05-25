/*
 * ============================================================
 *  Vinheria Agnello — Sistema de Monitoramento Ambiental CP2
 *  Equipe: Debuggers
 *
 *  Versao corrigida para Arduino/Wokwi.
 *  Observacao: este arquivo estava sem o cabecalho com bibliotecas,
 *  pinos e constantes; por isso o compilador nao reconhecia DHT,
 *  LCD, RTC, EEPROM e os nomes dos pinos.
 * ============================================================
 */
 
// -------------------- Bibliotecas usadas ---------------------
#include <Wire.h>                 // Comunicacao I2C: LCD e RTC
#include <EEPROM.h>               // Memoria interna para salvar configs/logs
#include <LiquidCrystal_I2C.h>    // LCD 16x2 com modulo I2C
#include <RTClib.h>               // RTC DS1307 e classe DateTime
#include <DHT.h>                  // Sensor DHT11/DHT22
 
// -------------------- Sensor DHT -----------------------------
#define DHT_PIN   12              // Pino de dados do DHT
#define DHT_TYPE  DHT11           // Troque para DHT22 se usar DHT22
 
// -------------------- Pinos do projeto -----------------------
#define LDR_PIN      A0           // Sensor de luminosidade LDR
#define LED_GREEN    3            // LED verde
#define LED_YELLOW   4            // LED amarelo
#define LED_RED      5            // LED vermelho
#define BUZZER_PIN   6            // Buzzer ativo/passivo
#define BTN_MENU     7            // Botao MENU
#define BTN_UP       8            // Botao UP
#define BTN_DOWN     9            // Botao DOWN
 
// -------------------- Enderecos da EEPROM --------------------
#define MAGIC_BYTE      0xDA      // Identificador da versao das configuracoes
#define ADDR_MAGIC      0         // Onde fica salvo o MAGIC_BYTE
#define ADDR_CONFIG     2         // Inicio da struct Config
#define ADDR_LOG_HEAD   52        // Cabeca do log circular
#define ADDR_LOG_COUNT  53        // Quantidade de logs salvos
#define ADDR_LOG_START  54        // Inicio dos logs
#define LOG_SIZE        12        // Tamanho de cada registro
#define MAX_LOGS        48        // Quantidade maxima de registros
 
// -------------------- Intervalos de atualizacao --------------
#define T_DHT         2000UL      // Leitura do DHT a cada 2 segundos
#define T_LDR         1000UL      // Leitura do LDR a cada 1 segundo
#define T_SCREEN       400UL      // Atualizacao do LCD
#define T_LOG        60000UL      // Salva log a cada 60 segundos
#define T_DEBOUNCE     200UL      // Anti-repique dos botoes
#define T_BUZZER_ON   3000UL    // Buzzer fica ligado por 3 segundos quando estiver em perigo
#define T_BUZZER_OFF  3000UL    // Depois fica desligado por 3 segundos antes de repetir
 
// -------------------- Ajustes para simular melhor no Wokwi ---
#define LDR_CAL_TIME     1200UL // Tempo da calibracao automatica do LDR
#define DHT_START_DELAY  1000UL // Tempo inicial para o DHT estabilizar
 
// -------------------- Limites de luminosidade ----------------
#define LIGHT_OK_MAX   50       // 0 a 50%  = OK
#define LIGHT_ALT_MAX  75       // 51 a 75% = atencao; acima disso = critico
 
// -------------------- Limites de temperatura em Celsius ------
#define TEMP_OK_MIN    10.0f    // Faixa ideal minima
#define TEMP_OK_MAX    15.0f    // Faixa ideal maxima
#define TEMP_ALT_MIN    8.0f    // Faixa de atencao minima
#define TEMP_ALT_MAX   18.0f    // Faixa de atencao maxima
 
// -------------------- Limites de umidade ---------------------
#define HUM_OK_MIN     50.0f    // Faixa ideal minima
#define HUM_OK_MAX     70.0f    // Faixa ideal maxima
#define HUM_ALT_MIN    40.0f    // Faixa de atencao minima
#define HUM_ALT_MAX    80.0f    // Faixa de atencao maxima
 
// -------------------- Objetos principais ---------------------
DHT               dht(DHT_PIN, DHT_TYPE);      // Objeto do DHT11
LiquidCrystal_I2C lcd(0x27, 16, 2);            // LCD I2C 16 colunas x 2 linhas
RTC_DS1307        rtc;                         // RTC DS1307
bool              rtcReady = false;            // Indica se o RTC foi encontrado
uint32_t          fakeEpochAtBoot = 0UL; // Horario base usado se nao houver RTC
 
// -------------------- Ajustes do RTC -------------------------
// 1 = ajusta o RTC automaticamente para a hora de compilacao ao iniciar.
// Use 1 para acertar o relogio no teste. Depois, se quiser preservar a hora
// do modulo DS1307 com bateria, troque para 0.
#define AJUSTAR_RTC_NO_UPLOAD 1
 
// 0 = o RTC ja guarda a hora local do computador/Arduino IDE.
// 1 = o RTC guarda UTC e o codigo aplica o fuso cfg.utc na exibicao.
// Para Arduino fisico normalmente deixe 0. Para Wokwi, se precisar, use 1.
#define RTC_GUARDA_UTC 0
 
// -------------------- Configuracoes salvas -------------------
struct Config {
  byte logo;        // 0 = logo desligada | 1 = logo ligada
  byte fahrenheit;  // 0 = Celsius | 1 = Fahrenheit
  byte lang;        // 0 = Ingles | 1 = Portugues
  int  utc;         // Fuso horario configuravel
  int  luzMin;      // Menor leitura usada na calibracao do LDR
  int  luzMax;      // Maior leitura usada na calibracao do LDR
};
Config cfg;
 
// -------------------- Registro salvo na EEPROM ---------------
struct LogEntry {
  uint32_t timestamp; // Horario do registro em Unix time
  int16_t  temp10;    // Temperatura multiplicada por 10
  int16_t  hum10;     // Umidade multiplicada por 10
  uint8_t  light;     // Luminosidade em porcentagem
  uint8_t  status;    // Status geral: 0 OK, 1 atencao, 2 critico
  uint8_t  pad[2];    // Espaco extra para fechar 12 bytes
};
 
// -------------------- Leituras atuais dos sensores -----------
float  tempC     = 0.0f;       // Temperatura atual em Celsius
float  humPct    = 0.0f;       // Umidade atual em porcentagem
int    lightPct  = 0;          // Luminosidade atual em porcentagem
bool   dhtOk     = false;      // Indica se a ultima leitura do DHT foi valida
 
// -------------------- Estados dos parametros -----------------
byte stTemp   = 0;             // Status da temperatura
byte stHum    = 0;             // Status da umidade
byte stLight  = 0;             // Status da luminosidade
byte stGlobal = 0;             // Pior status entre todos os sensores
 
// -------------------- Controle de telas ----------------------
byte curScreen = 0;            // Tela atual mostrada no LCD
#define NUM_SCREENS 11         // Total de telas do menu
 
// -------------------- Temporizadores -------------------------
unsigned long tmDHT      = 0;  // Controle de tempo do DHT
unsigned long tmLDR      = 0;  // Controle de tempo do LDR
unsigned long tmScreen   = 0;  // Controle de tempo do LCD
unsigned long tmLog      = 0;  // Controle de tempo dos logs
unsigned long tmBtnMenu  = 0;  // Debounce do botao MENU
unsigned long tmBtnUp    = 0;  // Debounce do botao UP
unsigned long tmBtnDown  = 0;  // Debounce do botao DOWN
unsigned long tmBuzzer   = 0;  // Controle do tempo do buzzer ligado
bool          buzzerOn   = false;  // Indica se o buzzer esta tocando agora
bool          buzzerPause = false;  // Indica se o buzzer esta no intervalo de silencio
 
// -------------------- Media movel do LDR ---------------------
#define LDR_SAMPLES 10         // Quantidade de amostras usadas na media
int  ldrBuf[LDR_SAMPLES];      // Vetor com as ultimas leituras
byte ldrIdx   = 0;             // Posicao atual dentro do vetor
bool ldrFull  = false;         // Indica se o vetor ja encheu pelo menos uma vez
 
// -------------------- Caracteres personalizados do LCD -------
byte icThermo[8] = { B00100,B01010,B01010,B01010,B01110,B11111,B11111,B01110 };
byte icDrop[8]   = { B00100,B00100,B01010,B01010,B10001,B10001,B10001,B01110 };
byte icSun[8]    = { B00100,B10101,B01110,B11111,B01110,B10101,B00100,B00000 };
byte icClock[8]  = { B01110,B10001,B10101,B10111,B10001,B10001,B01110,B00000 };
byte icAlien[8]  = { B01110,B11111,B10101,B11111,B01110,B01110,B10001,B00000 }; // Alien Debuggers
byte icWine[8]   = { B01110,B01110,B01110,B00100,B00100,B11111,B11111,B01110 };
byte icBarE[8]   = { B11111,B10001,B10001,B10001,B10001,B10001,B10001,B11111 };
byte icBarF[8]   = { B11111,B11111,B11111,B11111,B11111,B11111,B11111,B11111 };
 
// ============================================================
//  Funcoes auxiliares de texto e LCD
// ============================================================
 
// Retorna true quando o idioma atual e portugues.
bool isPT() {
  return cfg.lang == 1;
}
 
// Imprime uma linha e completa com espacos ate ocupar as 16 colunas.
void lcdLine(byte row, const char* txt) {
  lcd.setCursor(0, row);
  lcd.print(txt);
  for (byte i = strlen(txt); i < 16; i++) lcd.print(' ');
}
 
// Imprime uma linha com titulo na esquerda e informacao na direita.
void lcdTitle(const char* left, const char* right) {
  lcd.setCursor(0, 0);
  lcd.print(left);
  byte used = strlen(left);
  byte rlen = strlen(right);
  while (used + rlen < 16) {
    lcd.print(' ');
    used++;
  }
  lcd.print(right);
}
 
// Completa o restante da linha atual com espacos.
void lcdClearRest(byte used) {
  while (used < 16) {
    lcd.print(' ');
    used++;
  }
}
 
// Mostra a dica de botoes nas telas de configuracao.
void showConfigHint() {
  lcd.setCursor(11, 1);
  lcd.print("+/-  ");
}
 
// Conta quantos caracteres um numero inteiro ocupa no LCD.
byte digitsSigned(int v) {
  byte d = (v < 0) ? 1 : 0;
  long n = v < 0 ? -(long)v : v;
  do {
    d++;
    n /= 10;
  } while (n > 0);
  return d;
}
 
// Converte Celsius para Fahrenheit.
float toF(float c) {
  return c * 9.0f / 5.0f + 32.0f;
}
 
// Retorna a temperatura na unidade escolhida pelo usuario.
float dispTemp(float c) {
  return cfg.fahrenheit ? toF(c) : c;
}
 
// Retorna a letra da unidade atual.
char unitChar() {
  return cfg.fahrenheit ? 'F' : 'C';
}
 
// Retorna o texto curto de status.
const char* statusShort(byte s) {
  if (s == 0) return "OK";
  if (s == 1) return isPT() ? "ATN" : "ALT";
  return isPT() ? "PER" : "BAD";
}
 
// Retorna o texto longo de status.
const char* statusLong(byte s) {
  if (s == 0) return isPT() ? "NORMAL" : "NORMAL";
  if (s == 1) return isPT() ? "ATENCAO" : "ALERT";
  return isPT() ? "PERIGO" : "DANGER";
}
 
// Retorna uma seta visual simples para mostrar se esta bom ou ruim.
char statusMark(byte s) {
  if (s == 0) return '+';
  if (s == 1) return '!';
  return 'X';
}
 
// ============================================================
//  EEPROM — configuracoes
// ============================================================
 
// Salva todas as configuracoes na EEPROM.
void saveConfig() {
  EEPROM.put(ADDR_MAGIC, (byte)MAGIC_BYTE);
  EEPROM.put(ADDR_CONFIG, cfg);
}
 
// Carrega as configuracoes da EEPROM ou cria valores padrao.
void loadConfig() {
  byte magic;
  EEPROM.get(ADDR_MAGIC, magic);
 
  if (magic != MAGIC_BYTE) {
    cfg.logo       = 1;
    cfg.fahrenheit = 0;
    cfg.lang       = 1;
    cfg.utc        = -3;
    cfg.luzMin     = 0;
    cfg.luzMax     = 1023;
    saveConfig();
    return;
  }
 
  EEPROM.get(ADDR_CONFIG, cfg);
 
  if (cfg.logo > 1) cfg.logo = 1;
  if (cfg.fahrenheit > 1) cfg.fahrenheit = 0;
  if (cfg.lang > 1) cfg.lang = 1;
  if (cfg.utc < -12 || cfg.utc > 14) cfg.utc = -3;
  if (cfg.luzMax <= cfg.luzMin + 20) {
    cfg.luzMin = 0;
    cfg.luzMax = 1023;
  }
}
 
// ============================================================
//  EEPROM — historico circular de logs
// ============================================================
 
// Salva um registro com as leituras atuais.
void writeLog() {
  byte head  = EEPROM.read(ADDR_LOG_HEAD);
  byte count = EEPROM.read(ADDR_LOG_COUNT);
 
  if (head >= MAX_LOGS) head = 0;
  if (count > MAX_LOGS) count = 0;
 
  LogEntry e;
  e.timestamp = rtcReady ? rtc.now().unixtime() : (fakeEpochAtBoot + millis() / 1000UL);
  e.temp10    = dhtOk ? (int16_t)(tempC * 10.0f) : -999;
  e.hum10     = dhtOk ? (int16_t)(humPct * 10.0f) : -999;
  e.light     = (uint8_t)lightPct;
  e.status    = stGlobal;
  e.pad[0]    = 0;
  e.pad[1]    = 0;
 
  EEPROM.put(ADDR_LOG_START + (int)head * LOG_SIZE, e);
 
  head = (head + 1) % MAX_LOGS;
  if (count < MAX_LOGS) count++;
 
  EEPROM.write(ADDR_LOG_HEAD, head);
  EEPROM.write(ADDR_LOG_COUNT, count);
}
 
// Le um registro salvo. i=0 significa o registro mais recente.
bool readLog(byte i, LogEntry &e) {
  byte head  = EEPROM.read(ADDR_LOG_HEAD);
  byte count = EEPROM.read(ADDR_LOG_COUNT);
 
  if (count == 0 || i >= count) return false;
 
  int idx = ((int)head - 1 - (int)i + MAX_LOGS * 2) % MAX_LOGS;
  EEPROM.get(ADDR_LOG_START + idx * LOG_SIZE, e);
  return true;
}
 
// ============================================================
//  Icones e logo
// ============================================================
 
// Registra os caracteres personalizados no LCD.
void registerIcons() {
  lcd.createChar(0, icThermo);
  lcd.createChar(1, icDrop);
  lcd.createChar(2, icSun);
  lcd.createChar(3, icClock);
  lcd.createChar(4, icAlien);
  lcd.createChar(5, icWine);
}
 
// Mostra a animacao inicial da equipe Debuggers.
void showLogo() {
  if (!cfg.logo) return;
 
  lcd.createChar(6, icBarE);
  lcd.createChar(7, icBarF);
 
  lcd.clear();
  lcd.setCursor(1, 0);
  lcd.write(byte(4));
  lcd.print(" DEBUGGERS ");
  lcd.write(byte(4));
  lcd.setCursor(2, 1);
  lcd.print("VINHERIA CP2");
  delay(900);
 
  lcd.clear();
  lcd.setCursor(1, 0);
  lcd.write(byte(5));
  lcd.print(" Monitorando ");
  lcd.setCursor(1, 1);
  lcd.print("ambiente ideal");
  delay(800);
 
  lcd.clear();
  lcdLine(0, isPT() ? " Carregando... " : " Loading...     ");
  lcd.setCursor(0, 1);
  for (byte i = 0; i < 16; i++) lcd.write(byte(6));
  delay(100);
  for (byte i = 0; i < 16; i++) {
    lcd.setCursor(i, 1);
    lcd.write(byte(7));
    delay(30);
  }
 
  lcd.clear();
  lcdLine(0, isPT() ? " Sistema pronto" : " System ready   ");
  lcdLine(1, isPT() ? "Sistema iniciado" : "System started ");
  delay(700);
  lcd.clear();
 
  registerIcons();
}
 
// ============================================================
//  Sensores
// ============================================================
 
// Calibra o LDR lendo o menor e maior valor durante alguns instantes.
void calibrateLDR() {
  int mn = 1023;
  int mx = 0;
  unsigned long start = millis();
 
  while (millis() - start < LDR_CAL_TIME) {
    int v = analogRead(LDR_PIN);
    if (v < mn) mn = v;
    if (v > mx) mx = v;
    delay(30);
  }
 
  if (mx <= mn + 20) {
    mn = 0;
    mx = 1023;
  }
 
  cfg.luzMin = mn;
  cfg.luzMax = mx;
  saveConfig();
 
  ldrIdx = 0;
  ldrFull = false;
 
  Serial.print(F("[LDR] Calibrado: "));
  Serial.print(mn);
  Serial.print(F(" - "));
  Serial.println(mx);
}
 
// Le temperatura e umidade do DHT11.
void readDHT() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
 
  if (!isnan(t) && !isnan(h) && t > -40.0f && h >= 0.0f && h <= 100.0f) {
    tempC = t;
    humPct = h;
    dhtOk = true;
  } else {
    dhtOk = false;
  }
 
  Serial.print(F("[DHT] "));
  if (dhtOk) {
    Serial.print(F("T="));
    Serial.print(tempC);
    Serial.print(F("C  H="));
    Serial.print(humPct);
    Serial.println(F("%"));
  } else {
    Serial.println(F("FALHA - confira o sensor DHT"));
  }
}
 
// Le o LDR e calcula uma media movel para a luz nao ficar pulando no LCD.
void sampleLDR() {
  ldrBuf[ldrIdx++] = analogRead(LDR_PIN);
 
  if (ldrIdx >= LDR_SAMPLES) {
    ldrIdx = 0;
    ldrFull = true;
  }
 
  byte n = ldrFull ? LDR_SAMPLES : ldrIdx;
  long sum = 0;
 
  for (byte i = 0; i < n; i++) sum += ldrBuf[i];
 
  int avg = (int)(sum / n);
 
  // Em muitos circuitos de LDR, quanto MAIS luz existe, MENOR fica o valor analogico.
  // Por isso a porcentagem foi invertida: mais luz no sensor = maior porcentagem no LCD.
  int rawCalibrado = constrain(avg, cfg.luzMin, cfg.luzMax);
  lightPct = map(rawCalibrado, cfg.luzMin, cfg.luzMax, 100, 0);
  lightPct = constrain(lightPct, 0, 100);
}
 
// ============================================================
//  Avaliacao dos estados
// ============================================================
 
// Avalia luminosidade: 0 OK, 1 atencao, 2 critico.
byte evalLight(int v) {
  if (v <= LIGHT_OK_MAX) return 0;
  if (v <= LIGHT_ALT_MAX) return 1;
  return 2;
}
 
// Avalia temperatura sempre em Celsius.
byte evalTemp(float t) {
  if (t >= TEMP_OK_MIN && t <= TEMP_OK_MAX) return 0;
  if (t >= TEMP_ALT_MIN && t <= TEMP_ALT_MAX) return 1;
  return 2;
}
 
// Avalia umidade relativa do ar.
byte evalHum(float h) {
  if (h >= HUM_OK_MIN && h <= HUM_OK_MAX) return 0;
  if (h >= HUM_ALT_MIN && h <= HUM_ALT_MAX) return 1;
  return 2;
}
 
// Atualiza todos os status e pega o pior deles como status geral.
void evaluateStates() {
  stLight  = evalLight(lightPct);
  stTemp   = dhtOk ? evalTemp(tempC) : 2;
  stHum    = dhtOk ? evalHum(humPct) : 2;
  stGlobal = max(stLight, max(stTemp, stHum));
}
 
// ============================================================
//  LEDs e buzzer
// ============================================================
 
// Acende o LED correto e controla o buzzer.
// O buzzer so apita quando o sistema esta em perigo/critico:
// toca 3 segundos, fica 3 segundos em silencio e repete enquanto continuar critico.
void updateAlerts() {
  unsigned long now = millis();
 
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED, LOW);
 
  // Situação normal: LED verde ligado e buzzer totalmente desligado.
  if (stGlobal == 0) {
    digitalWrite(LED_GREEN, HIGH);
    noTone(BUZZER_PIN);
    buzzerOn = false;
    buzzerPause = false;
    return;
  }
 
  // Situação de atenção: LED amarelo ligado, sem buzzer.
  if (stGlobal == 1) {
    digitalWrite(LED_YELLOW, HIGH);
    noTone(BUZZER_PIN);
    buzzerOn = false;
    buzzerPause = false;
    return;
  }
 
  // Situação de perigo/crítico: LED vermelho + buzzer intermitente.
  digitalWrite(LED_RED, HIGH);
 
  if (buzzerOn) {
    // Se ja tocou por 3 segundos, desliga e entra na pausa.
    if (now - tmBuzzer >= T_BUZZER_ON) {
      noTone(BUZZER_PIN);
      buzzerOn = false;
      buzzerPause = true;
      tmBuzzer = now;
    }
  } else {
    // Se ainda nao comecou ou ja terminou a pausa de 3 segundos, toca de novo.
    if (!buzzerPause || now - tmBuzzer >= T_BUZZER_OFF) {
      tone(BUZZER_PIN, 2000);
      buzzerOn = true;
      buzzerPause = false;
      tmBuzzer = now;
    }
  }
}
 
// ============================================================
//  Relogio
// ============================================================
 
// Retorna a hora local considerando o UTC configurado.
DateTime localTime() {
  uint32_t base;
 
  if (rtcReady) {
    base = rtc.now().unixtime();
  } else {
    base = fakeEpochAtBoot + millis() / 1000UL;
  }
 
#if RTC_GUARDA_UTC
  return DateTime(base + (long)cfg.utc * 3600L);
#else
  return DateTime(base);
#endif
}
 
// Converte um timestamp salvo para o horario que deve aparecer no LCD.
DateTime displayTimeFromTimestamp(uint32_t ts) {
#if RTC_GUARDA_UTC
  return DateTime(ts + (long)cfg.utc * 3600L);
#else
  return DateTime(ts);
#endif
}
 
// Imprime horario no formato HH:MM.
void printTimeHM(DateTime &dt) {
  if (dt.hour() < 10) lcd.print('0');
  lcd.print(dt.hour());
  lcd.print(':');
  if (dt.minute() < 10) lcd.print('0');
  lcd.print(dt.minute());
}
 
// Imprime horario no formato HH:MM:SS.
void printTimeHMS(DateTime &dt) {
  printTimeHM(dt);
  lcd.print(':');
  if (dt.second() < 10) lcd.print('0');
  lcd.print(dt.second());
}
 
// ============================================================
//  Desenho das telas
// ============================================================
 
// Tela 0: resumo principal com tudo em duas linhas.
void drawMainScreen(DateTime &now) {
  lcd.setCursor(0, 0);
  byte used = 0;
 
  if (cfg.logo) {
    lcd.write(byte(4));
    lcd.print(" DBG ");
    used = 5;
  }
 
  if (!dhtOk) {
    lcd.print(isPT() ? "DHT ERRO" : "DHT ERR");
    used += 8;
  } else {
    int t = (int)dispTemp(tempC);
    int h = (int)humPct;
    lcd.print('T');
    lcd.print(t);
    lcd.print(unitChar());
    lcd.print(" H");
    lcd.print(h);
    lcd.print('%');
    used += 1 + digitsSigned(t) + 1 + 2 + digitsSigned(h) + 1;
  }
  lcdClearRest(used);
 
  lcd.setCursor(0, 1);
  byte used2 = 0;
  lcd.write(byte(2));
  lcd.print('L');
  lcd.print(lightPct);
  lcd.print("% ");
  used2 = 1 + 1 + digitsSigned(lightPct) + 2;
  printTimeHM(now);
  used2 += 5;
  lcdClearRest(used2);
}
 
// Tela 1: temperatura em tela limpa, sem texto de status.
void drawTempScreen() {
  lcd.setCursor(0, 0);
  lcd.write(byte(0));
  lcd.print(isPT() ? " TEMPERATURA " : " TEMPERATURE ");
 
  lcd.setCursor(0, 1);
  if (!dhtOk) {
    lcd.print(isPT() ? "Sensor DHT erro " : "DHT sensor err ");
  } else {
    char buf[7];
    dtostrf(dispTemp(tempC), 5, 1, buf);
    lcd.print("Atual: ");
    lcd.print(buf);
    lcd.print((char)223);
    lcd.print(unitChar());
    lcd.print("   ");
  }
}
 
// Tela 2: umidade em tela limpa, sem texto de status.
void drawHumScreen() {
  lcd.setCursor(0, 0);
  lcd.write(byte(1));
  lcd.print(isPT() ? " UMIDADE      " : " HUMIDITY     ");
 
  lcd.setCursor(0, 1);
  if (!dhtOk) {
    lcd.print(isPT() ? "Sensor DHT erro " : "DHT sensor err ");
  } else {
    char buf[7];
    dtostrf(humPct, 5, 1, buf);
    lcd.print("Atual: ");
    lcd.print(buf);
    lcd.print("%   ");
  }
}
 
// Tela 3: luminosidade em tela limpa, sem texto de status.
void drawLightScreen() {
  lcd.setCursor(0, 0);
  lcd.write(byte(2));
  lcd.print(isPT() ? " LUMINOSIDADE" : " LIGHT LEVEL ");
 
  lcd.setCursor(0, 1);
  lcd.print("Atual: ");
  lcd.print(lightPct);
  lcd.print("%       ");
}
 
// Tela 4: relogio atual e UTC.
void drawClockScreen(DateTime &now) {
  lcd.setCursor(0, 0);
  lcd.write(byte(3));
  lcd.print(isPT() ? " RELOGIO      " : " CLOCK        ");
 
  lcd.setCursor(0, 1);
  printTimeHMS(now);
  lcd.print(" UTC");
  if (cfg.utc >= 0) lcd.print('+');
  lcd.print(cfg.utc);
  lcd.print("   ");
}
 
// Tela 5: painel limpo com faixas ideais do projeto.
void drawStatusScreen() {
  lcdLine(0, isPT() ? " FAIXAS IDEAIS " : " IDEAL RANGES  ");
  lcd.setCursor(0, 1);
 
  if (curScreen == 5) {
    lcd.print("T10-15 H50-70 ");
  }
}
 
// Tela 6: configuracao do fuso UTC.
void drawUtcConfig() {
  lcdLine(0, isPT() ? " FUSO/UTC       " : " TIMEZONE/UTC   ");
  lcd.setCursor(0, 1);
  lcd.print("UTC ");
  if (cfg.utc >= 0) lcd.print('+');
  lcd.print(cfg.utc);
  lcd.print("      ");
  showConfigHint();
}
 
// Tela 7: configuracao da unidade de temperatura.
void drawUnitConfig() {
  lcdLine(0, isPT() ? " UNIDADE TEMP.  " : " TEMP UNIT      ");
  lcd.setCursor(0, 1);
  lcd.print(cfg.fahrenheit ? "Fahrenheit " : "Celsius    ");
  showConfigHint();
}
 
// Tela 8: configuracao da logo Debuggers.
void drawLogoConfig() {
  lcd.setCursor(0, 0);
  if (cfg.logo) lcd.write(byte(4));
  else lcd.print(' ');
  lcd.print(isPT() ? " LOGO DEBUGGERS" : " DEBUGGERS LOGO");
 
  lcd.setCursor(0, 1);
  lcd.print(cfg.logo ? (isPT() ? "Ligada     " : "Enabled    ") : (isPT() ? "Desligada  " : "Disabled   "));
  showConfigHint();
}
 
// Tela 9: ultimo log salvo na EEPROM.
void drawLogScreen() {
  byte count = EEPROM.read(ADDR_LOG_COUNT);
 
  if (count == 0) {
    lcdLine(0, isPT() ? " SEM REGISTROS  " : " NO LOGS SAVED  ");
    lcdLine(1, isPT() ? "aguarde 60s...  " : "wait 60s...     ");
    return;
  }
 
  LogEntry e;
  readLog(0, e);
  DateTime dt = displayTimeFromTimestamp(e.timestamp);
 
  lcd.setCursor(0, 0);
  lcd.write(byte(3));
  lcd.print(' ');
  printTimeHM(dt);
  lcd.print(' ');
 
  if (e.temp10 == -999) {
    lcd.print("DHT ERR ");
  } else {
    float rawC = (float)e.temp10 / 10.0f;
    float td = cfg.fahrenheit ? toF(rawC) : rawC;
    char tbuf[6];
    dtostrf(td, 4, 1, tbuf);
    lcd.print(tbuf);
    lcd.print((char)223);
    lcd.print(unitChar());
  }
 
  lcd.setCursor(0, 1);
  lcd.print(isPT() ? "Luz" : "Lgt");
  lcd.print(':');
  lcd.print(e.light);
  lcd.print("%  n=");
  lcd.print(count);
  lcd.print("      ");
}
 
// Tela 10: configuracao de idioma.
void drawLangConfig() {
  lcdLine(0, cfg.lang ? " IDIOMA         " : " LANGUAGE       ");
  lcd.setCursor(0, 1);
  lcd.print(cfg.lang ? "Portugues  " : "English    ");
  showConfigHint();
}
 
// Escolhe qual tela desenhar de acordo com curScreen.
void drawScreen(DateTime &now) {
  switch (curScreen) {
    case 0:  drawMainScreen(now);  break;
    case 1:  drawTempScreen();     break;
    case 2:  drawHumScreen();      break;
    case 3:  drawLightScreen();    break;
    case 4:  drawClockScreen(now); break;
    case 5:  drawStatusScreen();   break;
    case 6:  drawUtcConfig();      break;
    case 7:  drawUnitConfig();     break;
    case 8:  drawLogoConfig();     break;
    case 9:  drawLogScreen();      break;
    case 10: drawLangConfig();     break;
  }
}
 
// ============================================================
//  Botoes
// ============================================================
 
// Diz se a tela atual e uma tela de configuracao.
bool isConfigScreen() {
  return curScreen == 6 || curScreen == 7 || curScreen == 8 || curScreen == 10;
}
 
// Altera uma configuracao usando o sentido informado.
void changeConfig(int direction) {
  switch (curScreen) {
    case 6:
      cfg.utc += direction;
      if (cfg.utc > 14) cfg.utc = 14;
      if (cfg.utc < -12) cfg.utc = -12;
      break;
 
    case 7:
      cfg.fahrenheit = !cfg.fahrenheit;
      break;
 
    case 8:
      cfg.logo = !cfg.logo;
      break;
 
    case 10:
      cfg.lang = !cfg.lang;
      break;
  }
 
  saveConfig();
  lcd.clear();
}
 
// Le os botoes com debounce simples.
void readButtons() {
  unsigned long now = millis();
 
  if (digitalRead(BTN_MENU) == LOW && now - tmBtnMenu >= T_DEBOUNCE) {
    curScreen = (curScreen + 1) % NUM_SCREENS;
    lcd.clear();
    tmBtnMenu = now;
  }
 
  if (digitalRead(BTN_UP) == LOW && now - tmBtnUp >= T_DEBOUNCE) {
    if (isConfigScreen()) changeConfig(+1);
    tmBtnUp = now;
  }
 
  if (digitalRead(BTN_DOWN) == LOW && now - tmBtnDown >= T_DEBOUNCE) {
    if (isConfigScreen()) {
      changeConfig(-1);
    } else {
      lcd.clear();
      lcdLine(0, isPT() ? " CALIBRANDO LDR" : " CALIBRATING LDR");
      lcdLine(1, isPT() ? " Aguarde...     " : " Please wait... ");
      calibrateLDR();
      sampleLDR();
      evaluateStates();
      lcd.clear();
    }
    tmBtnDown = millis();
  }
}
 
// ============================================================
//  setup — executa uma vez ao ligar/resetar
// ============================================================
void setup() {
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
 
  pinMode(BTN_MENU, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
 
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED, LOW);
  noTone(BUZZER_PIN);
 
  Serial.begin(9600);
  Serial.println(F("=== Vinheria Agnello CP2 - Debuggers UI organizada ==="));
 
  lcd.init();
  lcd.backlight();
  registerIcons();
 
  loadConfig();
 
  dht.begin();
  delay(DHT_START_DELAY);
 
  rtcReady = rtc.begin();
  if (!rtcReady) {
    fakeEpochAtBoot = DateTime(F(__DATE__), F(__TIME__)).unixtime();
    lcdLine(0, " RTC simulado   ");
    lcdLine(1, " usando millis() ");
    Serial.println(F("[AVISO] RTC nao encontrado. Usando horario de compilacao + millis()."));
    delay(700);
  } else {
    if (!rtc.isrunning() || AJUSTAR_RTC_NO_UPLOAD) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      Serial.println(F("[RTC] Horario ajustado pelo tempo de compilacao."));
    }
    Serial.println(F("[RTC] OK"));
  }
 
  showLogo();
  calibrateLDR();
  readDHT();
  sampleLDR();
  evaluateStates();
  updateAlerts();
 
  Serial.println(F("[OK] Sistema iniciado."));
}
 
// ============================================================
//  loop — roda repetidamente durante todo o projeto
// ============================================================
void loop() {
  unsigned long now = millis();
  DateTime localNow = localTime();
 
  readButtons();
 
  if (now - tmLDR >= T_LDR) {
    tmLDR = now;
    sampleLDR();
    evaluateStates();
    updateAlerts();
  }
 
  if (now - tmDHT >= T_DHT) {
    tmDHT = now;
    readDHT();
    evaluateStates();
    updateAlerts();
  }
 
  if (now - tmScreen >= T_SCREEN) {
    tmScreen = now;
    drawScreen(localNow);
  }
 
  if (now - tmLog >= T_LOG) {
    tmLog = now;
    writeLog();
 
    Serial.print(F("[LOG] T="));
    Serial.print(dispTemp(tempC));
    Serial.print(cfg.fahrenheit ? F("F") : F("C"));
    Serial.print(F(" H="));
    Serial.print(humPct);
    Serial.print(F("% L="));
    Serial.print(lightPct);
    Serial.print(F("% Status="));
    Serial.println(statusLong(stGlobal));
  }
 
  // Mantem o ciclo do buzzer atualizado mesmo quando os sensores nao mudam.
  updateAlerts();
}