//#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <GeoLinker.h>
#include <SoftwareSerial.h>
#include <ThingSpeak.h>


// GPS (SoftwareSerial) 
#define GPS_RX_PIN D1  
#define GPS_TX_PIN D2  
#define GPS_BAUD 9600
SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN); 

// Motor
#define motor1 D7
#define motor2 D8 

// SENSORES
const int PINO_SENSOR_PIR = D5;       
const int PINO_FIM_CURSO = D6;       
const int PINO_BOTAO_RESET = D3; // Cuidado: D3 é o pino Flash (GPIO0). Não aperte durante o boot.

// Thingspeak 
const char api_key = "HJQHF9V8BDU293LC";
unsigned long ChannelNumber = 3187507;

// CONFIGURAÇÃO GEOLINKER
const char* ssid = "yourSSID";             
const char* password = "yourPassword";     
const char* apiKey = "G999FeV5Kn5V";       
const char* deviceID = "ESP-32_Tracker";   

// Configurações do Tracker
const uint16_t updateInterval = 2;         
const bool enableOfflineStorage = true;   
const uint8_t offlineBufferLimit = 10;   
const bool enableAutoReconnect = true;     
const int8_t timeOffsetHours = -3; 
const int8_t timeOffsetMinutes = 0;

GeoLinker geo; 

//  VARIÁVEIS DE ESTADO
volatile bool alerta_pir = false;
volatile bool alerta_fim_curso = false;
volatile bool sistema_em_alerta = false;
volatile bool reset_pendente = false;
bool tracking_ativo = false; // Controla se o GPS deve enviar dados ou não

int estado_botao_anterior = HIGH; 

// INTERRUPÇÕES (ISR)
void IRAM_ATTR leituraPIR() {
  if (!reset_pendente) {
    alerta_pir = digitalRead(PINO_SENSOR_PIR);
  }
}

void IRAM_ATTR leituraFimCurso() {
  if (!reset_pendente) {
    alerta_fim_curso = digitalRead(PINO_FIM_CURSO);
  }
}

// ==================================================================
//                    SETUP
// ==================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nIniciando Sistema de Segurança Veicular...");

  // Configuração Pinos
  pinMode(PINO_SENSOR_PIR, INPUT);
  pinMode(PINO_FIM_CURSO, INPUT);
  pinMode(PINO_BOTAO_RESET, INPUT_PULLUP);

  // Interrupções
  attachInterrupt(digitalPinToInterrupt(PINO_SENSOR_PIR), leituraPIR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PINO_FIM_CURSO), leituraFimCurso, CHANGE);  
  
  // GPS
  gpsSerial.begin(GPS_BAUD);
  Serial.println("GPS Serial iniciado (RX: D1, TX: D2)"); 

  // GeoLinker
  geo.begin(gpsSerial);
  geo.setApiKey(apiKey);
  geo.setDeviceID(deviceID);
  geo.setUpdateInterval_seconds(updateInterval);
  geo.setDebugLevel(DEBUG_BASIC);          
  geo.enableOfflineStorage(enableOfflineStorage);
  geo.setOfflineBufferLimit(offlineBufferLimit);
  geo.enableAutoReconnect(enableAutoReconnect);
  geo.setTimeOffset(timeOffsetHours, timeOffsetMinutes);
  geo.setNetworkMode(GEOLINKER_WIFI);     
  geo.setWiFiCredentials(ssid, password);

  // Teste de WiFi
  Serial.print("Conectando ao WiFi: ");
  Serial.println(ssid);
  if (geo.connectToWiFi()) {
    Serial.println("WiFi Conectado! Aguardando gatilho de segurança...");
  } else {
    Serial.println("WiFi não conectado (será tentado novamente se houver roubo).");
  }

  // Servidor
  server.on("/", handleRoot);
  server.on("/ligar", ligarMotor);
  server.on("/desligar", desligarMotor);
  server.on("/voltar", voltar);

  server.begin();
  Serial.println("Servidor iniciado");
}


