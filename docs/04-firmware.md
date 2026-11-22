# 4. Firmware (Arduino IDE)

O sketch é o **`firmware/jarvis_s3/jarvis_s3.ino`**. Abra esse arquivo; os `.h` da mesma pasta abrem junto, em abas.

## 4.1 Instale o suporte ao ESP32 e as bibliotecas
1. Em **Arquivo → Preferências → URLs adicionais**, cole:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. Em **Ferramentas → Placa → Gerenciador de Placas**, instale **esp32** da Espressif, versão 3.3 ou mais nova.
3. No **Gerenciador de Bibliotecas**, instale **ArduinoJson**, versão **6.21.5**.
4. Instale o decodificador de MP3 **arduino-libhelix**:
   1. Baixe o ZIP em https://github.com/pschatzmann/arduino-libhelix (*Code → Download ZIP*).
   2. Na IDE, use *Sketch → Incluir Biblioteca → Adicionar biblioteca .ZIP*.

## 4.2 Suas credenciais
Na pasta `firmware/jarvis_s3/`, copie `secrets_local.example.h` para **`secrets_local.h`** e preencha:
```cpp
#define WIFI_SSID      "MinhaRede"
#define WIFI_PASSWORD  "minha-senha"
#define NETLIFY_URL    "https://meu-jarvis.netlify.app"
#define DEVICE_TOKEN   "o-mesmo-DEVICE_TOKEN-do-Netlify"
#define OTA_PASSWORD   "uma-senha-para-atualizar-pelo-wifi"
```

## 4.3 Menu Ferramentas
Escolha a placa **ESP32S3 Dev Module** e configure:

| Opção | Valor |
|---|---|
| USB CDC On Boot | **Enabled** |
| Flash Size | **16MB (128Mb)** |
| PSRAM | **OPI PSRAM** |
| Partition Scheme | **ESP SR 16M (3MB APP/6MB SPIFFS/3.9MB MODEL)** |
| Upload Speed | 921600 |

## 4.4 A palavra “Jarvis” (fazer uma vez só)
A Arduino IDE grava, junto com o sketch, um arquivo de modelos de voz que só tem a palavra **“Hi ESP”**. Para usar “Jarvis”:
1. Feche a Arduino IDE.
2. Abra a pasta do arquivo original:
   - **Windows:** `%LOCALAPPDATA%\Arduino15\packages\esp32\tools\esp32-arduino-libs\<versão>\esp32s3\esp_sr\`
   - **Linux:** `~/.arduino15/packages/esp32/tools/esp32-arduino-libs/<versão>/esp32s3/esp_sr/`
   - **Mac:** `~/Library/Arduino15/packages/esp32/tools/esp32-arduino-libs/<versão>/esp32s3/esp_sr/`
3. Renomeie o `srmodels.bin` que está lá para `srmodels.bin.original`.
4. Copie o **`firmware/srmodels_jarvis.bin`** deste repositório para essa pasta com o nome **`srmodels.bin`**.

> O arquivo traz os modelos **“Jarvis”** e **“Hi ESP”**. Se o modelo “Jarvis” não carregar na sua versão da placa, o firmware passa sozinho para “Hi ESP” e avisa no monitor serial.
> Se você atualizar o pacote esp32 da Arduino IDE, repita esse passo.

## 4.5 Grave e teste
1. Ligue o ESP32-S3 pela porta USB marcada como **USB** (ou **COM**) e escolha a porta em **Ferramentas → Porta**.
2. Clique em **Carregar**. Se a gravação não começar, segure o **BOOT**, aperte o **RESET** e solte o BOOT.
3. Abra o **Monitor Serial** em **115200**. Deve aparecer:
   ```
   🎙️ Palavra de ativação: wn9_jarvis_tts (normal)
   📶 conectado  IP 192.168.x.x
   ✅ J.A.R.V.I.S. 5.0-s3 | https://meu-jarvis.netlify.app | ...
   ```
4. Diga **“Jarvis”**: o LED fica ciano e toca um bipe curto. Agora faça o pedido.
   Também dá para testar digitando uma pergunta no Monitor Serial e apertando ENTER.

## 4.6 Atualizar pelo WiFi
Depois da primeira gravação por cabo, a placa **jarvis** aparece em **Ferramentas → Porta** como porta de rede. A senha é a `OTA_PASSWORD`.

## Ajustes em `config.h`
| Ajuste | Para quê |
|---|---|
| Pinos `MIC_*`, `SPK_*`, `PIN_*` | Se você ligou em outros GPIOs |
| `MIC_SHIFT_BITS` | Ganho do microfone. Diminua se o Jarvis não te entende; aumente se o áudio sair distorcido |
| `END_SILENCE_MS` | Quanto silêncio encerra a fala (padrão: 0,9 s) |
| `PIN_RGB_LED` | 48 ou 38, conforme a versão da placa |

> Build avançado pelo **ESP-IDF**: veja `firmware/idf/` (mesmo código).

Próximo: [5. Como usar →](05-como-usar.md)
