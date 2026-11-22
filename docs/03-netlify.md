# 3. Servidor grátis (Netlify)

O Netlify roda a IA, a voz, o painel e as integrações. Ele só liga quando alguém chama, por isso cabe no plano gratuito.

## 3.1 Deploy com 1 clique
1. Crie uma conta em netlify.com. Dá para entrar com o GitHub.
2. Clique no botão abaixo. Ele cria uma cópia deste repositório na sua conta do GitHub e publica o site:

   [![Deploy to Netlify](https://www.netlify.com/img/deploy/button.svg)](https://app.netlify.com/start/deploy?repository=https://github.com/Carlos05125877/Assistente-de-Voz)

3. Anote o endereço do site, por exemplo `https://meu-jarvis.netlify.app`. Ele vai no firmware.

## 3.2 Senhas (variáveis de ambiente)
Em **Site configuration → Environment variables**, crie:

| Variável | O que colocar |
|---|---|
| `PANEL_PASSWORD` | Uma senha para entrar no painel |
| `DEVICE_TOKEN` | Um texto longo e aleatório, por exemplo de um gerador de senhas com 32+ caracteres. O mesmo valor vai no ESP32 |

Depois, em **Deploys → Trigger deploy**, publique de novo para as variáveis valerem.

## 3.3 Chaves de API (grátis)
1. **Gemini (a IA):** em aistudio.google.com, vá em **Get API key** e crie uma chave.
2. **Groq (voz → texto):** em console.groq.com, vá em **API Keys** e crie uma chave.
3. Abra o seu site, entre com a `PANEL_PASSWORD` e, na aba **Sistema → Chaves de API**, cole as duas chaves e clique em **Salvar chaves**.
   A chave do Groq é enviada sozinha para o ESP32.

## 3.4 Integrações opcionais (no painel)
- **Bíblia:** na aba **Bíblia**, entre com o e-mail e a senha do YouVersion. Se a sua conta usa login do Google ou da Apple, siga o passo a passo que aparece ali.
- **Spotify:**
  1. Em developer.spotify.com, crie um app.
  2. Copie a *Redirect URI* que aparece na aba **Spotify** do painel e cadastre no app.
  3. No painel, cole o Client ID e o Client Secret.
- **Casa inteligente:**
  1. Em iot.tuya.com, crie um projeto e vincule o app SmartLife.
  2. No painel, cole o Access ID e o Secret.
  3. Cadastre os dispositivos com o nome que você vai falar.

## 3.5 Quanto gasta do plano grátis
O plano free tem **300 créditos por mês** e não cobra se passar: o site só pausa até o mês seguinte.
Pela estimativa, o uso pessoal fica entre **60 e 120 créditos por mês**.

> ⚠️ **Cada deploy de produção gasta 15 créditos.** Junte as mudanças e publique de uma vez. Acompanhe o consumo em *Usage & billing*.

Próximo: [4. Firmware (Arduino IDE) →](04-firmware.md)
