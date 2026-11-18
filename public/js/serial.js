/**
 * Módulo de Comunicação Web Serial API para Arduino Uno
 * Permite que o navegador se comunique diretamente com a placa Arduino via USB.
 */

class ArduinoSerialController {
  constructor() {
    this.port = null;
    this.reader = null;
    this.writer = null;
    this.readableStreamClosed = null;
    this.writableStreamClosed = null;
    this.conectado = false;
    this.buffer = '';
  }

  init() {
    // Configura botões de conectar Arduino (tanto no Login quanto na Barra Superior)
    document.addEventListener('click', (e) => {
      const btn = e.target.closest('.arduino-badge');
      if (btn) {
        if (!this.conectado) {
          this.conectar();
        } else {
          if (confirm('Deseja desconectar a placa Arduino?')) {
            this.desconectar();
          }
        }
      }
    });

    // Detecta desconexão física do cabo USB
    if ('serial' in navigator) {
      navigator.serial.addEventListener('disconnect', (event) => {
        console.warn('[Serial] Dispositivo USB desconectado fisicamente.');
        this.desconectar(true);
      });

      // Tenta reconexão automática com porta previamente autorizada
      this.tentarAutoConectar();
    } else {
      this.atualizarStatusUI(false, 'Serial não suportada');
      console.warn('Web Serial API não é suportada por este navegador. Utilize o Google Chrome ou Microsoft Edge.');
    }
  }

  async tentarAutoConectar() {
    try {
      const ports = await navigator.serial.getPorts();
      if (ports && ports.length > 0) {
        console.log('[Serial] Porta previamente autorizada encontrada. Tentando conectar...');
        this.port = ports[0];
        await this.iniciarConexaoPorta();
      }
    } catch (err) {
      console.log('[Serial] Auto-conexão em espera:', err.message);
    }
  }

  async conectar() {
    if (!('serial' in navigator)) {
      alert('Seu navegador não suporta Web Serial API. Para conectar o Arduino via USB, utilize o Google Chrome ou Microsoft Edge.\n\nVocê também pode utilizar o botão "Simular RFID" para testar sem a placa física!');
      return;
    }

    try {
      // Solicita seleção da porta COM do Arduino Uno
      this.port = await navigator.serial.requestPort();
      await this.iniciarConexaoPorta();
      window.mostrarToast && window.mostrarToast('Arduino Uno conectado com sucesso à porta USB!', 'success');
    } catch (err) {
      console.error('Falha ao conectar com o Arduino:', err);
      this.atualizarStatusUI(false, 'Conectar Arduino');
      if (err.name !== 'NotFoundError') {
        alert('Erro ao conectar com a porta Serial: ' + err.message);
      }
    }
  }

  async iniciarConexaoPorta() {
    if (!this.port) return;

    await this.port.open({ baudRate: 9600 });
    this.conectado = true;
    this.atualizarStatusUI(true, 'Arduino Conectado');

    // Configura escritor para envio de comandos (OPEN, CLOSE, DENIED, TEST, PING)
    const textEncoder = new TextEncoderStream();
    this.writableStreamClosed = textEncoder.readable.pipeTo(this.port.writable);
    this.writer = textEncoder.writable.getWriter();

    // Inicia escuta contínua de dados recebidos do Arduino
    this.lerDadosSerial();
  }

  async lerDadosSerial() {
    const textDecoder = new TextDecoderStream();
    this.readableStreamClosed = this.port.readable.pipeTo(textDecoder.writable);
    this.reader = textDecoder.readable.getReader();

    try {
      while (true) {
        const { value, done } = await this.reader.read();
        if (done) {
          break;
        }
        if (value) {
          this.processarTextoRecebido(value);
        }
      }
    } catch (err) {
      console.warn('Conexão serial encerrada ou interrompida:', err.message);
    } finally {
      if (this.reader) {
        this.reader.releaseLock();
      }
      this.desconectar(true);
    }
  }

  processarTextoRecebido(texto) {
    this.buffer += texto;
    const linhas = this.buffer.split('\n');

    // Mantém a última linha incompleta no buffer
    this.buffer = linhas.pop();

    for (let linha of linhas) {
      linha = linha.trim();
      if (!linha) continue;

      console.log('[Arduino Uno]:', linha);

      // 1. Detecção de Código RFID (formato "RFID:A1B2C3D4")
      if (linha.startsWith('RFID:')) {
        const rfidCode = linha.replace('RFID:', '').trim().toUpperCase();
        console.log('>>> Cartão RFID Detectado no Leitor Físico:', rfidCode);

        // Dispara evento global no navegador
        window.dispatchEvent(new CustomEvent('rfid-lido', {
          detail: { rfid: rfidCode, origem: 'arduino' }
        }));
      }

      // 2. Notificações de Status do Arduino (Cancela, Sensor Ultrassônico, etc)
      if (linha.startsWith('STATUS:') || linha.startsWith('EVENTO:')) {
        window.dispatchEvent(new CustomEvent('arduino-status', {
          detail: { status: linha }
        }));
      }
    }
  }

  async enviarComando(comando) {
    if (!this.conectado || !this.writer) {
      console.log('[Simulação Hardware] Comando enviado para placa:', comando.trim());
      return false;
    }

    try {
      const cmdFormatado = comando.trim() + '\n';
      await this.writer.write(cmdFormatado);
      console.log('[Serial Enviado ao Arduino]:', cmdFormatado.trim());
      return true;
    } catch (err) {
      console.error('Erro ao enviar comando para o Arduino:', err);
      return false;
    }
  }

  // Atalhos para controle físico
  abrirCancela() { return this.enviarComando('OPEN'); }
  fecharCancela() { return this.enviarComando('CLOSE'); }
  testarCancela() { return this.enviarComando('TEST'); }
  negarAcesso() { return this.enviarComando('DENIED'); }

  async desconectar(silencioso = false) {
    try {
      this.conectado = false;
      if (this.reader) {
        await this.reader.cancel().catch(() => {});
      }
      if (this.writer) {
        await this.writer.close().catch(() => {});
      }
      if (this.port) {
        await this.port.close().catch(() => {});
      }
    } catch (err) {
      console.warn('Erro ao fechar porta serial:', err);
    } finally {
      this.port = null;
      this.reader = null;
      this.writer = null;
      this.atualizarStatusUI(false, 'Conectar Arduino');
      if (!silencioso) {
        window.mostrarToast && window.mostrarToast('Arduino desconectado.', 'info');
      }
    }
  }

  atualizarStatusUI(conectado, texto) {
    // Atualiza todos os botões de badge (no login e no header)
    document.querySelectorAll('.arduino-badge').forEach(badge => {
      badge.className = `arduino-badge ${conectado ? 'connected' : 'disconnected'}`;
      const textSpan = badge.querySelector('.arduino-status-text') || badge.querySelector('#arduino-status-text');
      if (textSpan) {
        textSpan.innerText = texto;
      }
    });
  }
}

// Instância global acessível no sistema
const arduinoSerial = new ArduinoSerialController();
document.addEventListener('DOMContentLoaded', () => {
  arduinoSerial.init();
});
