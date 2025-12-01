#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>
#include <ThingSpeak.h>

#include <GeoLinker.h> 

// GPS 
#define GPS_RX_PIN D5
#define GPS_TX_PIN D6
#define GPS_BAUD 9600
SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN); 

// Motor
#define motor D4

// Sensores
const int PINO_SENSOR_PIR = D1;       
const int PINO_FIM_CURSO = D0;       
const int PINO_BOTAO_RESET = D2; // Cuidado: D3 é Flash (GPIO0).

// Redes e serviços

// Thingspeak 
const char* ts_api_key = "HJQHF9V8BDU293LC"; 
unsigned long ChannelNumber = 3187507;
WiFiClient client; 

// GeoLinker e WiFi
const char* ssid = "VIVO";             
const char* password = "91002540lu";     
const char* apiKey = "G999FeV5Kn5V";       
const char* deviceID = "ESP-32_Tracker";   

// Instâncias
ESP8266WebServer server(80); 
GeoLinker geo; 

// Configurações do GPS
const uint16_t updateInterval = 2;         
const bool enableOfflineStorage = true;   
const uint8_t offlineBufferLimit = 10;   
const bool enableAutoReconnect = true;     
const int8_t timeOffsetHours = -3; 
const int8_t timeOffsetMinutes = 0;


// Declaração de variáveis
volatile bool alerta_pir = false;
volatile bool alerta_fim_curso = false;
volatile bool sistema_em_alerta = false;
volatile bool reset_pendente = false;
bool tracking_ativo = false;
bool motorLigado = false; 

int estado_botao_anterior = HIGH; 
unsigned long lastThingSpeakUpdate = 0; 


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

// Funções que serão usadas pelos servidor / página web
void handleRoot();
void ligarMotor();
void desligarMotor();
void voltar();

// ==================================================================
// SETUP
// ==================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nIniciando Sistema de Segurança Veicular...");

  pinMode(PINO_SENSOR_PIR, INPUT);
  pinMode(PINO_FIM_CURSO, INPUT);
  pinMode(PINO_BOTAO_RESET, INPUT_PULLUP);

  pinMode(motor, OUTPUT);
  digitalWrite(motor, LOW);

  attachInterrupt(digitalPinToInterrupt(PINO_SENSOR_PIR), leituraPIR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PINO_FIM_CURSO), leituraFimCurso, CHANGE);  
  
  // GPS
  gpsSerial.begin(GPS_BAUD);
  Serial.println("GPS Serial iniciado (RX: D1, TX: D2)"); 

  // Thingspeak
  ThingSpeak.begin(client);

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

  Serial.print("Conectando ao WiFi: ");
  Serial.println(ssid);

  if (geo.connectToWiFi()) {
    Serial.println("WiFi Conectado! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi não conectado (será tentado novamente se houver roubo).");
  }

  // Web Server
  server.on("/", handleRoot);
  server.on("/ligar", ligarMotor);
  server.on("/desligar", desligarMotor);
  server.on("/voltar", voltar);

  server.begin();
  Serial.println("Servidor HTTP iniciado");
}

void loop() {
  server.handleClient();

  // BOTÃO RESET 
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
    tracking_ativo = false; 
    
    Serial.println("=== SISTEMA RESETADO. CARRO SEGURO. ===");
    
    reset_pendente = false;
    delay(500);
  }

  // Sistema entrando em estado de Alerta
  if (alerta_pir || alerta_fim_curso) {
    if (!sistema_em_alerta) {
        sistema_em_alerta = true;
        Serial.println("--- ALERTA ATIVADO! ---");
    }
  } else {
      sistema_em_alerta = false;
  }

  // Ação do Alarme
  if (sistema_em_alerta) {
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 2000) {
        Serial.printf("Status: PIR=%d | FimCurso=%d\n", alerta_pir, alerta_fim_curso);
        lastPrint = millis();
    }
    
    if (alerta_pir && alerta_fim_curso) {
      if (!tracking_ativo) {
         tracking_ativo = true;
         Serial.println(">>> INICIANDO RASTREAMENTO GPS <<<");
      }
    } 
  }

  // --- 4. EXECUÇÃO DO RASTREADOR GPS ---
  if (tracking_ativo) {
      uint8_t status = geo.loop(); 

      if (status == STATUS_SENT) {
         Serial.println("[GPS] Coordenadas enviadas!"); 
      } else if (status == STATUS_NETWORK_ERROR) {
         Serial.println("[GPS] Erro de Rede - Offline."); 
      }
  }

  // Envio de informações para o Thingspeak
  if (millis() - lastThingSpeakUpdate > 20000) { 
      if (WiFi.status() == WL_CONNECTED) {
          ThingSpeak.setField(1, (int)alerta_pir);
          ThingSpeak.setField(2, (int)alerta_fim_curso);
          
          int x = ThingSpeak.writeFields(ChannelNumber, ts_api_key);
          if(x == 200){
            Serial.println("ThingSpeak atualizado com sucesso.");
          } else {
            Serial.println("Erro na atualização do ThingSpeak. HTTP code: " + String(x));
          }
      }
      lastThingSpeakUpdate = millis();
  }
}

// Web Server
void handleRoot() {
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html lang="pt-br">
    <head>
      <meta charset="UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <title>Controle do Motor</title>
      <style>
        body {font-family: Arial, sans-serif; text-align: center; margin-top: 50px;}
        button {padding: 15px 25px; font-size: 20px; margin: 10px; cursor: pointer; border-radius: 5px;}
        .btn-on {background-color: #4CAF50; color: white;}
        .btn-off {background-color: #f44336; color: white;}
        .btn-rev {background-color: #008CBA; color: white;}
      </style>
    </head>
    <body>
      <h1>Controle do Motor DC</h1>
      <button class="btn-on" onclick="acao('/ligar')">Ligar Motor (Horário)</button>
      <br>
      <button class="btn-off" onclick="acao('/desligar')">Desligar Motor</button>
      <script>
        function acao(url) {
          fetch(url)
            .then(response => response.text())
            .then(data => alert(data))
            .catch(error => console.error('Erro:', error));
        }
      </script>
    </body>
    </html>
  )rawliteral";
  
  server.send(200, "text/html", html);
}

void ligarMotor() {
  motorLigado = true;
  digitalWrite(motor, HIGH);
  server.send(200, "text/plain", "Motor ligado!");
}

void desligarMotor() {
  motorLigado = false;
  digitalWrite(motor, LOW);
  server.send(200, "text/plain", "Motor desligado!");
}
