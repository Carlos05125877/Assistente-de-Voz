# 🤖 J.A.R.V.I.S. — Assistente de voz: ESP32-S3 + Netlify (100% grátis)

Diga **“Jarvis”** e fale, sem apertar nada. O ESP32-S3 fica ligado 24 h com a palavra de ativação, o relógio e o áudio.
Tudo o que é pesado roda no **Netlify**, que só liga quando é chamado (plano free, sem servidor 24 h).

```
          ┌──────────────── ESP32-S3 N16R8 (sempre ligado, offline) ────────────────┐
 "Jarvis" ─► WakeNet (palavra) ─► AFE: eco + ruído + fim da fala ─► Groq Whisper ─┐ │
          │ alarmes · lembretes · timers · horários das rotinas (NVS)              │ │
          │ player MP3: buffer de 256 KB na PSRAM, toca enquanto baixa             │ │
          └─────────────────────────────────▲──────────────────────────────────────┘ │
                     MP3 em partes + ações  │        POST /api/turn (texto + estado)  │
          ┌─────────────── Netlify Functions (sob demanda) ──────────────────────────▼─┐
          │ Gemini + ferramentas · Edge TTS (voz neural) · YouVersion · Spotify          │
          │ SmartLife/Tuya · clima · notícias · cotações · Wikipédia · painel · Blobs    │
          └──────────────────────────────────────────────────────────────────────────────┘
```

| Fica no ESP32-S3 | Vai para o Netlify |
|---|---|
| Palavra **“Jarvis”** (ESP-SR WakeNet9, sem internet) | Agente Gemini com function calling |
| Cancelamento de eco: “Jarvis” interrompe o Jarvis enquanto ele fala | Voz do Jarvis: Edge TTS em MP3 24 kHz |
| Supressão de ruído e detecção de fim de fala (AFE) | YouVersion (login, planos, devocional, passagens) |
| Voz → texto direto no Groq (upload enquanto você fala) | Spotify, casa inteligente, clima, notícias, cotações, Wikipédia |
| Alarmes, lembretes, timers e rotinas com horário (tocam offline) | Painel de controle e dados (Netlify Blobs) |
| Player MP3 estéreo nos 2 alto-falantes | Sync diário do YouVersion (função agendada) |

**Como uma conversa acontece**
1. Você diz “Jarvis, …”.
2. O WakeNet reconhece a palavra no próprio chip e a conexão com o Groq já abre.
3. Sua fala, limpa de eco e ruído, sobe enquanto você fala. Quando você para, o texto chega em menos de 1 s.
4. O Netlify responde com a 1ª frase em streaming, que começa a tocar em ~1 s.
5. O resto vem em partes pela mesma conexão. Com 256 KB de buffer na PSRAM, o devocional inteiro sai sem pausas.

**Durante e depois da resposta**
- Se o Jarvis terminar com uma pergunta, ele continua ouvindo sem precisar dizer “Jarvis”.
- Dizer “Jarvis” no meio da fala interrompe e já escuta o novo pedido.

## 1. Hardware (ESP32-S3-DevKitC-1 **N16R8**: 16 MB flash, 8 MB PSRAM)
| Função | Pinos |
|---|---|
| Microfone INMP441 | SCK 4 · WS 5 · SD 6 · L/R no GND · VDD 3V3 |
| 2× MAX98357A estéreo | BCLK 15 · LRC 16 · DIN 7 (os dois no mesmo barramento; mantenha a ligação do pino SD que define L e R) |
| Botões | BOOT (GPIO 0) e externo (GPIO 21 → GND) |
| LED de status | RGB da placa (GPIO 48; na DevKitC-1 v1.1 é o 38: troque `PIN_RGB_LED` em `config.h`) |

Não use os GPIO 19/20 (USB), 26–37 (flash e PSRAM octal) nem 43/44 (serial).

Cores do LED:
- azul fraco: esperando
- ciano: ouvindo
- roxo: pensando
- verde: falando
- âmbar: mudo
- vermelho: erro

**Botão:** um toque fala ou interrompe; segurar 2 s deixa o microfone mudo (privacidade). Também dá para dizer “Jarvis, desliga o microfone”; para religar, use o botão ou o painel.

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

## 3. Firmware do ESP32-S3 (ESP-IDF)
O firmware usa o **ESP-IDF 5.x** com o Arduino como componente. O ESP-IDF é necessário para gravar o modelo da palavra “Jarvis” na partição `model`. Por isso a Arduino IDE não serve mais. As dependências são baixadas sozinhas no primeiro build:
- arduino-esp32
- esp-sr
- ArduinoJson
- arduino-libhelix

