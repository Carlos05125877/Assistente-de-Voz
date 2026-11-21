# 🤖 J.A.R.V.I.S. — Sistema Completo para ESP32 (`ESP-WROOM-32` / `ESP32S`) + Servidor Externo Bridge

Sistema completo de assistente de voz inteligente inspirado no **J.A.R.V.I.S.**, desenvolvido sob medida para o **ESP32 WiFi + Bluetooth (`ESP-WROOM-32`)**, equipado com **2 Alto-Falantes em Estéreo Real (Canal L + Canal R)** e **1 Microfone Digital I2S**, **sem sensor de distância**.

---

## 🌟 O Que Este Sistema Faz

1. **🎵 Conexão Completa com o Spotify**:
   - Busca e toca músicas, artistas, álbuns e playlists por comando de voz (*"Jarvis, toca Coldplay no Spotify"*).
   - Controles de **Play, Pause, Próxima Música, Música Anterior, Volume (0-100%)** e consulta da faixa atual (*"Que música está tocando?"*).
   - Funciona tanto **diretamente no ESP32** (via OAuth2 Refresh Token) quanto via **Servidor Externo Jarvis Bridge** (com login no Spotify em 1 clique e streaming de áudio PCM direto para os 2 alto-falantes do ESP32).
2. **📖 Conexão Oficial com o App da Bíblia (`Holy Bible / YouVersion` — `https://www.bible.com/pt/reading-plans`)**:
   - Conecta diretamente com o ecossistema do **App da Bíblia (YouVersion)**:
     - **Planos de Leitura & Devocionais Diários** (`https://www.bible.com/pt/reading-plans`): guarda na memória Flash `NVS` do ESP32 (e no Servidor Externo) qual Plano de Leitura você está fazendo e em qual **Dia do Plano** você está (`Dia 1`, `Dia 2`, `Dia 3`...).
     - Lê por voz nos 2 alto-falantes a **Reflexão Devocional do Dia + a Passagem Bíblica (NVI/ARC/NVT)** e avança automaticamente ou por comando de voz (*"Jarvis, leia o meu devocional de hoje"*, *"Avance para o próximo dia do meu plano da Bíblia"*).
     - Inicia novos planos por tema (*ansiedade e paz, sabedoria em Provérbios, esperança e fé, gratidão e louvor*) ou por **qualquer URL/ID de plano de `https://www.bible.com/pt/reading-plans`**.
     - Busca o **Versículo do Dia Oficial** de `https://www.bible.com/pt/verse-of-the-day` e qualquer capítulo/versículo nas traduções **NVI (`129`), ARC (`211`), NVT (`193`) e ARA (`1608`)** de `bible.com/pt/bible`.
3. **⏰ Guardião de Alarmes Persistentes (`NVS`) e Timers**:
   - **Guarda até 8 Alarmes Despertadores por horário (`HH:MM`) na memória Flash NVS do ESP32** (não apaga ao desligar da tomada e sincroniza automaticamente com o Servidor Externo!).
   - Suporta alarmes diários recorrentes ou únicos, além de **5 Timers regressivos simultâneos** e **Lista de Lembretes/Notas persistente**.
   - Quando o horário do alarme chega, o Jarvis toca o **alarme estéreo espacial** nos 2 alto-falantes e anuncia o rótulo do alarme por voz neural!
4. **🖥️ Servidor Externo Opcional (`external_server/jarvis_bridge_server.py`)**:
   - Servidor leve em Python (`FastAPI`) para rodar no PC, Raspberry Pi ou VPS.
   - Alivia `~85 KB` de RAM e conexões TLS/HTTPS do ESP32, oferece login OAuth2 do Spotify com 1 clique no navegador (`http://localhost:8080`) e faz backup automático dos alarmes e histórico bíblico em `jarvis_data.json`.
5. **🏠 Casa Inteligente (`SmartLife / Tuya IoT`) + Clima (`Open-Meteo`) + Painel Web HUD (`http://jarvis.local`)**:
   - Controle de lâmpadas, fitas LED RGB, ar-condicionado e tomadas inteligentes + painel web completo com cards dedicados para **Voz/Gemini**, **Spotify**, **App Bíblia**, **Alarmes NVS**, **Teste Estéreo L/R** e **SmartLife**.

---

## 🔌 Esquema de Ligação (Pinout Seguro para `ESP-WROOM-32` — Sem Sensor de Distância)

> **ATENÇÃO**: No `ESP-WROOM-32`, os pinos `GPIO 6 a 11` são usados pela memória Flash interna. Nunca ligue periféricos nesses pinos. Todos os pinos abaixo são 100% seguros.

