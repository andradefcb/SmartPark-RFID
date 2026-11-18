/******************************************************************************/
/*  Projeto PEX 2° Período: Sistema de Estacionamento Inteligente com RFID    */
/*  VERSÃO: Arduino Uno / Nano (ATmega328P)                                   */
/*  Componentes: Arduino Uno, MFRC522 (RFID), Servo Motor SG90 (Cancela),     */
/*               Sensor Ultrassônico HC-SR04, LEDs Indicadores e Buzzer       */
/******************************************************************************/

#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>

// ====================================================================
// MAPEAMENTO OFICIAL DE PINOS NO ARDUINO UNO
// ====================================================================

// --- LEITOR RFID RC522 (Comunicação SPI por Hardware) ---
// ⚠️ ATENÇÃO: Ligue o pino 3.3V do RC522 EXCLUSIVAMENTE nos 3.3V do Arduino Uno!
//    NUNCA ligue no 5V para não danificar o leitor RFID.
// -------------------------------------------------------------
// Pino do RC522        Liga no Arduino Uno
// SDA (SS)      ----->  Pino Digital 10
// SCK           ----->  Pino Digital 13
// MOSI          ----->  Pino Digital 11
// MISO          ----->  Pino Digital 12
// RST           ----->  Pino Digital 4
// 3.3V          ----->  Pino 3.3V (NUNCA 5V)
// GND           ----->  GND
// IRQ           ----->  Não conecta
// -------------------------------------------------------------
#define SS_PIN       10   // Pino SDA (SS) do RC522 -> Digital 10
#define RST_PIN      4    // Pino RST do RC522     -> Digital 4

// --- ATUADORES E DEMAIS SENSORES ---
#define PINO_SERVO     6  // Sinal PWM do Servo Motor da Cancela (Pino Digital 6)
#define PINO_TRIG      3  // Pino TRIG do sensor ultrassônico HC-SR04
#define PINO_ECHO      2  // Pino ECHO do sensor ultrassônico HC-SR04
#define PINO_LED_VERDE 7  // LED Verde: Cancela aberta (com resistor 220Ω)
#define PINO_LED_VERM  8  // LED Vermelho: Cancela fechada (com resistor 220Ω)
#define PINO_BUZZER    5  // Buzzer: Alerta sonoro de acesso negado

// ====================================================================
// CONFIGURAÇÃO DO BUZZER (Ativo em Nível Baixo vs Alto)
// ====================================================================
// A grande maioria dos módulos de buzzer para Arduino (placa com 3 pinos)
// aciona em nível BAIXO (LOW = apita, HIGH = desligado).
// Se o buzzer ficava apitando direto ao ligar, deixe BUZZER_ATIVO_LOW = true.
// Se estiver usando um buzzer comum de 2 pernas ligado direto ao GND, mude para false.
const bool BUZZER_ATIVO_LOW = true;

// --- CONFIGURAÇÃO DOS ÂNGULOS DO SERVO E DISTÂNCIA ---
const int ANGULO_FECHADO = 10;     // Cancela abaixada (10° evita forçar o batente físico do SG90)
const int ANGULO_ABERTO  = 90;     // Cancela levantada (90°)
const int DISTANCIA_DETECCAO = 15; // Distância de passagem em cm (HC-SR04)

// --- OBJETOS ---
MFRC522 rfid(SS_PIN, RST_PIN);
Servo servoCancela;

// --- VARIÁVEIS DE CONTROLE ---
bool cancelaAberta = false;
unsigned long tempoAbertura = 0;
const unsigned long TEMPO_MAXIMO_ABERTA = 7000; // Tempo máximo de cancela aberta por segurança (7s)
String ultimoUidLido = "";
unsigned long tempoUltimaLeitura = 0;
const unsigned long INTERVALO_LEITURA_RFID = 2500; // Evita leitura repetida do mesmo cartão em 2.5s

// ====================================================================
// FUNÇÕES AUXILIARES DO BUZZER (APENAS PARA ACESSO NEGADO)
// ====================================================================
void desligarBuzzer() {
  digitalWrite(PINO_BUZZER, BUZZER_ATIVO_LOW ? HIGH : LOW);
}

void ligarBuzzer() {
  digitalWrite(PINO_BUZZER, BUZZER_ATIVO_LOW ? LOW : HIGH);
}

