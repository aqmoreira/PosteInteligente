/*
  PROJETO: Poste Inteligente - Cidade Conectada (Versão IoT Multi-Postes)
  HARDWARE: ESP32 + DHT11 + LDR + OLED + WS2812B + Buzzer
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// ==========================================
//      CONFIGURAÇÃO DA IDENTIDADE DO POSTE
//      (ALTERE APENAS AQUI PARA CADA PLACA)
// ==========================================
const char* ID_POSTE = "poste_03";
// ==========================================

// --- CREDENCIAIS DE REDE E MQTT ---
const char* ssid = "Coloque aqui sua rede";
const char* password = "coloque aqui senha da rede";
const char* mqtt_server = "mqtt-dashboard.com";


// --- CONFIGURAÇÕES DE PINO ---
#define DHTPINO 4
#define LDRPINO 34
#define BUZZERPINO 23
#define LEDPINO 15

// --- CONFIGURAÇÕES DOS COMPONENTES ---
#define DHTTYPE DHT11
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define NUM_LEDS 8



// --- OBJETOS ---
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT dht(DHTPINO, DHTTYPE);
Adafruit_NeoPixel fitaLED(NUM_LEDS, LEDPINO, NEO_GRB + NEO_KHZ800);
WiFiClient espClient;
PubSubClient client(espClient);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -10800);

// --- VARIÁVEIS GLOBAIS ---
String mensagemPropaganda = "Cidade Inteligente";
float temperatura = 0;
float humidade = 0;
int luminosidade = 0;
// Modos: 0=AUTO (LDR), 1=LIGADO_MANUAL, 2=DESLIGADO_MANUAL, 3=ECO_FORCADO
int modoOperacao = 0;
float potenciaEstimada = 0.0;  // Em Watts

// Variáveis para controle de tempo (Multitasking)
unsigned long tempoAnteriorDisplay = 0;
unsigned long tempoAnteriorSensores = 0;
unsigned long tempoAnteriorMQTT = 0;
int telaAtual = 0;

// Strings para tópicos (montadas dinamicamente no setup)
String topico_envio_sensores;
String topico_recebe_propaganda;
String topico_recebe_comando;
String topico_recebe_geral = "cidade/geral/alerta";  // Tópico comum a todos

void setup() {
  Serial.begin(115200);

  // MONTAGEM DOS TÓPICOS BASEADO NO ID
  // Resultado ex: "cidade/bairro/poste_02/sensores"
  topico_envio_sensores = "cidade/bairro/" + String(ID_POSTE) + "/sensores";
  topico_recebe_propaganda = "cidade/bairro/" + String(ID_POSTE) + "/propaganda";
  topico_recebe_comando = "cidade/bairro/" + String(ID_POSTE) + "/comando";

  Serial.print("Configurando Poste ID: ");
  Serial.println(ID_POSTE);

  // Inicializa Hardware
  dht.begin();
  pinMode(BUZZERPINO, OUTPUT);
  pinMode(LDRPINO, INPUT);

  fitaLED.begin();
  fitaLED.show();
  fitaLED.setBrightness(100);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 falhou!"));
    for (;;)
      ;
  }
  display.clearDisplay();
  display.setTextColor(WHITE);

  // Conexões
  conectarWiFi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  timeClient.begin();
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  timeClient.update();

  // --- TAREFA 1: Ler Sensores (2s) ---
  if (millis() - tempoAnteriorSensores > 2000) {
    tempoAnteriorSensores = millis();
    lerSensores();
    gerenciarLuzes();
  }

  // --- TAREFA 2: Enviar dados MQTT (5s) ---
  if (millis() - tempoAnteriorMQTT > 5000) {
    tempoAnteriorMQTT = millis();
    enviarTelemetria();
  }

  // --- TAREFA 3: Trocar Tela (3s) ---
  if (millis() - tempoAnteriorDisplay > 3000) {
    tempoAnteriorDisplay = millis();
    telaAtual++;
    if (telaAtual > 3) telaAtual = 0;
    atualizarDisplay();
  }
}

// --- FUNÇÕES AUXILIARES ---

void lerSensores() {
  temperatura = dht.readTemperature();
  humidade = dht.readHumidity();
  luminosidade = analogRead(LDRPINO);

  if (isnan(temperatura) || isnan(humidade)) {
    Serial.println("Falha no DHT");
    temperatura = -99;
    humidade = -99;
  }
}

void atualizarDisplay() {
  display.clearDisplay();

  // Cabeçalho fixo com o ID do poste (para saber qual é qual na rua)
  display.setTextSize(1);
  display.setCursor(90, 0);
  display.print(ID_POSTE);  // Mostra "poste_02" no cantinho

  switch (telaAtual) {
    case 0:  // TELA SENSORES
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("AMBIENTE:");
      display.setTextSize(2);
      display.setCursor(0, 15);
      if (temperatura != -99) {
        display.print(temperatura, 1);
        display.print("C");
      }
      display.setTextSize(1);
      display.setCursor(0, 40);
      if (humidade != -99) {
        display.print("Hum: ");
        display.print(humidade, 0);
        display.print("%");
      }
      break;

    case 1:  // TELA LUMINOSIDADE
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("ILUMINACAO:");
      display.setTextSize(2);
      display.setCursor(0, 20);
      display.print(luminosidade);
      display.setTextSize(1);
      display.setCursor(0, 45);
      if (luminosidade < 1000) display.print("DIA");
      else display.print("NOITE");
      break;

    case 2:  // TELA HORA
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("HORA CERTA:");
      display.setTextSize(3);
      display.setCursor(10, 20);
      display.print(timeClient.getFormattedTime().substring(0, 5));
      break;

    case 3:  // TELA MENSAGEM
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("MSG PREFEITURA:");
      display.setTextSize(1);
      display.setCursor(0, 15);
      display.print(mensagemPropaganda);
      break;
  }
  display.display();
}

void gerenciarLuzes() {
  int hora = timeClient.getHours();
  uint32_t corBranca = fitaLED.Color(255, 255, 255);
  int ledsAcesos = 0;
  int brilhoAtual = 0;  // 0 a 255

  fitaLED.clear();  // Limpa tudo inicialmente

  // LÓGICA DE DECISÃO (Máquina de Estados)

  // --- MODO 2: DESLIGADO MANUAL ---
  if (modoOperacao == 2) {
    brilhoAtual = 0;
    ledsAcesos = 0;
  }

  // --- MODO 1: LIGADO TOTAL MANUAL (Teste/Emergência) ---
  else if (modoOperacao == 1) {
    for (int i = 0; i < NUM_LEDS; i++) fitaLED.setPixelColor(i, corBranca);
    brilhoAtual = 255;  // Força máximo
    ledsAcesos = NUM_LEDS;
  }

  // --- MODO 3: ECO FORÇADO (Intercalado Manual) ---
  else if (modoOperacao == 3) {
    for (int i = 0; i < NUM_LEDS; i = i + 2) fitaLED.setPixelColor(i, corBranca);
    brilhoAtual = 100;
    ledsAcesos = NUM_LEDS / 2;
  }

  // --- MODO 0: AUTOMÁTICO (Segue o Sensor e Hora) ---
  else {
    if (luminosidade < 2000) {  // Dia
      brilhoAtual = 0;
      ledsAcesos = 0;
    } else {  // Noite
      // Verifica Madrugada para Economia
      if (hora >= 23 || hora < 5) {
        for (int i = 0; i < NUM_LEDS; i = i + 2) fitaLED.setPixelColor(i, corBranca);
        brilhoAtual = 50;  // Brilho reduzido
        ledsAcesos = NUM_LEDS / 2;
      } else {
        for (int i = 0; i < NUM_LEDS; i++) fitaLED.setPixelColor(i, corBranca);
        brilhoAtual = 100;  // Brilho padrão (ajustado para segurança USB)
        ledsAcesos = NUM_LEDS;
      }
    }
  }

  // Aplica o brilho e mostra
  fitaLED.setBrightness(brilhoAtual);
  fitaLED.show();

  // CÁLCULO DE CONSUMO (Estimativa em Watts)
  // Corrente aprox por LED full white = 60mA (0.06A)
  // Tensão = 5V
  // Fator de Brilho = brilhoAtual / 255.0
  float correnteTotal = ledsAcesos * 0.06 * (brilhoAtual / 255.0);
  potenciaEstimada = correnteTotal * 5.0;  // P = V * I
}

void enviarTelemetria() {
  String modoTexto = "AUTO";
  if (modoOperacao == 1) modoTexto = "MANUAL_ON";
  if (modoOperacao == 2) modoTexto = "MANUAL_OFF";
  if (modoOperacao == 3) modoTexto = "ECO_MAX";

  String json = "{";
  json += "\"id\": \"" + String(ID_POSTE) + "\",";
  json += "\"temp\": " + String(temperatura) + ",";
  json += "\"luz\": " + String(luminosidade) + ",";
  json += "\"watts\": " + String(potenciaEstimada) + ",";  // Novo dado
  json += "\"modo\": \"" + modoTexto + "\"";               // Novo dado
  json += "}";

  client.publish(topico_envio_sensores.c_str(), json.c_str());
}

void callback(char* topic, byte* payload, unsigned int length) {
  String mensagem = "";
  for (int i = 0; i < length; i++) {
    mensagem += (char)payload[i];
  }
  String topicoRecebido = String(topic);

  // CASO 1: É UMA MENSAGEM DE TEXTO (PROPAGANDA)
  if (topicoRecebido == topico_recebe_propaganda) {
    mensagemPropaganda = mensagem;
    telaAtual = 3;
    atualizarDisplay();
    // Buzz curto apenas para chamar atenção visual
    tone(BUZZERPINO, 1000, 100);
  }

  // CASO 2: É UM COMANDO DE CONTROLE (LUZES/ENERGIA)
  else if (topicoRecebido == topico_recebe_comando) {
    // Buzz duplo para confirmar recebimento de comando administrativo
    tone(BUZZERPINO, 2000, 100);
    delay(150);
    tone(BUZZERPINO, 2000, 100);

    if (mensagem == "AUTO") modoOperacao = 0;
    if (mensagem == "LIGAR") modoOperacao = 1;
    if (mensagem == "DESLIGAR") modoOperacao = 2;  // Atenção: O botão no site deve enviar "DESLIGAR" e não "OFF"
    if (mensagem == "ECO") modoOperacao = 3;

    // Aplica a mudança imediatamente
    gerenciarLuzes();
    enviarTelemetria();
  }

  // CASO 3: ALERTA GERAL DA DEFESA CIVIL
  else if (topicoRecebido == topico_recebe_geral) {
    mensagemPropaganda = "ALERTA: " + mensagem;
    telaAtual = 3;
    atualizarDisplay();
    // Sirene de alerta
    for (int i = 0; i < 3; i++) {
      tone(BUZZERPINO, 3000, 500);
      delay(500);
    }
  }
}

void conectarWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi OK");
}

void reconnect() {
  while (!client.connected()) {
    // Client ID também deve ser único
    String clientId = "PosteIoT_" + String(ID_POSTE);

    if (client.connect(clientId.c_str())) {
      // Se inscreve no tópico Específico E no tópico Geral
      client.subscribe(topico_recebe_propaganda.c_str());
      client.subscribe(topico_recebe_geral.c_str());
      client.subscribe(topico_recebe_comando.c_str());
    } else {
      delay(5000);
    }
  }
}
