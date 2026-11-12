/**
 * Módulo de Integração PIX - SmartPark RFID
 * Projeto PEX 2° Período
 *
 * Responsável por:
 * 1. Gerar cobranças PIX dinâmicas com QR Code e código "Pix Copia e Cola" (Padrão BR Code EMV).
 * 2. Receber e processar confirmações de pagamento (Webhook oficial de APIs bancárias:
 *    Mercado Pago, EFI Bank / Gerencianet, Banco Inter, Asaas, etc.).
 * 3. Liberar o ticket fiscal/comprovante e acionar a cancela SOMENTE após a confirmação do pagamento.
 */

// Carrega automaticamente o arquivo .env se estiver no Node 20.6+
try {
  if (typeof process.loadEnvFile === 'function') {
    process.loadEnvFile();
  }
} catch (e) {
  // Ignora se o .env não existir ou já tiver sido carregado
}

// ====================================================================
// CONFIGURAÇÕES DA API DO PIX
// (Aqui você insere as credenciais do seu gateway bancário)
// ====================================================================
const CONFIG_PIX = {
  // Provider atual: 'manual' (chave estática), 'mercadopago', 'efi_gerencianet', 'inter', 'asaas'
  provider: process.env.PIX_PROVIDER || 'manual',

  // Dados da Chave PIX Padrão
  chave: process.env.PIX_CHAVE || 'andradefcb1212@gmail.com',
  tipoChave: 'EMAIL', // CPF, CNPJ, TELEFONE, EMAIL, ALEATORIA
  beneficiario: 'SmartPark Estacionamento PEX',
  cidade: 'MANAUS',

  // Credenciais para APIs Reais (preencher quando for usar produção)
  mercadopago: {
    accessToken: process.env.MP_ACCESS_TOKEN || 'SEU_ACCESS_TOKEN_MERCADO_PAGO'
  },
  efi_gerencianet: {
    clientId: process.env.EFI_CLIENT_ID || 'SEU_CLIENT_ID_EFI',
    clientSecret: process.env.EFI_CLIENT_SECRET || 'SEU_CLIENT_SECRET_EFI',
    certificado: process.env.EFI_CERTIFICADO_PATH || './certificado.p12'
  }
};

/**
 * Gera o payload oficial do Pix Copia e Cola (BR Code)
 * baseado no padrão do Banco Central (EMV QRCPS-MPM).
 */
function gerarPayloadPixCopiaECola({ chave, beneficiario, cidade, valor, txid = 'SMARTPARK' }) {
  const formatField = (id, value) => {
    const len = value.length.toString().padStart(2, '0');
    return `${id}${len}${value}`;
  };

  const valorStr = Number(valor).toFixed(2);
  const txidFormatado = txid.replace(/[^a-zA-Z0-9]/g, '').slice(0, 25) || 'SMARTPARK';

  // 00: Payload Format Indicator (01)
  let payload = formatField('00', '01');

  // 26: Merchant Account Information - Pix
  const gui = formatField('00', 'br.gov.bcb.pix');
  const key = formatField('01', chave);
  payload += formatField('26', gui + key);

  // 52: Merchant Category Code (0000)
  payload += formatField('52', '0000');

  // 53: Transaction Currency (986 = BRL)
  payload += formatField('53', '986');

  // 54: Transaction Amount
  if (Number(valor) > 0) {
    payload += formatField('54', valorStr);
  }

  // 58: Country Code (BR)
  payload += formatField('58', 'BR');

  // 59: Merchant Name
  const nomeBeneficiario = (beneficiario || 'SmartPark').slice(0, 25);
  payload += formatField('59', nomeBeneficiario);

  // 60: Merchant City
  const cidadeBeneficiario = (cidade || 'MANAUS').slice(0, 15);
  payload += formatField('60', cidadeBeneficiario);

  // 62: Additional Data Field (txid)
  const txidField = formatField('05', txidFormatado);
  payload += formatField('62', txidField);

  // 63: CRC16 Checksum
  payload += '6304';
  const crc = calcularCRC16(payload);
  return payload + crc;
}

