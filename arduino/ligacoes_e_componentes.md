# Guia de Ligações e Componentes - Sistema de Estacionamento RFID (PEX)

Este documento descreve as conexões elétricas e a pinagem utilizada no protótipo de cancela de estacionamento com leitor RFID **MFRC522**, Servo Motor, Sensor Ultrassônico **HC-SR04**, LEDs indicadores e Buzzer.

---

## 1. Lista de Componentes

| Quantidade | Componente | Descrição / Modelo |
| :---: | :--- | :--- |
| **1x** | Arduino Uno ou Nano | Placa controladora principal |
| **1x** | Módulo Leitor RFID RC522 | Leitura de tags e cartões RFID 13.56MHz (com pinos SPI) |
| **1x** | Servo Motor SG90 (ou similar) | Acionamento da haste da cancela |
| **1x** | Sensor Ultrassônico HC-SR04 | Detecção de presença/passagem do veículo |
| **1x** | LED Verde (5mm) | Indicador visual de passagem liberada |
| **1x** | LED Vermelho (5mm) | Indicador visual de cancela fechada / acesso negado |
| **2x** | Resistores de 220Ω ou 330Ω | Limitadores de corrente para os LEDs |
| **1x** | Buzzer Ativo (5V) | Emissão de bips de confirmação e alerta (opcional) |
| **1x** | Protoboard e Jumpers | Barramento de ligação M/M e M/F |

---

## 2. Pinagem Completa no Arduino Uno

### A. Leitor RFID RC522 (Comunicação SPI)
> **ATENÇÃO:** O pino **3.3V** deve ser ligado **exclusivamente na saída 3.3V do Arduino**. Ligar em 5V pode queimar o chip do leitor!

| Pino no Módulo RC522 | Pino no Arduino Uno | Observações |
| :--- | :---: | :--- |
| **VCC / 3.3V** | **3.3V** | **NUNCA** ligar nos 5V! |
| **RST** | **Pino 4** | Reset do módulo (Disposição Oficial) |
| **GND** | **GND** | Terra comum |
| **MISO** | **Pino 12** | Barramento SPI (Master In Slave Out) |
| **MOSI** | **Pino 11** | Barramento SPI (Master Out Slave In) |
| **SCK** | **Pino 13** | Barramento SPI (Clock) |
| **SDA / SS** | **Pino 10** | Slave Select do leitor RFID |
| **IRQ** | *Não conectado* | Deixar livre |

---

### B. Servo Motor (Cancela)

| Fio do Servo Motor | Pino no Arduino Uno |
| :--- | :---: |
| Marrom / Preto | **GND** |
| Vermelho | **5V** |
| Amarelo / Laranja (Sinal) | **Pino 6** |

---

### C. Sensor Ultrassônico HC-SR04

| Pino do Sensor | Pino no Arduino Uno |
| :--- | :---: |
| **VCC** | **5V** |
| **TRIG** | **Pino 3** |
| **ECHO** | **Pino 2** |
| **GND** | **GND** |

---

### D. Sinalização (LEDs e Buzzer)

| Componente | Conexão Positiva (+) | Conexão Negativa (-) |
| :--- | :---: | :---: |
| **LED Verde** | **Pino 7** (com resistor 220Ω) | **GND** |
| **LED Vermelho** | **Pino 8** (com resistor 220Ω) | **GND** |
| **Buzzer Ativo** | **Pino 5** | **GND** |

---

## 3. Instalação da Biblioteca RFID na Arduino IDE

Para compilar o código do Arduino:
1. Abra a **Arduino IDE**.
2. Vá em **Sketch -> Incluir Biblioteca -> Gerenciar Bibliotecas...** (ou `Ctrl + Shift + I`).
3. No campo de busca, digite: `MFRC522`.
4. Procure pela biblioteca **MFRC522 by GithubCommunity** (ou *MFRC522 by Miguel Balboa*).
5. Clique em **Instalar**.
6. A biblioteca `Servo.h` e `SPI.h` já vêm instaladas por padrão na Arduino IDE.

---

## 4. Comunicação Serial com o Sistema Web

O Arduino e a aplicação Web conversam através da porta USB (Baudrate: **9600**):

- **Mensagens enviadas pelo Arduino:**
  - `RFID:<UID>` (ex: `RFID:3A78F21B`) -> Enviado sempre que uma tag é aproximada.
  - `STATUS:CANCELA_ABERTA` -> Informa que a cancela subiu.
  - `STATUS:CANCELA_FECHADA` -> Informa que a cancela desceu.

- **Comandos aceitos pelo Arduino:**
  - `OPEN` -> Aciona a cancela (sobe o servo para 90°, acende LED verde e apaga vermelho).
  - `CLOSE` -> Abaixa a cancela (servo em 0°, acende LED vermelho).
  - `DENIED` -> Pisca o LED vermelho e toca alerta sonoro de acesso negado.

  - `DIAG` -> Refaz o autoteste do leitor RC522 (responde `INFO:RC522_OK versao=0x92` ou `ERRO:RC522_NAO_ENCONTRADO`).

---

## 5. Como Descobrir o Código (UID) de uma Tag RFID

Cada tag/cartão tem um código único (UID) gravado de fábrica, que o RC522 lê em hexadecimal (ex: `3A78F21B`). Há duas formas de descobrir:

### Opção A — Pelo site (recomendado)
1. Rode `node server.js` e abra `http://localhost:3000` no **Chrome ou Edge**.
2. Clique em **Conectar Arduino** e escolha a porta COM da placa.
   - Ao conectar aparece o aviso *"Leitor RFID RC522 detectado"*. Se aparecer *"não responde"*, revise a fiação.
3. Entre como admin → aba **Cadastro de Veículos** → botão **Ler Tag**.
4. Aproxime a tag do leitor. O código é preenchido sozinho no campo e o site avisa se a tag já estiver cadastrada.
5. Preencha nome, placa etc. e salve. A partir daí, aproximar essa tag registra a entrada e **abre a cancela**.

> Enquanto a janela "Ler Tag" estiver aberta, a leitura **não** abre a cancela nem registra entrada.

### Opção B — Pelo Monitor Serial da Arduino IDE
1. Feche o site (ou clique em desconectar Arduino) — a porta USB só pode ser usada por um programa de cada vez.
2. Arduino IDE → **Ferramentas → Monitor Serial**, velocidade **9600**.
3. Aproxime a tag: aparece `RFID:3A78F21B`. O código é o que vem depois de `RFID:`.
4. Digite esse código no cadastro do site (espaços e `:` são removidos automaticamente).

### Fluxo completo da cancela
```
Tag no leitor -> Arduino envia "RFID:<UID>" -> navegador (Web Serial) -> POST /api/leitura-rfid
   cadastrada e fora do pátio   -> registra entrada  -> navegador envia "OPEN"   -> cancela sobe
   cadastrada e paga (<15 min)  -> libera saída      -> navegador envia "OPEN"   -> cancela sobe
   não cadastrada               -> acesso negado     -> navegador envia "DENIED" -> buzzer + LED vermelho
```
