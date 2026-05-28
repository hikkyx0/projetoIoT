#include <Wire.h>
#include <WiFi.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <MPU6050_light.h>


#define LED_AZUL 4
#define LED_VERDE 5
#define LED_VERMELHO 18


// estrutura teórica de localização por rede wifi
// a ideia é: qual rede conectou = qual local o aluno está
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


// variável que armazenaria o local do aluno após conectar numa rede conhecida
String localAtual = "Desconhecido";


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


void testarLEDs();
void piscarLED(int pino, int vezes, int intervaloMs);
void lerBatimento();
void verificarQueda();
void atualizarLEDs();




void setup() {
  Serial.begin(115200);
  Serial.println("\nPulseira VITALIS iniciando...");


  pinMode(LED_AZUL, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);


  // o led azul acende durante a inicialização e apaga quando o sistema estiver pronto
  digitalWrite(LED_AZUL, HIGH);
  testarLEDs();


  Wire.begin(21, 22);


  if (mpu.begin() != 0) {
    Serial.println("ERRO: MPU6050 desconectado");
    piscarLED(LED_VERMELHO, 20, 100);
    while (true);
  }
  Serial.println("MPU6050 encontrado. Calibrando...");
  delay(3000);
  mpu.calcOffsets(true, true);
  Serial.println("MPU6050 calibrado");


  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("ERRO: MAX30102 desconectado");
    piscarLED(LED_VERMELHO, 20, 100);
    while (true);
  }
  particleSensor.setup(60, 4, 2, 100, 411, 4096);
  Serial.println("MAX30102 configurado");


  // apaga o azul quando termina de inicializar
  digitalWrite(LED_AZUL, LOW);
  Serial.println("-Sistema pronto-\n");
}




void loop() {
  mpu.update();


  lerBatimento();
  verificarQueda();
  atualizarLEDs();


  Serial.printf("[LOCAL: %s] | BPM: %d | Queda: %s\n",
    localAtual.c_str(),
    beatAvg,
    quedaAtiva ? "SIM" : "nao"
  );


  delay(500);
}




void lerBatimento() {
  long irValue = particleSensor.getIR();
  dedoDetectado = (irValue > 50000);


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




void piscarLED(int pino, int vezes, int intervaloMs) {
  for (int i = 0; i < vezes; i++) {
    digitalWrite(pino, HIGH); delay(intervaloMs);
    digitalWrite(pino, LOW);  delay(intervaloMs);
  }
}


void testarLEDs() {
  digitalWrite(LED_VERMELHO, HIGH); delay(500); digitalWrite(LED_VERMELHO, LOW);
  digitalWrite(LED_VERDE, HIGH); delay(500); digitalWrite(LED_VERDE,    LOW);
  Serial.println("Teste de LEDs OK.");
}

