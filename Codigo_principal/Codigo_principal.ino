const int PINO_SENSOR_PIR = D1;      
const int PINO_FIM_CURSO = D2;       
const int PINO_BOTAO_RESET = D3;      

// Variáveis de Estado
volatile bool alerta_pir = false;
volatile bool alerta_fim_curso = false;
volatile bool sistema_em_alerta = false;
volatile bool reset_pendente = false;

int estado_botao_anterior = HIGH; 

void IRAM_ATTR leituraPIR() {
  if (!reset_pendente) {
    alerta_pir = digitalRead(PINO_SENSOR_PIR);
    Serial.println("Interrupção PIR acionada.");
  }
}

// Função de interrupção para o Sensor Fim de Curso
void IRAM_ATTR leituraFimCurso() {
  if (!reset_pendente) {
    alerta_fim_curso = digitalRead(PINO_FIM_CURSO);
    Serial.println("Interrupção Fim de Curso acionada.");
  }
}

// Função de interrupção para o Botão de Reset
void IRAM_ATTR handleResetButton() {
  if (digitalRead(PINO_BOTAO_RESET) == LOW) {
    reset_pendente = true;
    Serial.println("Botão de Reset Pressionado.");
  }
}


void setup() {
  Serial.begin(115200);
  Serial.println("\nIniciando Sistema de Segurança Veicular...");
  pinMode(PINO_SENSOR_PIR, INPUT);
  pinMode(PINO_FIM_CURSO, INPUT);
  pinMode(PINO_BOTAO_RESET, INPUT_PULLUP);  
}


void loop() {
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
    
    Serial.println("==========================================");
    Serial.println("SISTEMA RESETADO. CARRO SEGURO.");
    Serial.println("==========================================");
    
    reset_pendente = false;
  }

  // Lógica:
  
  // O sistema entra em ALERTA se alguma das situações a seguir for verdadeira (OR):
  // - alerta_pir = 1 E alerta_fim_curso = 1 (Porta aberta E alguém entrou)
  // - alerta_pir = 1 E alerta_fim_curso = 0 (Alguém está dentro do carro)
  // - alerta_pir = 0 E alerta_fim_curso = 1 (Porta foi aberta mas ninguém entrou) - Pode ser considerado um pré-alerta/alerta.
  // A situação de alerta_pir = 0 E alerta_fim_curso = 0 é a única que garante segurança.
  
  if (alerta_pir || alerta_fim_curso) {
    // Pelo menos um dos sensores foi acionado
    if (!sistema_em_alerta) {
        sistema_em_alerta = true;
        Serial.println("------------------------------------------");
        Serial.println("ALERTA ATIVADO!");
        Serial.println("------------------------------------------");
    }
  } else {
      // Se ambos forem 0 (carro seguro)
      sistema_em_alerta = false;
  }

  // Ação do Alerta
  if (sistema_em_alerta) {
    Serial.print("Estado: Alerta PIR=");
    Serial.print(alerta_pir ? "ON" : "OFF");
    Serial.print(" | Alerta Fim Curso=");
    Serial.print(alerta_fim_curso ? "ON" : "OFF");
    
    // Possibilidades
    if (alerta_pir && alerta_fim_curso) {
      Serial.println("* Porta Aberta E Alguém Entrou (Roubo Certo) *");
    } else if (alerta_pir && !alerta_fim_curso) {
      Serial.println("* Alguém Está Dentro do Carro *");
    } else if (!alerta_pir && alerta_fim_curso) {
      Serial.println("Porta Aberta, Ninguém Entrou (Pré-Alerta)");
    } 
    
  } else {
    // Estado Seguro
    static unsigned long ultimo_print = 0;
    if (millis() - ultimo_print > 5000) {
      Serial.println("Carro Seguro. Sensores OFF.");
      ultimo_print = millis();
    }
  }

  delay(50);
}