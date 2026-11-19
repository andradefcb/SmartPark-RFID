/******************************************************************************/
/*  TESTE DE COMUNICAÇÃO SPI COM O RC522 (sem a biblioteca MFRC522)           */
/*  Monitor Serial: 9600 baud.                                                */
/*                                                                            */
/*  Descobre ONDE está a falha entre o Arduino e o leitor:                    */
/*   - Lê a versão do chip 20x em 3 velocidades (125 kHz, 1 MHz, 4 MHz).      */
/*   - Escreve valores num registrador e lê de volta (testa o fio MOSI).      */
/*   - Depois fica lendo a versão a cada 1 s: MEXA nos fios um por um e veja  */
/*     se o valor muda. O fio que faz o valor mudar é o que tem mau contato.  */
/******************************************************************************/

#include <SPI.h>

#define SS_PIN  10
#define RST_PIN 4

// Registradores do RC522
const byte REG_COMMAND   = 0x01;
const byte REG_TXCONTROL = 0x14;
const byte REG_TRELOAD_L = 0x2D; // registrador "livre" usado para teste de escrita
const byte REG_VERSION   = 0x37;

byte lerReg(byte reg, uint32_t clk) {
  SPI.beginTransaction(SPISettings(clk, MSBFIRST, SPI_MODE0));
  digitalWrite(SS_PIN, LOW);
  SPI.transfer(0x80 | ((reg << 1) & 0x7E));
  byte v = SPI.transfer(0x00);
  digitalWrite(SS_PIN, HIGH);
  SPI.endTransaction();
  return v;
}

void escreverReg(byte reg, byte valor, uint32_t clk) {
  SPI.beginTransaction(SPISettings(clk, MSBFIRST, SPI_MODE0));
  digitalWrite(SS_PIN, LOW);
  SPI.transfer((reg << 1) & 0x7E);
  SPI.transfer(valor);
  digitalWrite(SS_PIN, HIGH);
  SPI.endTransaction();
}

// Originais: 0x91/0x92. Clones usam vários outros valores (0x88, 0x12, 0xB2,
// 0x18...). O que importa é não ser 0x00/0xFF (sem resposta) e ser estável.
bool versaoConhecida(byte v) {
  return v != 0x00 && v != 0xFF;
}

// Retorna true se a velocidade passou em tudo
bool testarVelocidade(uint32_t clk, const __FlashStringHelper *nome) {
  byte primeira = lerReg(REG_VERSION, clk);
  int iguais = 0, zeros = 0, ffs = 0;
  for (int i = 0; i < 20; i++) {
    byte v = lerReg(REG_VERSION, clk);
    if (v == primeira) iguais++;
    if (v == 0x00) zeros++;
    if (v == 0xFF) ffs++;
    delay(5);
  }

  const byte padroes[] = { 0x55, 0xAA, 0x0F, 0xF0, 0x3C };
  int escritaOk = 0;
  for (byte i = 0; i < sizeof(padroes); i++) {
    escreverReg(REG_TRELOAD_L, padroes[i], clk);
    if (lerReg(REG_TRELOAD_L, clk) == padroes[i]) escritaOk++;
  }

  Serial.print(nome);
  Serial.print(F(": versao=0x"));
  Serial.print(primeira, HEX);
  Serial.print(versaoConhecida(primeira) ? F(" (responde)") : F(" (SEM RESPOSTA)"));
  Serial.print(F(" | estavel "));
  Serial.print(iguais);
  Serial.print(F("/20 | zeros "));
  Serial.print(zeros);
  Serial.print(F(" | FF "));
  Serial.print(ffs);
  Serial.print(F(" | escrita/leitura "));
  Serial.print(escritaOk);
  Serial.println(F("/5"));

  return versaoConhecida(primeira) && iguais == 20 && escritaOk == 5;
}

void setup() {
  Serial.begin(9600);
  while (!Serial) {}

  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH);

  // RST sempre em HIGH (chip ligado). Pulso LOW = reset de hardware.
  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, LOW);
  delay(10);
  digitalWrite(RST_PIN, HIGH);
  delay(100);

  SPI.begin();

  Serial.println();
  Serial.println(F("=== TESTE SPI RC522 ==="));
  bool ok125 = testarVelocidade(125000UL,  F("125 kHz"));
  bool ok1M  = testarVelocidade(1000000UL, F("1 MHz  "));
  bool ok4M  = testarVelocidade(4000000UL, F("4 MHz  "));

  // Liga a antena e confere
  escreverReg(REG_COMMAND, 0x0F, 125000UL); // soft reset
  delay(50);
  byte tx = lerReg(REG_TXCONTROL, 125000UL);
  escreverReg(REG_TXCONTROL, tx | 0x03, 125000UL);
  tx = lerReg(REG_TXCONTROL, 125000UL);
  Serial.print(F("Antena apos ligar: 0x"));
  Serial.print(tx, HEX);
  Serial.println((tx & 0x03) == 0x03 ? F(" -> LIGADA") : F(" -> NAO LIGOU"));

  Serial.println();
  Serial.println(F("=== CONCLUSAO ==="));
  if (ok125 && ok1M && ok4M) {
    Serial.println(F("SPI OK em todas as velocidades. A comunicacao esta boa."));
  } else if (ok125 && !ok4M) {
    Serial.println(F("OK devagar, falha rapido: sinal ruim. Use jumpers curtos,"));
    Serial.println(F("tire o leitor da protoboard e ligue direto com fios curtos."));
  } else {
    Serial.println(F("Falha ate em 125 kHz: problema fisico de ligacao/alimentacao."));
    Serial.println(F("- So zeros: nada volta no MISO. Confira MISO->12, SDA->10, RST->4, solda."));
    Serial.println(F("- So FF: MISO preso em alto. Confira MISO/MOSI trocados e SDA->10."));
    Serial.println(F("- Valor muda a cada leitura: mau contato. Veja abaixo qual fio."));
    Serial.println(F("- Escrita falha mas versao ok: fio MOSI->11 ou SCK->13."));
  }
  Serial.println();
  Serial.println(F("Agora lendo a versao a cada 1 s. Mexa em um fio por vez:"));
}

void loop() {
  byte v = lerReg(REG_VERSION, 125000UL);
  Serial.print(F("versao=0x"));
  Serial.print(v, HEX);
  Serial.println(versaoConhecida(v) ? F("  ok") : F("  <-- ERRADO"));
  delay(1000);
}