### 1. Microfone Digital I2S (`INMP441` ou `ICS-43434`)
| Pino do Microfone | Pino do `ESP-WROOM-32` | Observação |
| :--- | :--- | :--- |
| **VDD** | `3.3V` | **NUNCA ligue em 5V!** |
| **GND** | `GND` | Terra comum |
| **L/R** | `GND` | Define captura no canal Esquerdo |
| **SCK (BCLK)** | `GPIO 26` | Clock de bits I2S0 |
| **WS (LRCLK)** | `GPIO 25` | Word Select I2S0 |
| **SD (DOUT)** | `GPIO 33` | Entrada de dados de áudio |

### 2. Dois Alto-Falantes com 2x Amplificadores I2S `MAX98357A` (ou 1x DAC Estéreo `PCM5102A`)
| Pino dos 2x `MAX98357A` | Pino do `ESP-WROOM-32` | Configuração Estéreo (Canal L vs Canal R) |
| :--- | :--- | :--- |
| **VIN** | `5V` (VIN/VBUS) | Fonte 5V >= 2A recomendada para 2 alto-falantes de 3W/4Ω |
| **GND** | `GND` | Terra comum |
| **BCLK** | `GPIO 14` | Ligado em paralelo nos 2 módulos |
| **LRC (WS)** | `GPIO 15` | Ligado em paralelo nos 2 módulos |
| **DIN (DOUT)** | `GPIO 27` | Ligado em paralelo nos 2 módulos |
| **SD (SD_MODE)** | *Seleciona o Canal* | **Módulo Esquerdo (L)**: ligue `SD` direto no `3.3V`<br>**Módulo Direito (R)**: ligue `SD` ao `3.3V` via resistor `100kΩ–470kΩ` (ou aberto p/ mix L+R) |

### 3. Acionamento por Botão, Web ou Serial (Sem Sensor de Distância)
- **Botão `BOOT` nativo (`GPIO 0`)**: Pressione para falar com o Jarvis.
- **Botão Externo Opcional (`GPIO 4` e `GND`)**: Caso queira um botão no gabinete do projeto.
- **Painel Web HUD (`http://jarvis.local`)**: Botão para ativar o microfone remotamente ou enviar comandos e gerenciar alarmes.

---

## 🖥️ Como Usar o Servidor Externo (`external_server/jarvis_bridge_server.py`)

O ESP32 funciona 100% sozinho, mas se quiser aliviar o microcontrolador e facilitar o login do Spotify:

1. Instale as dependências e inicie o servidor no seu computador ou Raspberry Pi:
   ```bash
   cd external_server
   pip install -r requirements.txt
   python jarvis_bridge_server.py
   ```
