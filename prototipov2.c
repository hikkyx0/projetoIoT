#include <Wire.h>
#include <WiFi.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <MPU6050_light.h>

// pinos dos leds
#define LED_AZUL 2
#define LED_VERDE 4
#define LED_VERMELHO 5

// cada rede conhecida tem nome, senha e o local que ela representa
struct RedeConhecida {
  const char* ssid;
  const char* senha;
  const char* local;
};

const RedeConhecida redesConhecidas[] = {
  {"UNIFEOB_Biblioteca",  "senha_bib",  "Biblioteca"},
  {"UNIFEOB_Lab1",        "senha_lab1", "Laboratorio 1"},
  {"UNIFEOB_Lab2",        "senha_lab2", "Laboratorio 2"},
  {"UNIFEOB_Cantina",     "senha_cant", "Cantina"},
  {"UNIFEOB_Ginasio",     "senha_gin",  "Ginasio"},
  {"UNIFEOB_Sala_Aula_A", "senha_saa",  "Sala de Aula A"},
};
const int NUM_REDES = sizeof(redesConhecidas) / sizeof(redesConhecidas[0]);

// localAtual é a variável principal de localização, qualquer parte do código pode ler ela
String localAtual = "Desconhecido";
bool wifiConectado = false;

MAX30105 particleSensor;
MPU6050  mpu(Wire);

// variáveis do batimento
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute = 0;
int beatAvg = 0;
bool dedoDetectado = false;

const int BPM_MINIMO = 50;
const int BPM_MAXIMO = 110;

// variáveis da queda
const float LIMIAR_QUEDA = 2.5;
bool quedaAtiva = false;
unsigned long tempoUltimaQueda = 0;
const unsigned long DURACAO_ALERTA_QUEDA = 10000;

// declaração antecipada das funções, necessário porque o setup() chama elas antes de estarem definidas
void testarLEDs();
void piscarLED(int pino, int vezes, int intervaloMs);
void conectarWiFi();
void lerBatimento();
void verificarQueda();
void atualizarLEDs();


void setup() {
  Serial.begin(115200);
  Serial.println("\nPulseira VITALIS iniciando...");

  pinMode(LED_AZUL, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  testarLEDs();

  Wire.begin(21, 22);

  // inicializa o MPU6050 e trava se não encontrar
  if (mpu.begin() != 0) {
    Serial.println("ERRO: MPU6050 desconectado");
    piscarLED(LED_VERMELHO, 20, 100);
    while (true);
  }
  Serial.println("MPU6050 encontrado. Calibrando...");
  delay(3000);
  mpu.calcOffsets(true, true);
  Serial.println("MPU6050 calibrado");

  // inicializa o MAX30102 e trava se não encontrar
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("ERRO: MAX30102 desconectado");
    piscarLED(LED_VERMELHO, 20, 100);
    while (true);
  }
  particleSensor.setup(60, 4, 2, 100, 411, 4096);
  Serial.println("MAX30102 configurado");

  conectarWiFi();
  Serial.println("-Sistema pronto-\n");
}


void loop() {
  mpu.update();

  lerBatimento();
  verificarQueda();
  atualizarLEDs();

  // reconecta automaticamente se cair a rede
  if (WiFi.status() != WL_CONNECTED) {
    wifiConectado = false;
    conectarWiFi();
  }

  // isso aparece no Serial Monitor a cada 500ms, é o jeito de acompanhar o sistema sem tela
  Serial.printf("[LOCAL: %s] | BPM: %d | Queda: %s | WiFi: %s\n",
    localAtual.c_str(),
    beatAvg,
    quedaAtiva ? "SIM" : "nao",
    wifiConectado ? "OK"  : "fora"
  );

  delay(500);
}


void lerBatimento() {
  long irValue  = particleSensor.getIR();
  dedoDetectado = (irValue > 50000); // abaixo de 50000 significa que não tem dedo no sensor

  if (!dedoDetectado) return;

  if (checkForBeat(irValue)) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    beatsPerMinute = 60.0 / (delta / 1000.0);

    if (beatsPerMinute > 20 && beatsPerMinute < 255) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;

      beatAvg = 0;
      for (byte x = 0; x < RATE_SIZE; x++) beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }
  }
}