// ====================================================================
// CONFIGURAÇÃO INICIAL (SETUP)
// ====================================================================
void setup() {
  Serial.begin(9600);
  Serial.setTimeout(50);

  // 1. Configura o pino do buzzer iniciando TOTALMENTE DESLIGADO (silencioso)
  desligarBuzzer();
  pinMode(PINO_BUZZER, OUTPUT);
  desligarBuzzer();

  // 2. Configura os pinos dos LEDs e ultrassônico
  pinMode(PINO_TRIG, OUTPUT);
  pinMode(PINO_ECHO, INPUT);
  pinMode(PINO_LED_VERDE, OUTPUT);
  pinMode(PINO_LED_VERM, OUTPUT);

  // 3. Inicializa barramento SPI padrão do Arduino Uno e o leitor RC522
  SPI.begin();
  rfid.PCD_Init();
  rfid.PCD_SetAntennaGain(rfid.RxGain_max); // Máxima sensibilidade de leitura RFID
  verificarLeitorRFID(); // Autoteste: confirma se o RC522 está respondendo no SPI

  // 4. Inicialização e autoteste do servomotor (cancela)
  // Define faixa de pulso segura do micro-servo SG90 (544us a 2400us)
  servoCancela.attach(PINO_SERVO, 544, 2400);

  // Teste de movimento inicial para conferir visualmente o funcionamento:
  servoCancela.write(ANGULO_ABERTO);  // Sobe brevemente a cancela
  digitalWrite(PINO_LED_VERDE, HIGH);
  digitalWrite(PINO_LED_VERM, LOW);
  delay(500);

  servoCancela.write(ANGULO_FECHADO); // Abaixa a cancela
  digitalWrite(PINO_LED_VERDE, LOW);
  digitalWrite(PINO_LED_VERM, HIGH);
  cancelaAberta = false;

  Serial.println(F("ARDUINO_UNO_SMARTPARK_PRONTO"));
  Serial.println(F("Buzzer: Silencioso (ativo apenas em acesso negado)"));
  Serial.println(F("RFID RC522: Pinos configurados (RST=4, SS=10, MOSI=11, MISO=12, SCK=13)"));
  Serial.println(F("Aguardando aproximacao de cartao RFID..."));
}

// ====================================================================
// LOOP PRINCIPAL
// ====================================================================
void loop() {
  // 1. Processa comandos recebidos do computador / site via Serial (OPEN, CLOSE, DENIED, TEST, PING)
  verificarComandosSerial();

  // 2. Realiza leitura do leitor RFID MFRC522
  verificarLeituraRFID();

  // 3. Gerencia fechamento inteligente da cancela após passagem do veículo ou timeout
  gerenciarFechamentoCancela();

  delay(40);
}

// ====================================================================
// AUTOTESTE DO LEITOR RC522
// Lê o registrador de versão do chip. 0x00 ou 0xFF = leitor não responde
// (fio solto, SDA/RST trocados, alimentação errada).
// ====================================================================
void verificarLeitorRFID() {
  byte versao = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  if (versao == 0x00 || versao == 0xFF) {
    Serial.println(F("ERRO:RC522_NAO_ENCONTRADO"));
    Serial.println(F("Confira as ligacoes: SDA=10, SCK=13, MOSI=11, MISO=12, RST=4, 3.3V e GND."));
  } else {
    Serial.print(F("INFO:RC522_OK versao=0x"));
    Serial.println(versao, HEX);
  }
}

