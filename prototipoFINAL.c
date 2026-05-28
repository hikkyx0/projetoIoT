#include <Wire.h>
#include <WiFi.h>
#include "MAX30105.h"
#include <MPU6050_light.h>
#include "ThingSpeak.h"

// ─────────────────────────────────────────
//  CONFIGURAÇÃO WIFI
// ─────────────────────────────────────────
const char* WIFI_SSID  = "redePI";
const char* WIFI_SENHA = "truewifi80";

// ─────────────────────────────────────────
//  CONFIGURAÇÃO THINGSPEAK
//    Field 1: BPM
//    Field 2: Queda (0 ou 1)
//    Field 3: Pulso detectado (0 ou 1)
// ─────────────────────────────────────────
unsigned long TS_CHANNEL_ID = 3389952;
const char*   TS_WRITE_KEY  = "4JA0Q8N3N9YM9C83";

const unsigned long INTERVALO_ENVIO = 15000;
unsigned long ultimoEnvio = 0;

WiFiClient wifiClient;

// ─────────────────────────────────────────
//  SENSORES
// ─────────────────────────────────────────
MAX30105 particleSensor;
MPU6050  mpu(Wire);

// ─── Batimento cardíaco ───────────────────
const long IR_LIMIAR = 100000;

long   irAnterior     = 0;
long   irPico         = 0;
long   irVale         = 999999;
bool   subindo        = false;
bool   pulsoDetectado = false;

unsigned long tempoUltimoBatimento = 0;
int   beatAvg   = 0;
const byte RATE_SIZE = 4;
int   rates[RATE_SIZE];
byte  rateSpot  = 0;

// ─── Detecção de queda ────────────────────
// magnitude em repouso é calculada no setup e usada como referência
float magnitudeRepouso  = 1.0;
const float LIMIAR_IMPACTO   = 0.2; // g acima do repouso pra considerar queda

bool          quedaAtiva       = false;
unsigned long tempoUltimaQueda = 0;
const unsigned long DURACAO_ALERTA_QUEDA = 10000;

// ─────────────────────────────────────────
//  PROTÓTIPOS
// ─────────────────────────────────────────
void conectarWiFi();
void enviarThingSpeak();
void lerBatimento();
void verificarQueda();


// ═════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println("\nPulseira VITALIS iniciando...");

  Wire.begin(21, 22);

  // ── MPU6050 ──────────────────────────────
  if (mpu.begin() != 0) {
    Serial.println("ERRO: MPU6050 desconectado");
    while (true);
  }
  Serial.println("MPU6050 encontrado. Calibrando...");
  delay(3000);
  // calibra SÓ o giroscópio (false = acelerômetro livre, true = giroscópio)
  //mpu.calcOffsets(false, true);
  Serial.println("MPU6050 calibrado");

  // mede a magnitude em repouso logo após calibrar pra usar como referência
  mpu.update();
  float ax = mpu.getAccX();
  float ay = mpu.getAccY();
  float az = mpu.getAccZ();
  magnitudeRepouso = sqrt(ax*ax + ay*ay + az*az);
  Serial.printf("Magnitude em repouso: %.3f\n", magnitudeRepouso);

  // ── MAX30102 ─────────────────────────────
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("ERRO: MAX30102 desconectado");
    while (true);
  }
  particleSensor.setup(60, 4, 2, 400, 411, 4096);
  Serial.println("MAX30102 configurado");

  // ── WiFi + ThingSpeak ────────────────────
  conectarWiFi();
  ThingSpeak.begin(wifiClient);

  Serial.println("-Sistema pronto-\n");
}


// ═════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════
void loop() {
  mpu.update();

  lerBatimento();
  verificarQueda();

  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
  }

  if (millis() - ultimoEnvio >= INTERVALO_ENVIO) {
    enviarThingSpeak();
    ultimoEnvio = millis();
  }

  Serial.printf("BPM: %d | Queda: %s | Pulso: %s | WiFi: %s\n",
    beatAvg,
    quedaAtiva     ? "SIM" : "nao",
    pulsoDetectado ? "SIM" : "nao",
    WiFi.status() == WL_CONNECTED ? "OK" : "OFF"
  );

  delay(20);
}


// ═════════════════════════════════════════
//  FUNÇÕES
// ═════════════════════════════════════════

void lerBatimento() {
  long irValue = particleSensor.getIR();

  pulsoDetectado = (irValue > IR_LIMIAR);

  if (!pulsoDetectado) {
    irPico  = 0;
    irVale  = 999999;
    subindo = false;
    return;
  }

  long variacao = irValue - irAnterior;

  if (variacao > 0) {
    subindo = true;
    if (irValue > irPico) irPico = irValue;
  } else if (variacao < 0 && subindo) {
    subindo = false;

    long amplitude = irPico - irVale;

    if (amplitude > 2000) {
      unsigned long agora = millis();
      unsigned long delta = agora - tempoUltimoBatimento;

      if (delta > 300 && delta < 2000) {
        float bpm = 60000.0 / delta;

        rates[rateSpot++] = (int)bpm;
        rateSpot %= RATE_SIZE;

        beatAvg = 0;
        for (byte x = 0; x < RATE_SIZE; x++) beatAvg += rates[x];
        beatAvg /= RATE_SIZE;

        Serial.printf(">> Batimento! BPM: %.1f | Media: %d\n", bpm, beatAvg);
      }

      tempoUltimoBatimento = agora;
      irVale = irValue;
    }

    irPico = 0;
  }

  if (irValue < irVale) irVale = irValue;
  irAnterior = irValue;
}

void verificarQueda() {
  float ax = mpu.getAccX();
  float ay = mpu.getAccY();
  float az = mpu.getAccZ();

  float magnitude = sqrt(ax*ax + ay*ay + az*az);

  // compara com a magnitude medida em repouso no setup
  float impacto = abs(magnitude - magnitudeRepouso);

  Serial.printf("mag: %.3f | imp: %.3f | ref: %.3f\n", magnitude, impacto, magnitudeRepouso);

  if (impacto > LIMIAR_IMPACTO && !quedaAtiva) {
    Serial.printf("QUEDA DETECTADA! Impacto: %.2fg\n", impacto);
    quedaAtiva       = true;
    tempoUltimaQueda = millis();
  }

  if (quedaAtiva && (millis() - tempoUltimaQueda > DURACAO_ALERTA_QUEDA)) {
    quedaAtiva = false;
    Serial.println("Alerta de queda encerrado.");
  }
}

void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("Conectando ao WiFi: %s ...", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_SENHA);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi conectado! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nFalha ao conectar. Continuando sem WiFi...");
  }
}

void enviarThingSpeak() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Sem WiFi, envio ao ThingSpeak ignorado.");
    return;
  }

  ThingSpeak.setField(1, beatAvg);
  ThingSpeak.setField(2, quedaAtiva     ? 1 : 0);
  ThingSpeak.setField(3, pulsoDetectado ? 1 : 0);

  int resultado = ThingSpeak.writeFields(TS_CHANNEL_ID, TS_WRITE_KEY);

  if (resultado == 200) {
    Serial.println("ThingSpeak: dados enviados com sucesso.");
  } else {
    Serial.printf("ThingSpeak: erro no envio (cod %d)\n", resultado);
  }
}