1. Instale o ESP-IDF 5.4 ou mais novo. O jeito mais fácil é a extensão **ESP-IDF** do VS Code.
2. Configure as credenciais:
   ```bash
   cd firmware
   cp main/secrets_local.example.h main/secrets_local.h   # WiFi, NETLIFY_URL, DEVICE_TOKEN, OTA_PASSWORD
   ```
3. Compile e grave pelo USB. O `flash` grava o app **e** o modelo do WakeNet:
   ```bash
   idf.py set-target esp32s3
   idf.py build
   idf.py -p COM5 flash monitor        # no Linux/Mac: -p /dev/ttyACM0
   ```
4. No monitor deve aparecer `Palavra de ativação: wn9_jarvis_tts`. Se aparecer outra palavra, abra `idf.py menuconfig`, vá em *ESP Speech Recognition → Load Multiple Wake Words*, marque **Jarvis**, desmarque as outras e grave de novo.
5. Para as próximas atualizações, use o WiFi (o modelo não muda):
   ```bash
   python tools/espota.py -i jarvis.local -a SUA_SENHA_OTA -f build/jarvis.bin
   ```

Ajustes úteis em `firmware/main/config.h`:
- `MIC_SHIFT_BITS`: ganho do microfone. Diminua se o Whisper não te ouvir; aumente se o áudio sair distorcido.
- `END_SILENCE_MS`: quanto silêncio encerra a fala.

**Memória:** o modelo do WakeNet e o AFE, o buffer de MP3 (256 KB), a gravação da fala (14 s) e os buffers TLS ficam na PSRAM de 8 MB. A RAM interna sobra para o WiFi e o áudio em tempo real. O painel mostra a RAM e a PSRAM livres.

**Sync:** a cada 3 min o ESP32 manda seu estado e recebe os comandos e configurações do painel. Com o painel aberto, a cada 10 s. Qualquer mudança local sincroniza em ~3 s.

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

Todas as frases começam com “Jarvis, …”. Se ele terminar com uma pergunta, responda direto. **Sem internet:** a palavra “Jarvis” continua sendo reconhecida (o pedido não, ele avisa com um som), e alarmes, lembretes e timers tocam do mesmo jeito. Um alarme para com o botão ou dizendo “Jarvis”.

## 5. Painel de controle
O painel tem estas abas:
- **Visão geral**
- **Comandos & voz**: fazer o Jarvis falar, prévia de voz no navegador, histórico
- **Bíblia**: login, planos, leitor
- **Spotify**
- **Assistente**: lembretes, listas, rotinas, datas, Wikipédia
- **Alarmes & notas**
- **Sistema**: eventos; configurações, incluindo a palavra “Jarvis”, a sensibilidade, a interrupção por voz e a continuação de conversa; chaves de API; casa inteligente

O painel só consulta o Netlify a cada 10 s com a aba visível. Diagnóstico do YouVersion em `/youversion/debug`. Uma função agendada sincroniza os planos do YouVersion todo dia às 05:15.

## 6. Estrutura
```
firmware/                    projeto ESP-IDF (ESP32-S3)
  main/main.cpp              laço principal: conversa, botão, eventos, sync
  main/listener.h            INMP441 + AFE (eco, ruído, VAD) + WakeNet "Jarvis" + gravação na PSRAM
  main/speaker.h             2x MAX98357A, sons, player MP3 (libhelix) e referência do eco
  main/net_link.h            HTTP keep-alive para o Netlify + Groq Whisper em streaming
  main/agenda.h              alarmes, lembretes, timers e rotinas na NVS
  sdkconfig.defaults         PSRAM octal, TLS na PSRAM, WakeNet9 "Jarvis"
  partitions.csv             2x app (OTA) + partição "model" do ESP-SR
netlify/functions            api.mjs (painel + sync), voice.mjs (turno/TTS), youversion-daily.mjs
netlify/lib                  agente, TTS, YouVersion, Spotify, Tuya, informações, armazenamento
public/index.html            painel
tests/                       testes offline das funções (npm test)
```

> ⚠️ As APIs `*.youversionapi.com` usadas para a conta do YouVersion **não são oficiais**: podem mudar sem aviso e o uso automatizado pode contrariar os termos do serviço. Use só na sua própria conta. O Edge TTS também é um serviço não oficial; se falhar, o Jarvis usa uma voz reserva.
