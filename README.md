# 🤖 J.A.R.V.I.S. — Assistente de voz híbrido: ESP32 + Netlify (100% grátis)

O ESP32 fica ligado 24 h fazendo só o que precisa ser instantâneo e funcionar sem internet.
Tudo o que é pesado roda no **Netlify**, que só liga quando é chamado (plano free, sem servidor 24 h).

```
              ┌──────────── ESP32 (sempre ligado, grátis) ────────────┐
 botão / voz ─►  microfone ──► Groq Whisper (texto, direto do ESP)    │
              │  alarmes · lembretes · timers · horários das rotinas  │ ◄─ NVS: tocam mesmo offline
              │  player MP3 (buffer de 40 KB, toca enquanto baixa)    │
              └───────────────┬───────────────────────▲──────────────┘
                 POST /api/turn (texto + estado)      │ MP3 em partes + ações (alarme, lembrete, volume…)
              ┌───────────────▼──── Netlify Functions (sob demanda) ──┴──────────┐
              │ Gemini + ferramentas · Edge TTS (voz neural) · YouVersion         │
              │ Spotify · SmartLife/Tuya · clima · notícias · cotações · Wikipédia│
              │ painel web (public/) · Netlify Blobs (dados) · sync diário Bíblia │
              └───────────────────────────────────────────────────────────────────┘
```

| Fica no ESP32 | Vai para o Netlify |
|---|---|
| Botão, LED, microfone e VAD | Agente Gemini com function calling |
| Voz → texto direto no Groq (upload enquanto você fala) | Voz do Jarvis: Edge TTS em MP3 24 kHz |
| Alarmes, lembretes, timers (NVS, disparam na hora exata) | YouVersion (login, planos, devocional, passagens) |
| Horários das rotinas | Spotify, casa inteligente, clima, notícias, cotações, Wikipédia |
| Decodificar e tocar MP3 nos 2 alto-falantes | Painel de controle e armazenamento (Blobs) |

**Por que assim:** o ESP-WROOM-32 (sem PSRAM, ~200 KB de RAM e uma sessão TLS por vez) não aguenta o Gemini com ferramentas, o TTS neural e as APIs de Bíblia/Spotify ao mesmo tempo. Já o Netlify não serve para coisas "sempre ligadas" (alarme às 6h, timer): uma função só roda quando alguém a chama. Então o relógio fica no ESP32 e a inteligência no Netlify.

**Fluidez:**
- A 1ª frase da resposta chega em *streaming* e começa a tocar em ~1 s.
- O resto do texto vem em partes de até 650 caracteres pela mesma conexão TLS (keep-alive).
- Enquanto o buffer de 40 KB (~6,5 s de áudio) toca, o ESP32 já baixa a parte seguinte, então o devocional inteiro sai sem pausas e sem estourar a RAM.
- O botão interrompe a fala.

## 1. Hardware (ESP-WROOM-32)
| Função | Pinos |
|---|---|
| Microfone INMP441 (I2S0) | BCLK 26 · WS 25 · SD 33 (L/R no GND) |
| 2× MAX98357A estéreo (I2S1) | BCLK 14 · LRC 15 · DIN 27 |
| Botões | BOOT (GPIO 0) e externo (GPIO 4 → GND) |
| LED de status | GPIO 2 |

GPIO 6–11 são da flash SPI e não devem ser usados.

## 2. Deploy no Netlify (plano free)
1. No Netlify: **Add new site → Import from Git** e escolha este repositório. Não há etapa de build: o painel é a pasta `public/` e as funções estão em `netlify/functions/`.
2. Em **Site configuration → Environment variables**, crie:

   | Variável | Para quê |
   |---|---|
   | `PANEL_PASSWORD` | Senha do painel (cookie HttpOnly assinado) |
   | `DEVICE_TOKEN` | Token longo e aleatório; o mesmo vai no `secrets_local.h` do ESP32 |
   | `GEMINI_API_KEY` | *(opcional)* Pode ser colocada depois pelo painel |
   | `GROQ_API_KEY` | *(opcional)* Pode ser colocada pelo painel; ela é enviada ao ESP32 no sync |

3. Faça o deploy e abra `https://SEU-SITE.netlify.app`, entre com a senha e, na aba **Sistema → Chaves de API**, cole as chaves do Gemini e do Groq (as duas têm plano gratuito).
4. Na aba **Bíblia**, entre com sua conta do YouVersion. Na aba **Spotify**, conecte a conta, usando a Redirect URI mostrada ali.

> **Créditos do plano free.** O free tem 300 créditos por mês e não cobra excedente: se acabarem, o site pausa até o próximo ciclo. O que gasta crédito é o tempo de execução das funções, as requisições, a banda e **cada deploy de produção (15 créditos)**.
>
> Nossa *estimativa* para uso pessoal fica em uns 60 a 120 créditos por mês (sync de 3 em 3 min, painel aberto 1 h por dia, ~30 conversas por dia e o devocional diário), contando 2 deploys. **Evite deploys frequentes**: faça vários commits e publique de uma vez (ou desligue o auto-deploy e publique manualmente). O consumo aparece em *Usage & billing*.