2. Abra `http://localhost:8080` no navegador, cole seu `Spotify Client ID` e `Client Secret` e clique em **Conectar Conta Spotify**.
3. No arquivo [config_template.h](file:///c:/Users/carlo/.gemini/antigravity/scratch/jarvis_esp32/config_template.h), coloque o IP do seu computador em `EXTERNAL_SERVER_URL`:
   ```cpp
   #define EXTERNAL_SERVER_URL "http://192.168.1.100:8080"
   ```

---

## 🗣️ Exemplos de Comandos de Voz para o Jarvis

- **🎵 Spotify & Música**:
  - *"Jarvis, toca Bohemian Rhapsody no Spotify"*
  - *"Jarvis, coloca uma playlist de louvor / rock clássico no Spotify"*
  - *"Jarvis, pausa a música"* / *"Pula para a próxima faixa"* / *"Volume do Spotify em 70%"*
- **📖 App da Bíblia (`Holy Bible / YouVersion` — `bible.com/pt/reading-plans`)**:
  - *"Jarvis, leia o meu devocional e plano de leitura de hoje"*
  - *"Jarvis, avance para o próximo dia do meu plano no App da Bíblia"*
  - *"Jarvis, comece um plano de leitura sobre ansiedade e paz no App da Bíblia"*
  - *"Jarvis, inicie o plano de sabedoria em Provérbios"*
  - *"Jarvis, quais planos de leitura estão disponíveis no App da Bíblia?"*
  - *"Jarvis, qual é o versículo do dia no YouVersion?"*
  - *"Jarvis, leia o Salmo 23 na NVI"* / *"O que diz em João 3:16?"*
- **⏰ Guardar Alarmes (NVS) & Timers**:
  - *"Jarvis, guarda um alarme para as 7 e 30 da manhã para trabalhar"*
  - *"Jarvis, coloca um despertador às 22 horas para tomar remédio"*
  - *"Jarvis, quais são os meus alarmes salvos?"*
  - *"Jarvis, remova o alarme das 7 e 30"*
  - *"Jarvis, coloca um timer de 10 minutos"*
- **🏠 Casa Inteligente & Sistema**:
  - *"Jarvis, acende a luz da sala"* / *"Coloca a fita LED em azul"*
  - *"Jarvis, como está o clima agora?"*
  - *"Jarvis, relatório completo de status do sistema"*

---

## 🔐 Conta pessoal do YouVersion (planos de leitura da sua conta)

O `external_server/jarvis_bridge_server.py` conecta na **sua conta** do App Bíblia e sincroniza os planos em andamento e o dia em que você parou.

1. Rode `python external_server/jarvis_bridge_server.py` e abra `http://127.0.0.1:8080`.
2. Em **"Sua Conta Pessoal no App Bíblia"**, entre com e-mail/usuário e senha do YouVersion.
   - O servidor tenta primeiro o **OAuth2 da API do app** (`auth.youversionapi.com/token`), que não depende dos IDs de Server Action do `login.youversion.com` (eles mudam a cada deploy do site). Se falhar, tenta o **login web** (`login.youversion.com` → `bible.com/api/authenticate`), guardando **todos** os cookies do bible.com.
   - Conta criada com **Google/Apple**? Abra o painel “login social” e cole o valor do cookie `yva` (token Bearer) ou a linha inteira de cookies do `www.bible.com`.
3. Os planos aparecem em **"📚 Planos de Leitura Sincronizados"**. Clique em **Ativar no Jarvis** em qualquer um. O sync roda de novo sozinho a cada 6 h (para pegar o que você leu no celular) ou quando você clica em **🔄 Sincronizar** ou diz *"Jarvis, sincroniza meus planos"*.
4. Se a lista ficar vazia, abra **`http://127.0.0.1:8080/youversion/debug`**. Ele mostra o que cada etapa respondeu (status HTTP e estrutura do JSON, **sem tokens, sem e-mail e sem senha**).

**Leitura por voz sem truncar:** o servidor monta o texto completo do dia (devocional + todas as passagens, pela API `reading-plans`/`bible` do YouVersion) e devolve ao ESP32 só um resumo (< 2 KB). O áudio completo é tocado via `GET /api/tts?key=last`, que sintetiza o Edge-TTS **em blocos** e envia PCM 16 kHz em streaming: a voz começa depois do 1º bloco e o texto nunca passa inteiro pela RAM do ESP-WROOM-32. Aperte o botão BOOT/GPIO 4 para interromper.

Exemplos: *"Jarvis, leia meu plano da Bíblia de hoje"*, *"Jarvis, avance para o próximo dia do meu plano"*, *"Jarvis, liste meus planos"*, *"Jarvis, ative o plano 2"*.

> ⚠️ As APIs `*.youversionapi.com` usadas aqui **não são oficiais** (são as mesmas que o app usa). Podem mudar sem aviso, e o uso automatizado pode contrariar os termos do YouVersion. Use só na sua própria conta.

---

## 🖥️ Painel de Controle v2 (no PC) — `http://127.0.0.1:8080`

O `jarvis_bridge_server.py` agora serve um painel completo (`external_server/web/panel.html`; o antigo continua em `/classic`):

| Aba | O que tem |
|---|---|
| **Visão geral** | Status do ESP32 ao vivo (IP, RAM, WiFi, uptime, volume), clima de 3 dias, plano de hoje com barra de progresso, Spotify com capa, próximos alarmes e caixa de comando |
| **Comandos & voz** | Histórico das conversas (voz e painel), "fazer o Jarvis falar" qualquer texto (longo inclusive), prévia da voz no navegador, troca de voz/velocidade do Edge-TTS, ações rápidas (briefing, testes L/R, microfone, reiniciar) |
| **Bíblia** | Login/sync da conta YouVersion, planos com progresso, ativar plano, **leitor** de qualquer dia de qualquer plano (devocional + passagens) com "ouvir aqui" ou "falar no ESP32", busca de passagens, troca de tradução |
| **Spotify** | Tocando agora (capa, progresso, dispositivo), controles, volume, busca por música/artista/álbum/playlist |
| **Alarmes & notas** | Alarmes do ESP32 (adicionar/remover), timers rápidos e Pomodoro, lista de lembretes |
| **Sistema** | Eventos ao vivo, configurações (nome, cidade/coordenadas do clima, IP do ESP32, conteúdo do briefing) |

**Como o painel fala com o ESP32:** o ESP32 manda um *heartbeat* a cada 15 s (`POST /api/device/heartbeat`). Os comandos do painel vão direto para `http://<ip-do-esp32>/api/queue`; se o ESP estiver ocupado falando ou inacessível, ficam numa fila e são entregues no próximo heartbeat. No ESP32 os comandos são executados no `loop()`, então o HUD `http://jarvis.local` não trava mais enquanto o Jarvis fala.

### Novas funções de voz
- **Briefing do dia**: *"Jarvis, me dá o briefing"* → saudação, data, clima, próximos alarmes, devocional do plano (ou resumo) e versículo do dia. Configure o conteúdo na aba Sistema.
- **Alarme devocional**: alarmes cujo rótulo tem *devocional*, *bíblia*, *briefing* ou *plano* tocam e em seguida leem o briefing com o plano do dia.
- Sem o PC ligado, o briefing usa só clima + alarmes direto do ESP32.

> ⚠️ O painel não tem senha: deixe a porta 8080 acessível apenas na sua rede local.
