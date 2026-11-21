# 🤖 J.A.R.V.I.S. — Assistente de voz com ESP32 + servidor na nuvem

O **ESP-WROOM-32** é só "boca e ouvido": microfone INMP441, 2 alto-falantes MAX98357A e um botão.
**Toda a inteligência roda no servidor** (`external_server/jarvis_bridge_server.py`), que pode ficar
**de graça** numa VM Oracle Cloud Always Free, num PC/Raspberry Pi em casa ou, pago, no Fly.io.

```
 ESP32 ──POST /api/voice (áudio PCM enquanto você fala)──▶ Servidor
                                                            ├─ Groq Whisper (voz → texto)
                                                            ├─ Gemini + ferramentas em Python
                                                            │    Spotify · YouVersion · SmartLife/Tuya · alarmes · timers
                                                            │    lembretes · listas · rotinas · notícias · cotações · Wikipédia
                                                            └─ Edge-TTS (pt-BR-AntonioNeural) em blocos
 ESP32 ◀──────────── resposta PCM 16 kHz tocada enquanto chega ───┘
 ESP32 ──GET /api/device/poll (long-poll)──▶ comandos do painel na hora (funciona atrás do roteador)
```

### Por que o ESP32 virou "thin client"
- **Uma conexão por pergunta.** Antes, o ESP abria várias conexões HTTPS a cada pergunta (Groq, Gemini, às vezes Tuya/Spotify). Cada handshake TLS no ESP32 custa RAM e tempo. Agora o ESP abre uma única conexão com o servidor, e é o servidor que chama as APIs.
- **Firmware muito menor.** Saíram Gemini, ferramentas, Spotify, Tuya, Bíblia e os JSON grandes. Sobram 3 arquivos pequenos (`audio_io.h`, `server_link.h`, `jarvis_esp32.ino`), e o firmware cabe numa partição com **OTA** (atualização pelo WiFi).
- **Chaves e configuração só no servidor.** Chaves de API, alarmes, dispositivos e rotinas são editados no painel, sem regravar o ESP.
- **Sem servidor:** o botão toca um som de erro, e os **alarmes ficam espelhados na NVS** do ESP e tocam mesmo assim.

---

## 1. Hardware (ESP-WROOM-32)
| Função | Pinos |
|---|---|
| Microfone INMP441 (I2S0) | BCLK 26 · WS 25 · SD 33 (L/R no GND) |
| 2× MAX98357A estéreo (I2S1) | BCLK 14 · LRC 15 · DIN 27 |
| Botões | BOOT (GPIO 0) e externo (GPIO 4 → GND) |
| LED de status | GPIO 2 |

GPIO 6–11 são da flash SPI e não devem ser usados.

## 2. Onde rodar o servidor (opções gratuitas)
O Jarvis precisa de um processo **sempre ligado** (agendador de alarmes e lembretes, long-poll do ESP32 e conversa em streaming). Por isso Netlify/Vercel (funções com limite de tempo), Render/Koyeb free (dormem sem tráfego e apagam os arquivos) e Hugging Face (Docker agora é pago) **não servem**.

### Opção A — Oracle Cloud *Always Free* (nuvem, grátis 24 h)
1. Crie a conta em oracle.com/cloud/free. Pedem cartão de **crédito** só para verificação; o Always Free não é cobrado. Escolha **Brazil East (São Paulo)** ou **Brazil Southeast (Vinhedo)** como *home region*, porque não dá para trocar depois.
2. Crie uma instância **Ubuntu**. Prefira a forma *VM.Standard.A1.Flex* (ARM); se aparecer "out of capacity", use a *VM.Standard.E2.1.Micro*.
3. Entre por SSH e rode:
   ```bash
   git clone https://github.com/Carlos05125877/Assistente-de-Voz.git && cd Assistente-de-Voz
   sudo bash deploy/oracle-setup.sh --funnel
   ```
   O script instala tudo, cria o serviço `jarvis` (reinicia sozinho) e mostra o `DEVICE_TOKEN`. Com `--funnel` ele também publica o painel em **HTTPS grátis sem domínio e sem abrir portas** (Tailscale Funnel: `https://<máquina>.<rede>.ts.net`).
4. Coloque essa URL em `PUBLIC_URL` (`/etc/jarvis.env`) e em `JARVIS_SERVER_URL` no ESP32, depois rode `sudo systemctl restart jarvis`.

### Opção B — Em casa (PC, notebook velho ou Raspberry Pi), sem cartão
- **Windows:** rode `deploy\iniciar_jarvis_windows.bat`. Para iniciar com o Windows, ponha um atalho dele em `shell:startup`.
- **Linux / Raspberry Pi:** `sudo bash deploy/oracle-setup.sh`, o mesmo script da opção A.
- O ESP32 usa `http://IP-DO-PC:8080`, que é a menor latência possível.
- **Painel fora de casa:** instale o **Tailscale** (grátis para uso pessoal) no PC e no celular. Sem ele, o painel só abre na rede local.