void loop() {
  // Servidor em execução
  server.handleClient();

  // --- 1. VERIFICA BOTÃO RESET ---
  int estado_botao_atual = digitalRead(PINO_BOTAO_RESET);

  if (estado_botao_anterior == HIGH && estado_botao_atual == LOW) {
      reset_pendente = true;
      Serial.println("Botão de Reset Pressionado");
  }
  estado_botao_anterior = estado_botao_atual;

  if (reset_pendente) {
    alerta_pir = false;
    alerta_fim_curso = false;
    sistema_em_alerta = false;
    tracking_ativo = false; // DESATIVA O RASTREADOR
    
    Serial.println("==========================================");
    Serial.println("SISTEMA RESETADO. CARRO SEGURO.");
    Serial.println("==========================================");
    
    reset_pendente = false;
  }

  // --- 2. LÓGICA DOS SENSORES ---
  if (alerta_pir || alerta_fim_curso) {
    if (!sistema_em_alerta) {
        sistema_em_alerta = true;
        Serial.println("------------------------------------------");
        Serial.println("ALERTA ATIVADO!");
        Serial.println("------------------------------------------");
    }
  } else {
      sistema_em_alerta = false;
  }

  // --- 3. AÇÃO DO ALARME ---
  if (sistema_em_alerta) {
    Serial.print("Estado: Alerta PIR=");
    Serial.print(alerta_pir ? "ON" : "OFF");
    Serial.print(" | Alerta Fim Curso=");
    Serial.println(alerta_fim_curso ? "ON" : "OFF");
    
    // Possibilidades
    if (alerta_pir && alerta_fim_curso) {
      // *** ROUBO CERTO ***
      Serial.println("* Porta Aberta E Alguém Entrou (Roubo Certo) *");
      
      // Ativa o rastreador se ainda não estiver ativo
      if (!tracking_ativo) {
         tracking_ativo = true;
         Serial.println(">>> INICIANDO PROTOCOLO DE RASTREAMENTO GPS <<<");
      }
      
    } else if (alerta_pir && !alerta_fim_curso) {
      Serial.println("* Alguém Está Dentro do Carro *");
    } else if (!alerta_pir && alerta_fim_curso) {
      Serial.println("Porta Aberta, Ninguém Entrou (Pré-Alerta)");
    } 
    
  } else {
    // Estado Seguro (Imprime mensagem a cada 5 segundos para não poluir o serial)
    static unsigned long ultimo_print = 0;
    if (millis() - ultimo_print > 5000) {
      Serial.println("Carro Seguro. Sensores OFF.");
      ultimo_print = millis();
    }
  }

  // --- 4. EXECUÇÃO DO RASTREADOR GPS ---
  if (tracking_ativo) {
      // Esta função gerencia tudo: leitura NMEA e envio WiFi
      uint8_t status = geo.loop(); 

      if (status == STATUS_SENT) {
         Serial.println("[GPS] Coordenadas enviadas para a nuvem!"); 
      } else if (status == STATUS_NETWORK_ERROR) {
         Serial.println("[GPS] Erro de Rede - Salvando Offline."); 
      }
  } else {
 
      delay(100);
  }

  // Thingspeak
  ThingSpeak.writeField(ChannelNumber, 1, alerta_pir, api_key);
  ThingSpeak.writeField(ChannelNumber, 2, alerta_fim_curso, api_key);
}

// Exibição da página Web
void handleRoot() {
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html lang="pt-br">
    <head>
      <meta charset="UTF-8">
      <meta name="viewport" content="width=device-width, inicial-scale=1.0">
      <title> Motor do Carro</title>
      <style>
        body {font-family: Arial, sans-serif; text-align: center; margin-top: 50px;}
        button {padding: 15px 25px; font-size: 20px; margin: 10px; cursor: pointer;}
      </style>
    </head>
    <body>
      <h1> Controle do Motor DC</h1>
      <button onclick="ligar()">Ligar Motor (Horário)</button>
      <button onclick="desligar()">Desligar Motor</button>
      <button onclick="voltar()">Ligar Motor (Anti-Horário)</button>

      <script>
        function ligar() {
          fetch('/ligar()');
        }
        function desligar() {
          fetch('/desligar()');
        }
        function voltar() {
          fetch('/voltar()');
        }
      </script>
    </body>
    </html>
  )rawliteral";
}

// Funções para ligar e desligar o motor
void ligarMotor() {
  motorLigado = true;
  digitalWrite(motor1, HIGH);
  digitalWrite(motor2, LOW);
  server.send(200, "text/plain", "Motor ligado! (Sentido Horário)");
}

void desligarMotor() {
  motorLigado = false;
  digitalWrite(motor1, LOW);
  digitalWrite(motor2, LOW);
  server.send(200, "text/plain", "Motor desligado!");
}

void voltar() {
    motorLigado = false;
  digitalWrite(motor1, LOW);
  digitalWrite(motor2, HIGH);
  server.send(200, "text/plain", "Motor ligado! (Sentido Anti-Horário)");
}
