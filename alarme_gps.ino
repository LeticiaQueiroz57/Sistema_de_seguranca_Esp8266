#include <GeoLinker.h>
#include <SoftwareSerial.h>

// ==================================================================
//                    CONFIGURAÇÃO DE HARDWARE
// ==================================================================

// --- GPS (SoftwareSerial) ---
#define GPS_RX_PIN D1  
#define GPS_TX_PIN D2  
#define GPS_BAUD 9600
SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN); 

// --- SENSORES ---
const int PINO_SENSOR_PIR = D5;       
const int PINO_FIM_CURSO = D6;       
const int PINO_BOTAO_RESET = D3; // Cuidado: D3 é o pino Flash (GPIO0). Não aperte durante o boot.

// ==================================================================
//                    CONFIGURAÇÃO GEOLINKER
// ==================================================================
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

// ==================================================================
//                    VARIÁVEIS DE ESTADO
// ==================================================================
volatile bool alerta_pir = false;
volatile bool alerta_fim_curso = false;
volatile bool sistema_em_alerta = false;
volatile bool reset_pendente = false;
bool tracking_ativo = false; // Controla se o GPS deve enviar dados ou não

int estado_botao_anterior = HIGH; 

// ==================================================================
//                    INTERRUPÇÕES (ISR)
// ==================================================================

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
}

// ==================================================================
//                    LOOP PRINCIPAL
// ==================================================================

void loop() {
  
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
}