### Opção paga — Fly.io (região São Paulo)
`Dockerfile` e `fly.toml` já estão prontos:
```bash
fly launch --no-deploy --copy-config --name jarvis-SEUNOME --region gru
fly volumes create jarvis_data --region gru --size 1
fly secrets set PANEL_PASSWORD='uma-senha-forte' DEVICE_TOKEN="$(openssl rand -hex 16)" PUBLIC_URL='https://jarvis-SEUNOME.fly.dev'
fly deploy
```

### Depois de subir o servidor
Abra o painel, entre com a senha e preencha, na aba **Sistema**:
- **Chaves de API:** Groq (voz → texto) e Gemini (IA). As chaves da Tuya são opcionais.
- **Casa inteligente:** cadastre os dispositivos SmartLife.
- **Cidade/coordenadas:** usadas pelo clima.

Na aba **Spotify**, cadastre a Redirect URI `<PUBLIC_URL>/spotify/callback` e conecte. Na aba **Bíblia**, entre na sua conta YouVersion.

**Rodar no PC (rede local):**
```bash
pip install -r external_server/requirements.txt
python external_server/jarvis_bridge_server.py   # http://127.0.0.1:8080
```
Sem `PANEL_PASSWORD`/`DEVICE_TOKEN`, o servidor fica aberto. Use assim só na rede local.

| Variável | Para que serve |
|---|---|
| `PANEL_PASSWORD` | Senha do painel (obrigatória na internet) |
| `DEVICE_TOKEN` | Token que o ESP32 envia no cabeçalho `X-Device-Token` |
| `PUBLIC_URL` | URL pública (callback do Spotify) |
| `DATA_DIR` | Pasta persistente dos dados (`/data` no Fly.io) |
| `GROQ_API_KEY`, `GEMINI_API_KEY`, `TUYA_ACCESS_ID`, ... | Alternativa a cadastrar as chaves no painel |

## 3. Firmware do ESP32
Crie `secrets_local.h` (está no `.gitignore`):
```cpp
#define WIFI_SSID           "MinhaRede"
#define WIFI_PASSWORD       "minha-senha"
#define JARVIS_SERVER_URL   "https://jarvis.SUA-REDE.ts.net"   // ou "http://192.168.1.120:8080" em casa
#define JARVIS_DEVICE_TOKEN "o-mesmo-DEVICE_TOKEN-do-servidor"
#define OTA_PASSWORD        "troque-esta-senha"
```
Grave a primeira vez pelo cabo (PlatformIO ou Arduino IDE, partição *Minimal SPIFFS with OTA*). Depois dá para atualizar pelo WiFi (`upload_protocol = espota`, `upload_port = jarvis.local`, veja `platformio.ini`).

**Uso:**
- **Para falar:** aperte o botão, espere o *chime* e fale. A gravação para sozinha quando você para de falar.
- **Para interromper:** aperte o botão enquanto o Jarvis fala.
- **Pelo Monitor Serial:** ENTER liga o microfone; texto + ENTER envia uma pergunta digitada.

## 4. O que ele faz (exemplos de voz)
| Área | Exemplos |
|---|---|
| Bíblia (sua conta YouVersion) | "leia meu plano de hoje", "avance para o próximo dia", "liste meus planos", "leia Salmos 23" |
| Briefing | "me dá o briefing", com clima, alarmes, lembretes, aniversários, devocional e versículo (configurável) |
| Spotify | "toca Coldplay", "pausa", "próxima", "volume do Spotify em 40" |
| Casa | "liga a luz da sala", "fita LED em azul" |
| Alarmes/timers | "alarme às 6 e meia", "timer de 10 minutos" (alarmes com *devocional* no nome leem o briefing) |
| Lembretes | "me lembra amanhã às 9 de pagar a conta", "daqui a 20 minutos tirar o bolo" |
| Listas | "adiciona leite e café na lista de compras", "o que tem na lista de tarefas?" |
| Rotinas | "ativa a rotina boa noite", "modo foco" (crie as suas na aba Assistente) |
| Informação | "notícias de hoje", "quanto está o dólar?", "quem foi Santos Dumont?", "quantos dias faltam pro Natal?" |

Se o Jarvis terminar a resposta com uma pergunta, ele já volta a ouvir.

## 5. Painel de controle
Abas: **Visão geral**, **Comandos & voz** (fazer o Jarvis falar, prévia de voz, histórico), **Bíblia** (login, planos, leitor), **Spotify**, **Assistente** (lembretes, listas, rotinas, datas, Wikipédia), **Alarmes & notas** e **Sistema** (eventos ao vivo, configurações, chaves de API, casa inteligente). Diagnóstico do YouVersion em `/youversion/debug`.

> ⚠️ As APIs `*.youversionapi.com` usadas para a conta do YouVersion **não são oficiais**: podem mudar sem aviso e o uso automatizado pode contrariar os termos do serviço. Use só na sua própria conta.