// ====================================================================
// LEITURA DO CARTÃO RFID (MFRC522)
//
// COMO DESCOBRIR O CÓDIGO DE UMA TAG:
//   1. Arduino IDE > Ferramentas > Monitor Serial (9600 baud).
//   2. Aproxime a tag do leitor.
//   3. Aparece uma linha "RFID:3A78F21B" -> o código é 3A78F21B.
//   (Feche o Monitor Serial antes de conectar o site: a porta USB
//    só pode ser usada por um programa de cada vez.)
// ====================================================================
void verificarLeituraRFID() {
  // Detecta se há um novo cartão RFID aproximado
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  // Realiza a leitura dos dados do cartão
  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  // Formata o UID do cartão em hexadecimal maiúsculo (ex: A1B2C3D4)
  String uidAtual = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) {
      uidAtual += "0";
    }
    uidAtual += String(rfid.uid.uidByte[i], HEX);
  }
  uidAtual.toUpperCase();

  // Filtro de tempo para evitar leituras repetidas imediatas do mesmo cartão
  unsigned long agora = millis();
  if (uidAtual == ultimoUidLido && (agora - tempoUltimaLeitura < INTERVALO_LEITURA_RFID)) {
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  ultimoUidLido = uidAtual;
  tempoUltimaLeitura = agora;

  // Envia no formato padrão esperado pela aplicação web: "RFID:<UID>"
  // (Nota: o buzzer permanece silencioso aqui conforme solicitado)
  Serial.print(F("RFID:"));
  Serial.println(uidAtual);

  // Finaliza a comunicação com o cartão atual
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ====================================================================
// PROCESSAMENTO DE COMANDOS ENVIADOS PELO SISTEMA WEB VIA SERIAL
// ====================================================================
void verificarComandosSerial() {
  if (Serial.available() > 0) {
    String comando = Serial.readStringUntil('\n');
    comando.trim();
    comando.toUpperCase();

    if (comando == "OPEN" || comando == "ABRIR") {
      abrirCancela();
      Serial.println(F("STATUS:CANCELA_ABERTA"));
    } else if (comando == "CLOSE" || comando == "FECHAR") {
      fecharCancela();
      Serial.println(F("STATUS:CANCELA_FECHADA"));
    } else if (comando == "DENIED" || comando == "RECUSADO") {
      notificarAcessoNegado();
      Serial.println(F("STATUS:ACESSO_NEGADO"));
    } else if (comando == "PING") {
      Serial.println(F("PONG"));
    } else if (comando == "DIAG" || comando == "DIAGNOSTICO") {
      verificarLeitorRFID();
    } else if (comando == "TEST" || comando == "TESTAR") {
      Serial.println(F("TESTE_SERVO_EXECUTANDO"));
      abrirCancela();
      delay(2000);
      fecharCancela();
      Serial.println(F("TESTE_SERVO_FINALIZADO"));
    }
  }
}

// ====================================================================
// CONTROLE DA CANCELA (SERVO, SENSOR ULTRASSÔNICO E LEDS)
// ====================================================================
void abrirCancela() {
  servoCancela.write(ANGULO_ABERTO);
  digitalWrite(PINO_LED_VERDE, HIGH);
  digitalWrite(PINO_LED_VERM, LOW);
  cancelaAberta = true;
  tempoAbertura = millis();
}

void fecharCancela() {
  servoCancela.write(ANGULO_FECHADO);
  digitalWrite(PINO_LED_VERDE, LOW);
  digitalWrite(PINO_LED_VERM, HIGH);
  cancelaAberta = false;
}

void gerenciarFechamentoCancela() {
  if (!cancelaAberta) return;

  unsigned long tempoPassado = millis() - tempoAbertura;
  int distancia = lerDistanciaUltrassom();

  // Fecha após tempo limite de segurança (7 segundos)
  if (tempoPassado >= TEMPO_MAXIMO_ABERTA) {
    fecharCancela();
    Serial.println(F("EVENTO:CANCELA_FECHADA_TEMPO"));
    return;
  }

  // Fecha automaticamente quando o carro se afastar do sensor ultrassônico (após 2.5s)
  if (tempoPassado > 2500 && distancia > DISTANCIA_DETECCAO) {
    fecharCancela();
    Serial.println(F("EVENTO:CANCELA_FECHADA_PASSAGEM"));
  }
}

// Medição ultrassônica em centímetros (HC-SR04)
int lerDistanciaUltrassom() {
  digitalWrite(PINO_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PINO_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PINO_TRIG, LOW);

  long duracao = pulseIn(PINO_ECHO, HIGH, 25000); // Timeout de 25ms
  if (duracao == 0) return 999;
  return duracao / 58;
}

// ====================================================================
// FEEDBACK DE ACESSO NEGADO (ÚNICO MOMENTO EM QUE O BUZZER TOCA)
// ====================================================================
void notificarAcessoNegado() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(PINO_LED_VERM, LOW);
    ligarBuzzer(); // Ativa buzzer
    delay(120);
    digitalWrite(PINO_LED_VERM, HIGH);
    desligarBuzzer(); // Desativa buzzer
    delay(120);
  }
  desligarBuzzer(); // Garante silêncio após os bips
}