// Algoritmo CRC16-CCITT (Polinômio 0x1021)
function calcularCRC16(payload) {
  let crc = 0xFFFF;
  for (let i = 0; i < payload.length; i++) {
    crc ^= (payload.charCodeAt(i) << 8);
    for (let j = 0; j < 8; j++) {
      if ((crc & 0x8000) !== 0) {
        crc = ((crc << 1) ^ 0x1021) & 0xFFFF;
      } else {
        crc = (crc << 1) & 0xFFFF;
      }
    }
  }
  return crc.toString(16).toUpperCase().padStart(4, '0');
}

/**
 * Gera dados da cobrança PIX para uma sessão
 */
function gerarCobrancaPix(params = {}) {
  const sessaoId = params.sessaoId || 'SES-001';
  const valor = Number(params.valor || 0);
  const placa = params.placa || '';
  const titular = params.titular || '';

  const txid = 'TX' + String(sessaoId).replace(/[^0-9]/g, '').slice(-15);
  const copiaECola = gerarPayloadPixCopiaECola({
    chave: CONFIG_PIX.chave,
    beneficiario: CONFIG_PIX.beneficiario,
    cidade: CONFIG_PIX.cidade,
    valor,
    txid
  });

  return {
    txid,
    sessaoId,
    valor,
    placa,
    titular,
    chave: CONFIG_PIX.chave,
    beneficiario: CONFIG_PIX.beneficiario,
    cidade: CONFIG_PIX.cidade,
    copiaECola,
    status: 'AGUARDANDO_PAGAMENTO',
    criadoEm: new Date().toISOString()
  };
}

// ====================================================================
// MERCADO PAGO: cobrança PIX dinâmica + consulta de status
// O pagamento só é considerado confirmado quando a API do Mercado Pago
// responde status "approved" (nunca pelo conteúdo cru do webhook).
// ====================================================================
const crypto = require('crypto');
const MP_API = 'https://api.mercadopago.com';

function usandoMercadoPago() {
  return CONFIG_PIX.provider === 'mercadopago';
}

function getMercadoPagoAccessToken() {
  return (process.env.MP_ACCESS_TOKEN || CONFIG_PIX.mercadopago.accessToken || '').trim();
}

function validarTokenMercadoPago(token) {
  if (!token || token === 'SEU_ACCESS_TOKEN_MERCADO_PAGO') {
    return 'Token de acesso não configurado no .env (MP_ACCESS_TOKEN). Preencha com suas credenciais do Mercado Pago.';
  }
  if (token.startsWith('APP_USR-TEST-')) {
    return "Token com formato inválido ('APP_USR-TEST-...'). Não misture prefixos: se for usar credenciais de Teste use apenas 'TEST-...', e se for de Produção use apenas 'APP_USR-...'.";
  }
  if (!token.startsWith('APP_USR-') && !token.startsWith('TEST-')) {
    return "Token de acesso do Mercado Pago em formato inesperado. Tokens válidos iniciam com 'APP_USR-' (Produção) ou 'TEST-' (Sandbox).";
  }
  return null;
}

function formatarErroMercadoPago(data, statusHttp) {
  let detalhe = data.message || `HTTP ${statusHttp}`;
  if (Array.isArray(data.cause) && data.cause.length > 0) {
    const causas = data.cause
      .map(c => (typeof c === 'object' ? (c.description || c.code || JSON.stringify(c)) : String(c)))
      .filter(Boolean)
      .join(' | ');
    if (causas) detalhe += ` (${causas})`;
  } else if (data.error) {
    detalhe += ` [${data.error}]`;
  }
  return detalhe;
}

function headersMP(extra = {}) {
  const token = getMercadoPagoAccessToken();
  return {
    'Content-Type': 'application/json',
    'Authorization': `Bearer ${token}`,
    ...extra
  };
}

function validarCPF(cpf) {
  const c = String(cpf || '').replace(/\D/g, '');
  if (c.length !== 11 || /^(\d)\1{10}$/.test(c)) return false;
  let soma = 0;
  for (let i = 0; i < 9; i++) soma += parseInt(c.charAt(i), 10) * (10 - i);
  let resto = (soma * 10) % 11;
  if (resto === 10 || resto === 11) resto = 0;
  if (resto !== parseInt(c.charAt(9), 10)) return false;
  soma = 0;
  for (let i = 0; i < 10; i++) soma += parseInt(c.charAt(i), 10) * (11 - i);
  resto = (soma * 10) % 11;
  if (resto === 10 || resto === 11) resto = 0;
  return resto === parseInt(c.charAt(10), 10);
}