## 3. Firmware do ESP32
Crie `secrets_local.h` ao lado do `.ino` (ele está no `.gitignore`):
```cpp
#define WIFI_SSID      "MinhaRede"
#define WIFI_PASSWORD  "minha-senha"
#define NETLIFY_URL    "https://SEU-SITE.netlify.app"
#define DEVICE_TOKEN   "o-mesmo-DEVICE_TOKEN-do-Netlify"
#define OTA_PASSWORD   "troque-esta-senha"
// #define GROQ_API_KEY "gsk_..."   // opcional (o painel também envia)
```
Compile com PlatformIO (`pio run -t upload`) ou Arduino IDE. Bibliotecas usadas:
- **ArduinoJson 6**
- **arduino-libhelix** (`https://github.com/pschatzmann/arduino-libhelix`), na Arduino IDE instale pelo ZIP do GitHub

Depois do primeiro upload por cabo dá para atualizar pelo WiFi (OTA, host `jarvis.local`).

**Uso da RAM:** o buffer MP3 de 40 KB e o decoder (~25 KB) são reservados no boot. Só existe uma sessão TLS por vez (~40 KB): Groq *ou* Netlify. O status mostra a RAM livre no painel.

**Sync:** a cada 3 min, o ESP32 manda seu estado (alarmes, lembretes, timers, RAM, WiFi) e recebe os comandos do painel. Com o painel aberto, o intervalo cai para 10 s. Qualquer mudança local (um alarme criado por voz, por exemplo) sincroniza em ~3 s.

## 4. O que ele faz (exemplos de voz)
| Área | Exemplos |
|---|---|
| Bíblia (sua conta YouVersion) | "leia meu plano de hoje", "avance para o próximo dia", "liste meus planos", "leia Salmos 23" |
| Briefing | "me dá o briefing", com clima, alarmes, lembretes, aniversários, devocional e versículo (configurável) |
| Spotify | "toca Coldplay", "pausa", "próxima", "volume do Spotify em 40" |
| Casa | "liga a luz da sala", "fita LED em azul" |
| Alarmes/timers | "alarme às 6 e meia", "timer de 10 minutos" (alarmes com *devocional* ou *briefing* no nome leem o briefing ao desligar) |
| Lembretes | "me lembra amanhã às 9 de pagar a conta", "daqui a 20 minutos tirar o bolo" |
| Listas | "adiciona leite e café na lista de compras", "o que tem na lista de tarefas?" |
| Rotinas | "ativa a rotina boa noite", "modo foco" (crie as suas na aba Assistente; com horário, o ESP32 dispara sozinho) |
| Informação | "notícias de hoje", "quanto está o dólar?", "quem foi Santos Dumont?", "quantos dias faltam pro Natal?" |

Se o Jarvis terminar a resposta com uma pergunta, ele já volta a ouvir. **Sem internet:** alarmes, lembretes e timers tocam do mesmo jeito (com som em vez de voz).

## 5. Painel de controle
O painel tem estas abas:
- **Visão geral**
- **Comandos & voz**: fazer o Jarvis falar, prévia de voz no navegador, histórico
- **Bíblia**: login, planos, leitor
- **Spotify**
- **Assistente**: lembretes, listas, rotinas, datas, Wikipédia
- **Alarmes & notas**
- **Sistema**: eventos, configurações, chaves de API, casa inteligente

O painel só consulta o Netlify a cada 10 s com a aba visível. Diagnóstico do YouVersion em `/youversion/debug`. Uma função agendada sincroniza os planos do YouVersion todo dia às 05:15.

## 6. Estrutura
```
jarvis_esp32.ino   laço principal: botão, conversa, eventos, sync
audio_io.h         I2S (mic + 2 alto-falantes), sons, player MP3 com buffer em anel (libhelix)
net_link.h         HTTP keep-alive para o Netlify (chunked) + Groq Whisper em streaming
agenda.h           alarmes, lembretes, timers e rotinas na NVS
netlify/functions  api.mjs (painel + sync), voice.mjs (turno/TTS), youversion-daily.mjs (agendada)
netlify/lib        agente, TTS, YouVersion, Spotify, Tuya, informações, armazenamento (Blobs)
public/index.html  painel
tests/             testes offline das funções (npm test)
```

> ⚠️ As APIs `*.youversionapi.com` usadas para a conta do YouVersion **não são oficiais**: podem mudar sem aviso e o uso automatizado pode contrariar os termos do serviço. Use só na sua própria conta. O Edge TTS também é um serviço não oficial; se falhar, o Jarvis usa uma voz reserva.