void verificarQueda() {
  float ax = mpu.getAccX();
  float ay = mpu.getAccY();
  float az = mpu.getAccZ();

  // magnitude é o tamanho total do vetor de aceleração nos 3 eixos
  // em repouso fica em torno de 1g, numa queda passa de 2.5g
  float magnitude = sqrt(ax*ax + ay*ay + az*az);

  if (magnitude > LIMIAR_QUEDA) {
    Serial.printf("QUEDA DETECTADA, Aceleracao: %.2fg\n", magnitude);
    quedaAtiva = true;
    tempoUltimaQueda = millis();
  }

  if (quedaAtiva && (millis() - tempoUltimaQueda > DURACAO_ALERTA_QUEDA)) {
    quedaAtiva = false;
    Serial.println("Alerta de queda encerrado.");
  }
}


void atualizarLEDs() {
  // azul acompanha direto o estado do wifi
  digitalWrite(LED_AZUL, wifiConectado ? HIGH : LOW);

  // queda tem prioridade sobre tudo, pisca vermelho e ignora o resto
  if (quedaAtiva) {
    digitalWrite(LED_VERDE, LOW);
    piscarLED(LED_VERMELHO, 2, 150);
    return;
  }

  // sem dedo ou sem leitura válida, apaga os dois
  if (!dedoDetectado || beatAvg == 0) {
    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_VERMELHO, LOW);
    return;
  }

  // bpm fora da faixa acende vermelho, dentro da faixa acende verde
  if (beatAvg < BPM_MINIMO || beatAvg > BPM_MAXIMO) {
    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_VERMELHO, HIGH);
    return;
  }

  digitalWrite(LED_VERDE, HIGH);
  digitalWrite(LED_VERMELHO, LOW);
}


void conectarWiFi() {
  Serial.println("Escaneando redes...");
  digitalWrite(LED_AZUL, LOW);

  int encontradas = WiFi.scanNetworks();

  if (encontradas == 0) {
    Serial.println("Nenhuma rede encontrada.");
    localAtual = "Desconhecido";
    delay(10000);
    return;
  }

  // percorre as redes encontradas e compara com as cadastradas,
  // guarda a que tiver melhor sinal (RSSI mais próximo de zero)
  int melhorRSSI = -1000;
  int indiceEscolhido = -1;

  for (int i = 0; i < encontradas; i++) {
    for (int j = 0; j < NUM_REDES; j++) {
      if (WiFi.SSID(i) == redesConhecidas[j].ssid && WiFi.RSSI(i) > melhorRSSI) {
        melhorRSSI = WiFi.RSSI(i);
        indiceEscolhido = j;
      }
    }
  }

  if (indiceEscolhido == -1) {
    Serial.println("Nenhuma rede da UNIFEOB encontrada.");
    localAtual = "Fora do Campus";
    wifiConectado = false;
    return;
  }

  WiFi.begin(redesConhecidas[indiceEscolhido].ssid,
             redesConhecidas[indiceEscolhido].senha);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    piscarLED(LED_AZUL, 1, 300);
    tentativas++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConectado = true;
    localAtual = String(redesConhecidas[indiceEscolhido].local); // aqui que o local é definido
    Serial.printf("Conectado, Local: %s | Sinal: %d dBm\n", localAtual.c_str(), WiFi.RSSI());
    digitalWrite(LED_AZUL, HIGH);
  } else {
    wifiConectado = false;
    localAtual = "Desconhecido";
    Serial.println("Falha na conexao.");
  }
}


void piscarLED(int pino, int vezes, int intervaloMs) {
  for (int i = 0; i < vezes; i++) {
    digitalWrite(pino, HIGH); delay(intervaloMs);
    digitalWrite(pino, LOW); delay(intervaloMs);
  }
}

void testarLEDs() {
  digitalWrite(LED_VERMELHO, HIGH); delay(500); digitalWrite(LED_VERMELHO, LOW);
  digitalWrite(LED_VERDE, HIGH); delay(500); digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_AZUL, HIGH); delay(500); digitalWrite(LED_AZUL, LOW);
  Serial.println("Teste de LEDs OK.");
}