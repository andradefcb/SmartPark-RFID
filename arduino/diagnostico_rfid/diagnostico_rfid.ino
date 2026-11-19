/******************************************************************************/
/*  DIAGNÓSTICO DO LEITOR RFID RC522 - SmartPark PEX                          */
/*  Use este sketch SÓ para testar o leitor, sem servo/ultrassônico/buzzer.   */
/*  Monitor Serial: 9600 baud.                                                */
/*                                                                            */
/*  O que ele faz:                                                            */
/*   - Mostra a versão do chip e se a antena está ligada.                     */
/*   - A cada 2 s imprime "." (prova que o Arduino não travou).               */
/*   - Detecta qualquer cartão 13,56 MHz, inclusive um que ficou "parado"     */
/*     encostado no leitor, e imprime UID e tipo.                             */
/*   - Se a antena desligar sozinha (queda de tensão), religa e avisa.        */
/******************************************************************************/

#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN  10
#define RST_PIN 4

MFRC522 rfid(SS_PIN, RST_PIN);

unsigned long ultimoPonto = 0;
unsigned long ultimaChecagem = 0;

void imprimirRegistradores() {
  byte versao = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  byte tx     = rfid.PCD_ReadRegister(MFRC522::TxControlReg);
  byte ganho  = rfid.PCD_GetAntennaGain();

  Serial.print(F("Versao do chip: 0x"));
  Serial.println(versao, HEX);
  Serial.print(F("Antena (TxControlReg): 0x"));
  Serial.print(tx, HEX);
  Serial.println((tx & 0x03) == 0x03 ? F("  -> LIGADA") : F("  -> DESLIGADA!"));
  Serial.print(F("Ganho da antena: 0x"));
  Serial.println(ganho, HEX);
}

void setup() {
  Serial.begin(9600);
  while (!Serial) {}

  SPI.begin();
  rfid.PCD_Init();
  pinMode(RST_PIN, OUTPUT);   // mantém o chip ligado (RST em HIGH)
  digitalWrite(RST_PIN, HIGH);
  delay(50);
  rfid.PCD_AntennaOn();
  rfid.PCD_SetAntennaGain(rfid.RxGain_max);

  Serial.println();
  Serial.println(F("=== DIAGNOSTICO RC522 ==="));
  imprimirRegistradores();
  Serial.println(F("Encoste o cartao/tag na antena (parte com as trilhas), a 0-2 cm."));
}

void loop() {
  unsigned long agora = millis();

  // Heartbeat: um ponto a cada 2 s
  if (agora - ultimoPonto >= 2000) {
    ultimoPonto = agora;
    Serial.print('.');
  }

  // A cada 5 s confere se a antena continua ligada
  if (agora - ultimaChecagem >= 5000) {
    ultimaChecagem = agora;
    byte tx = rfid.PCD_ReadRegister(MFRC522::TxControlReg);
    if ((tx & 0x03) != 0x03) {
      Serial.println();
      Serial.println(F("AVISO: antena desligou (provavel queda de tensao). Reiniciando leitor..."));
      rfid.PCD_Init();
      pinMode(RST_PIN, OUTPUT);
      digitalWrite(RST_PIN, HIGH);
      rfid.PCD_AntennaOn();
      rfid.PCD_SetAntennaGain(rfid.RxGain_max);
      imprimirRegistradores();
    }
  }

  // WakeupA acorda também cartões que ficaram em estado HALT
  byte atqa[2];
  byte tamanho = sizeof(atqa);
  MFRC522::StatusCode st = rfid.PICC_WakeupA(atqa, &tamanho);
  if (st != MFRC522::STATUS_OK && st != MFRC522::STATUS_COLLISION) {
    delay(50);
    return;
  }

  Serial.println();
  Serial.println(F(">>> Cartao detectado no campo!"));

  if (!rfid.PICC_ReadCardSerial()) {
    Serial.println(F("    ...mas falhou ao ler o UID (afaste e encoste de novo)."));
    delay(300);
    return;
  }

  Serial.print(F("RFID:"));
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) Serial.print('0');
    Serial.print(rfid.uid.uidByte[i], HEX);
  }
  Serial.println();

  MFRC522::PICC_Type tipo = rfid.PICC_GetType(rfid.uid.sak);
  Serial.print(F("Tipo: "));
  Serial.println(rfid.PICC_GetTypeName(tipo));

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  delay(1000);
}
