# Guia de Integração do PIX (Mercado Pago) — SmartPark RFID

A cancela e o ticket **só são liberados quando o Mercado Pago confirma que o PIX caiu**.

## Como funciona

```
Cliente/guarita            Servidor (server.js)              Mercado Pago
     │  pede PIX  ───────────►  POST /api/pagamento/pix/gerar ──► cria cobrança (/v1/payments)
     │  ◄── QR + Copia e Cola ◄─┘ (guarda em database.json > cobrancasPix, status PENDENTE)
     │
     │  cliente paga no app do banco ─────────────────────────────►
     │                           POST /api/pix/webhook  ◄──────────  "payment id=123 mudou"
     │                           └─ reconsulta GET /v1/payments/123 (status = approved? valor ok?)
     │                              └─ SIM → liquidarCobranca(): sessão = PAGO, gera comprovante,
     │  ◄── SSE pagamento_concluido (abrirCancela)                    dispara evento SSE
     │  navegador da guarita envia "OPEN" pela serial ao ESP32
```

Garantias de segurança:
- **O corpo do webhook nunca é confiado.** Ele só diz *qual* pagamento consultar; o status vem da API do Mercado Pago.
- Valor pago precisa ser >= valor cobrado e o `external_reference` precisa bater com a sessão.
- `POST /api/pagamento/finalizar` **não marca nada como pago**: só reconsulta o provedor. Sem PIX aprovado retorna `402`.
- O comando `OPEN` de saída só é enviado ao ESP32 quando chega o evento `pagamento_concluido`.
- Se o webhook não chegar (ex.: localhost sem túnel), o site faz *polling* a cada 4 s e o servidor consulta o Mercado Pago direto — funciona mesmo sem webhook.

Arquivos: `pixService.js` (chamadas ao Mercado Pago), `server.js` (seção "PIX: COBRANÇA, WEBHOOK E LIBERAÇÃO").

## Passo a passo

### 1. Criar a conta e a aplicação
1. Tenha uma conta Mercado Pago com **chave PIX cadastrada**.
2. Acesse https://www.mercadopago.com.br/developers → **Suas integrações → Criar aplicação**.
3. Marque produto **"Checkout Transparente / API de Pagamentos"** (PIX).

### 2. Pegar as credenciais
- Em **Credenciais de teste** (para desenvolver) ou **Credenciais de produção** (dinheiro real), copie o **Access Token**.
- Atenção: o PIX em ambiente de teste do Mercado Pago não é pago por um banco real; para ver a cobrança "cair" de verdade use credenciais de **produção** com um valor baixo (ex.: R$ 0,01 mudando a tabela de preços) e pague com seu próprio app de banco.

### 3. Configurar as variáveis de ambiente
No PowerShell, antes de iniciar o servidor (ou no `iniciar_sistema.bat`):

```powershell
$env:PIX_PROVIDER      = "mercadopago"
$env:MP_ACCESS_TOKEN   = "APP_USR-xxxxxxxx"
$env:PIX_WEBHOOK_URL   = "https://SEU-TUNEL.ngrok-free.app/api/pix/webhook"
$env:MP_WEBHOOK_SECRET = "assinatura-secreta-do-webhook"   # passo 5
npm start
```

**Nunca** coloque o Access Token direto no código nem faça commit dele.

### 4. Expor o servidor para o webhook (só se rodar no seu PC)
O Mercado Pago precisa alcançar seu servidor pela internet:

```powershell
npx ngrok http 3000
```

Copie a URL `https://....ngrok-free.app` e use em `PIX_WEBHOOK_URL`. Sem isso o sistema ainda funciona pelo polling do passo "Como funciona", só com alguns segundos a mais.

### 5. Cadastrar o webhook (recomendado)
1. No painel da aplicação: **Webhooks → Configurar notificações**.
2. URL: `https://SEU-TUNEL.ngrok-free.app/api/pix/webhook`.
3. Evento: **Pagamentos**.
4. Copie a **assinatura secreta** gerada e coloque em `MP_WEBHOOK_SECRET` (o servidor passa a validar o header `x-signature`).

### 6. Testar
1. `npm start`, abra `http://localhost:3000`, faça login como admin.
2. Aproxime uma tag (ou registre entrada), abra a aba de pagamento e selecione o veículo: o QR Code e o Copia e Cola aparecem.
3. Pague pelo app do banco. Em poucos segundos o site mostra o ticket e o ESP32 abre a cancela.
4. Antes de pagar, clique em **Verificar Pagamento PIX**: deve avisar "PIX ainda não identificado" e a cancela permanece fechada.

### Modo de desenvolvimento sem gateway
Sem `PIX_PROVIDER=mercadopago`, o sistema gera apenas um PIX estático (a chave de `pixService.js`), que **não tem como ser detectado automaticamente**. Para demonstrar o fluxo sem Mercado Pago:

```powershell
$env:PIX_SIMULAR = "1"
npm start
# após gerar o QR, simule o pagamento:
Invoke-RestMethod -Method Post -Uri http://localhost:3000/api/pix/simular -ContentType application/json -Body '{"sessaoId":"SES-XXXX"}'
```

A rota `/api/pix/simular` fica **desativada** em produção (exige `PIX_SIMULAR=1` e é bloqueada com `mercadopago`).

## Observações e limites conhecidos
- A cobrança vale 30 min. Se o valor mudar com o tempo de permanência, uma nova cobrança é gerada; pagar a anterior ainda é aceito pelo valor daquela cobrança.
- Desconto/acréscimo manual da guarita foi removido do fluxo: o valor pago é o valor da cobrança.
- Pagamento duplicado ou tardio fica registrado como `PAGO_SEM_SESSAO_ATIVA` em `cobrancasPix` para estorno/análise manual.
- `database.json` e as rotas administrativas ainda não têm autenticação de API (login é só no front-end). Para uso real, proteja as rotas antes de publicar o servidor na internet.