async function criarCobrancaMercadoPago({ sessaoId, valor, placa, titular, cpf }) {
  const token = getMercadoPagoAccessToken();
  const erroToken = validarTokenMercadoPago(token);
  if (erroToken) {
    console.error('[Mercado Pago Erro de Configuração]', erroToken);
    throw new Error(erroToken);
  }

  const expira = new Date(Date.now() + 30 * 60 * 1000).toISOString();
  const body = {
    transaction_amount: Number(Number(valor).toFixed(2)),
    description: `SmartPark - Estacionamento ${placa || ''}`.trim(),
    payment_method_id: 'pix',
    external_reference: String(sessaoId),
    date_of_expiration: expira,
    payer: {
      email: process.env.MP_PAYER_EMAIL || 'cliente@smartpark.com.br',
      first_name: (titular || 'Cliente').split(' ')[0]
    }
  };

  const cpfLimpo = (cpf || '').replace(/\D/g, '');
  if (validarCPF(cpfLimpo)) {
    body.payer.identification = {
      type: 'CPF',
      number: cpfLimpo
    };
  }

  if (process.env.PIX_WEBHOOK_URL) body.notification_url = process.env.PIX_WEBHOOK_URL;

  const res = await fetch(`${MP_API}/v1/payments`, {
    method: 'POST',
    headers: headersMP({ 'X-Idempotency-Key': crypto.randomUUID() }),
    body: JSON.stringify(body)
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) {
    const detalhe = formatarErroMercadoPago(data, res.status);
    console.error('[Mercado Pago Recusou Cobrança]:', { status: res.status, data });
    throw new Error(`Mercado Pago recusou a cobrança: ${detalhe}`);
  }
  const t = data.point_of_interaction?.transaction_data;
  if (!t || !t.qr_code) {
    console.error('[Mercado Pago Resposta sem QR Code]:', data);
    throw new Error('Mercado Pago não retornou os dados do QR Code do PIX.');
  }
  return {
    idPagamento: String(data.id),
    copiaECola: t.qr_code,
    qrCodeBase64: t.qr_code_base64,
    valor: body.transaction_amount,
    expiraEm: data.date_of_expiration
  };
}

async function consultarPagamentoMercadoPago(idPagamento) {
  const token = getMercadoPagoAccessToken();
  const erroToken = validarTokenMercadoPago(token);
  if (erroToken) {
    console.error('[Mercado Pago Erro de Configuração]', erroToken);
    throw new Error(erroToken);
  }

  const res = await fetch(`${MP_API}/v1/payments/${encodeURIComponent(idPagamento)}`, {
    headers: headersMP()
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) {
    const detalhe = formatarErroMercadoPago(data, res.status);
    console.error('[Mercado Pago Falha ao Consultar]:', { status: res.status, data });
    throw new Error(`Falha ao consultar pagamento: ${detalhe}`);
  }
  return {
    status: data.status, // approved | pending | rejected | cancelled ...
    valor: Number(data.transaction_amount),
    sessaoId: data.external_reference
  };
}

// Valida a assinatura x-signature do webhook (opcional, ativa se MP_WEBHOOK_SECRET existir)
function assinaturaWebhookValida(req) {
  const segredo = process.env.MP_WEBHOOK_SECRET;
  if (!segredo) return true;
  const assinatura = req.get('x-signature') || '';
  const requestId = req.get('x-request-id') || '';
  const ts = (assinatura.match(/ts=([^,]+)/) || [])[1];
  const v1 = (assinatura.match(/v1=([^,]+)/) || [])[1];
  const dataId = String((req.query['data.id'] || (req.body && req.body.data && req.body.data.id) || '')).toLowerCase();
  if (!ts || !v1) return false;
  const esperado = crypto.createHmac('sha256', segredo)
    .update(`id:${dataId};request-id:${requestId};ts:${ts};`).digest('hex');
  const a = Buffer.from(esperado), b = Buffer.from(v1);
  return a.length === b.length && crypto.timingSafeEqual(a, b);
}

module.exports = {
  CONFIG_PIX,
  gerarPayloadPixCopiaECola,
  gerarCobrancaPix,
  usandoMercadoPago,
  criarCobrancaMercadoPago,
  consultarPagamentoMercadoPago,
  assinaturaWebhookValida,
  validarTokenMercadoPago,
  getMercadoPagoAccessToken
};
