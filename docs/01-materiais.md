# 1. Lista de materiais

| Qtd | Item | Detalhes / o que procurar | Observação |
|---|---|---|---|
| 1 | **ESP32-S3 DevKitC-1 N16R8** | “ESP32-S3 N16R8”: **16 MB de flash e 8 MB de PSRAM** | ⚠️ Tem que ser N16R8 (ou N8R8). Sem PSRAM a palavra “Jarvis” não roda |
| 1 | **Microfone I2S INMP441** | “INMP441 I2S” | Digital, ligação direta no ESP32 |
| 2 | **Amplificador I2S MAX98357A** | “MAX98357A I2S 3W” | Um para cada alto-falante (som estéreo) |
| 2 | **Alto-falante 4 Ω ou 8 Ω, até 3 W** | “alto-falante 3W 4 ohm 40 mm” (ou 50 mm) | Confira o diâmetro na pasta da [caixa 3D](../hardware/caixa-3d/) |
| 1 | **Botão (push-button)** | “chave táctil 12x12” | Opcional: o botão BOOT da placa também funciona |
| 1 | Resistor **470 kΩ** | — | Coloca o 2º amplificador no canal direito |
| 1 | Cabo **USB-C** de dados | — | Para gravar o firmware e alimentar |
| 1 | Fonte **5 V / 2 A** USB | Carregador de celular serve | |
| — | Jumpers / fios, solda e espaguete | — | |
| 4 | Parafusos **M3** (ou M2.5) | Para fechar a tampa | |
| — | Filamento **PLA** ou PETG | ~80–120 g | Para imprimir a caixa |

## Contas gratuitas que você vai criar
| Serviço | Para quê | Onde |
|---|---|---|
| **Netlify** | Servidor e painel | netlify.com |
| **Google AI Studio** | Chave do Gemini (a IA) | aistudio.google.com |
| **Groq** | Chave do Whisper (voz → texto) | console.groq.com |
| Spotify Developer *(opcional)* | Controlar o Spotify | developer.spotify.com |
| Tuya IoT *(opcional)* | Lâmpadas e tomadas SmartLife | iot.tuya.com |

> Nenhum desses planos pede cartão. O Spotify precisa de conta Premium para controlar a reprodução.

Próximo: [2. Montagem e ligações →](02-montagem.md)
