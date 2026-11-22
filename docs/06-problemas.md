# 6. Problemas comuns

| Sintoma | O que fazer |
|---|---|
| Erro de compilação `MP3DecoderHelix.h: No such file` | Instale a biblioteca **arduino-libhelix** pelo ZIP ([guia 4](04-firmware.md)) |
| Erros com `DynamicJsonDocument` | Use a **ArduinoJson 6.21.5** |
| Monitor serial: `Modelos do ESP-SR ausentes` | Escolha o Partition Scheme **ESP SR 16M** e grave de novo |
| A palavra configurada é `wn9_hiesp` e não “Jarvis” | Faça o passo **4.4** do [guia do firmware](04-firmware.md). Até lá, ele atende por “Hi ESP” |
| Não reage ao “Jarvis” | Confira o microfone (L/R no GND, SCK 4, WS 5, SD 6). No painel, em *Sistema*, marque **Palavra mais sensível** |
| Dispara sozinho | Desmarque **Palavra mais sensível** no painel |
| Ouve, mas não entende (som de erro) | Confira a chave do **Groq** no painel. Ajuste `MIC_SHIFT_BITS` em `config.h` |
| `DEVICE_TOKEN recusado` | O token no `secrets_local.h` tem que ser igual ao do Netlify (e faça um novo deploy depois de mudar a variável) |
| `Netlify inacessível` | Confira a `NETLIFY_URL` (com `https://`) e o Wi-Fi |
| Só sai som de um lado | Confira o pino **SD** de cada amplificador ([guia 2](02-montagem.md)) |
| Som baixo ou distorcido | Ajuste o volume no painel. Use alto-falantes de 4–8 Ω e fonte de 2 A |
| ESP32 reinicia ao falar alto | A fonte é fraca: use 5 V / 2 A e um cabo curto |
| LED não acende | Na DevKitC-1 v1.1, mude `PIN_RGB_LED` para 38 |
| Painel diz “ESP32 nunca conectou” | Veja o monitor serial: sem mensagens de erro de token ou de Netlify, ele sincroniza em até 3 min depois de ligar |
| A IA responde “chave do Gemini não configurada” | Cole a chave em **Sistema → Chaves de API** |

Não achou o seu problema? Abra uma **Issue** aqui no GitHub com o que aparece no monitor serial.
