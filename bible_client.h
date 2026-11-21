#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "config_private.h"

// ============================================================================
// CLIENTE OFICIAL APP DA BIBLIA — YOUVERSION / HOLY BIBLE (bible.com/pt)
// ============================================================================
// Conecta diretamente com o ecossistema do App da Biblia (YouVersion):
//   https://www.bible.com/pt/reading-plans
//   https://www.bible.com/pt/verse-of-the-day
//   https://www.bible.com/pt/bible/129/... (NVI, ARC, NVT, ARA)
//
// Funcionalidades:
// 1. Planos de Leitura & Devocionais Diarios (https://www.bible.com/pt/reading-plans)
//    - Guarda na memoria Flash NVS do ESP32 qual Plano de Leitura voce esta fazendo
//      e em qual Dia do Plano voce esta (Dia 1, Dia 2, Dia 3...)!
//    - Permite iniciar um plano por tema ("ansiedade", "paz", "sabedoria", "esperanca",
//      "gratidao") ou por ID/URL direta de bible.com/pt/reading-plans/{slug}.
//    - Le a Reflexao Devocional do Dia + a Passagem Biblica correspondente por voz!
//    - Comando para avancar para o Proximo Dia do Plano ("proximo dia do plano").
// 2. Versiculo do Dia Oficial do YouVersion (bible.com/pt/verse-of-the-day).
// 3. Leitura de qualquer Livro, Capitulo e Versiculo em Portugues (NVI / Almeida).
// 4. Integra-se automaticamente com o Servidor Externo Jarvis Bridge (se ativo)
//    para scraping completo do Next.js do bible.com, ou opera 100% standalone no ESP32!
// ============================================================================

struct YouVersionPlanDay {
  const char* dayTitle;
  const char* devotionalText;
  const char* scriptureRef;
  const char* scriptureText;
};

struct YouVersionReadingPlan {
  const char* planKey;
  const char* planSlug;
  const char* title;
  const char* topics;
  int totalDays;
  YouVersionPlanDay days[5];
};

// Planos de Leitura e Devocionais do YouVersion (https://www.bible.com/pt/reading-plans)
// Embarcados em PROGMEM/Flash no ESP32 + sincronizados ao vivo com bible.com/pt/reading-plans
static const YouVersionReadingPlan YOUVERSION_READING_PLANS[] = {
  {
    "ansiedade",
    "da-ansiedade-a-paz",
    "Da Ansiedade a Paz (Plano YouVersion)",
    "ansiedade,paz,mente,preocupacao,medo,calma",
    5,
    {
      {
        "Dia 1 de 5: Entregando o Peso da Preocupacao",
        "A ansiedade tenta nos convencer de que precisamos controlar o amanha. Mas Deus nos convida a trocar a preocupacao pela oracao com acao de gracas. Quando voce entrega suas inquietacoes a Cristo, a paz de Deus guarda o seu coracao e os seus pensamentos.",
        "Filipenses 4:6-7 (NVI)",
        "Nao andem ansiosos por coisa alguma, mas em tudo, pela oracao e suplicas, e com acao de gracas, apresentem seus pedidos a Deus. E a paz de Deus, que excede todo o entendimento, guardara o coracao e a mente de voces em Cristo Jesus."
      },
      {
        "Dia 2 de 5: Descansando no Cuidado do Pai",
        "Jesus nos lembra que o Pai celestial cuida das aves do ceu e dos lirios do campo. Voce vale muito mais para Ele. Respire fundo hoje e viva um dia de cada vez, confiando que Deus ja esta cuidando do seu amanha.",
        "Mateus 6:33-34 (NVI)",
        "Busquem, pois, em primeiro lugar o Reino de Deus e a sua justica, e todas essas coisas serao acrescentadas a voces. Portanto, nao se preocupem com o amanha."
      },
      {
        "Dia 3 de 5: Lancando Toda a Ansiedade Sobre Ele",
        "Confiar em Deus significa reconhecer que nao precisamos carregar o mundo sozinhos. Lance sobre Ele toda a sua ansiedade, porque Ele tem cuidado pessoal e constante de voce.",
        "1 Pedro 5:7 (NVI)",
        "Lancem sobre ele toda a sua ansiedade, porque ele tem cuidado de voces."
      },
      {
        "Dia 4 de 5: A Paz que o Mundo Nao Pode Dar",
        "A paz de Cristo nao e a ausencia de lutas, mas a presenca do Salvador conosco. Mesmo diante de desafios, nao deixe o seu coracao se perturbar nem tenha medo.",
        "Joao 14:27 (NVI)",
        "Deixo a paz a voces; a minha paz dou a voces. Nao a dou como o mundo a da. Nao se perturbe o seu coracao, nem tenham medo."
      },
      {
        "Dia 5 de 5: Mente Firme na Rocha Eterna",
        "A verdadeira serenidade nasce quando mantemos nossos pensamentos firmados nas promessas do Senhor. Ao concluir este plano do App Biblia, escolha meditar diariamente na fidelidade de Deus.",
        "Isaias 26:3 (NVI)",
        "Tu conservaras em perfeita paz aquele cujo proposito esta firme, porque em ti confia."
      }
    }
  },
  {
    "sabedoria",
    "sabedoria-diaria-em-proverbios",
    "Sabedoria Diaria em Proverbios (Plano YouVersion)",
    "sabedoria,proverbios,decisoes,trabalho,foco,inteligencia",
    5,
    {
      {
        "Dia 1 de 5: Confie no Senhor de Todo o Coracao",
        "Antes de tomar decisoes importantes, a Biblia nos ensina que a verdadeira sabedoria comeca na confianca total no Senhor, sem nos apoiarmos apenas no nosso proprio entendimento.",
        "Proverbios 3:5-6 (NVI)",
        "Confie no Senhor de todo o seu coracao e nao se apoie em seu proprio entendimento; reconheca o Senhor em todos os seus caminhos, e ele endireitara as suas veredas."
      },
      {
        "Dia 2 de 5: Guardando o Coracao",
        "Tudo o que fazemos, falamos e decidimos flui do nosso interior. Proteger a mente e o coracao com a Palavra de Deus e o segredo para uma vida integra e abencoada.",
        "Proverbios 4:23 (NVI)",
        "Acima de tudo, guarde o seu coracao, pois dele depende toda a sua vida."
      },
      {
        "Dia 3 de 5: Sabedoria no Falar",
        "Nossas palavras tem poder para curar ou ferir. A sabedoria de Proverbios nos ensina a responder com mansidao, promovendo a paz em nossa casa e trabalho.",
        "Proverbios 15:1 (NVI)",
        "A resposta calma desvia a furia, mas a palavra rispida desperta a ira."
      },
      {
        "Dia 4 de 5: Consagrando Seus Planos a Deus",
        "Planejar e fundamental, mas entregar cada projeto nas maos do Criador garante que os nossos passos estejam alinhados com um proposito maior.",
        "Proverbios 16:3 (NVI)",
        "Consagre ao Senhor tudo o que voce faz, e os seus planos serao bem-sucedidos."
      },
      {
        "Dia 5 de 5: Sabedoria que Vem do Alto",
        "Se voce precisa de direcao para uma escolha dificil hoje, peca a Deus com fe. Ele concede sabedoria generosamente a todos os que O buscam.",
        "Tiago 1:5 (NVI)",
        "Se algum de voces tem falta de sabedoria, peca-a a Deus, que a todos da livremente, de boa vontade; e lhe sera concedida."
      }
    }
  },
  {
    "esperanca",
    "esperanca-e-fe-nas-promessas",
    "Esperanca e Fe nas Promessas (Plano YouVersion)",
    "esperanca,fe,futuro,forca,coragem,vitoria,protecao",
    5,
    {
      {
        "Dia 1 de 5: Planos de Esperanca e Futuro",
        "Mesmo quando o cenario parece incerto, os planos de Deus para a sua vida sao planos de paz e proposito. A esperanca crista e uma ancora firme para a alma.",
        "Jeremias 29:11 (NVI)",
        "Porque sou eu que conheco os planos que tenho para voces, diz o Senhor, planos de faze-los prosperar e nao de causar dano, planos de dar a voces esperanca e um futuro."
      },
      {
        "Dia 2 de 5: Renovando as Forcas como a Aguia",
        "O cansaco pode surgir na caminhada, mas aqueles que esperam no Senhor recebem um vigor novo para superar qualquer obstaculo.",
        "Isaias 40:31 (NVI)",
        "Mas aqueles que esperam no Senhor renovam as suas forcas. Voam alto como aguias; correm e nao ficam exaustos, andam e nao se cansam."
      },
      {
        "Dia 3 de 5: Tudo Coopera para o Bem",
        "Nenhuma fase dificil e desperdicada nas maos de Deus. Ele tem poder para transformar ate as adversidades em crescimento e proposito.",
        "Romanos 8:28 (NVI)",
        "Sabemos que Deus age em todas as coisas para o bem daqueles que o amam, dos que foram chamados de acordo com o seu proposito."
      },
      {
        "Dia 4 de 5: Seja Forte e Corajoso",
        "Deus nao nos deu espirito de timidez, mas de poder, amor e equilibrio. Avance com coragem hoje, pois o Senhor e contigo por onde quer que andares.",
        "Josue 1:9 (NVI)",
        "Nao fui eu que ordenei a voce? Seja forte e corajoso! Nao se apavore nem desanime, pois o Senhor, o seu Deus, estara com voce por onde voce andar."
      },
      {
        "Dia 5 de 5: O Bom Pastor Guia Seus Passos",
        "Mesmo nos vales mais dificeis, a presenca do Bom Pastor traz consolo, protecao e a certeza de que a bondade e a misericordia nos seguirao todos os dias.",
        "Salmos 23:1-4 (NVI)",
        "O Senhor e o meu pastor; de nada terei falta. Em verdes pastagens me faz repousar e me conduz a aguas tranquilas; restaura-me o vigor."
      }
    }
  },
  {
    "gratidao",
    "habito-da-gratidao-e-louvor",
    "O Habito da Gratidao e Louvor (Plano YouVersion)",
    "gratidao,louvor,alegria,amor,familia,bencao",
    3,
    {
      {
        "Dia 1 de 3: Um Coracao Agradecido",
        "A gratidao transforma a nossa visao espiritual: em vez de olharmos para o que falta, celebramos a bondade fiel de Deus em nossa vida.",
        "Salmos 100:4-5 (NVI)",
        "Entrem por suas portas com acoes de gracas e em seus atrios com louvor; deem-lhe gracas e bendigam o seu nome. Pois o Senhor e bom e o seu amor leal e eterno."
      },
      {
        "Dia 2 de 3: As Misericordias de Cada Manha",
        "Cada novo amanhecer e um presente da graca de Deus. Agradeca hoje pela vida, pela saude e pelas misericordias do Senhor que se renovam.",
        "Lamentacoes 3:22-23 (NVI)",
        "Gracas ao grande amor do Senhor e que nao somos consumidos, pois as suas misericordias sao inesgotaveis. Renovam-se cada manha; grande e a sua fidelidade!"
      },
      {
        "Dia 3 de 3: Alegria e Gratidao emTodo Tempo",
        "Alegrem-se sempre, orem continuamente e deem gracas em todas as circunstancias, pois esta e a vontade de Deus em Cristo Jesus para voces.",
        "1 Tessalonicenses 5:16-18 (NVI)",
        "Alegrem-se sempre, orem continuamente, deem gracas em todas as circunstancias, pois esta e a vontade de Deus para voces em Cristo Jesus."
      }
    }
  }
};

static const size_t YOUVERSION_PLAN_COUNT = sizeof(YOUVERSION_READING_PLANS) / sizeof(YOUVERSION_READING_PLANS[0]);

class BibleClient {
public:
  Preferences biblePrefs;
  int activePlanIndex = 0;      // Plano ativo no YouVersion
  int activePlanDay   = 1;      // Dia atual do plano (1..N)
  String bibleVersion = "NVI";  // NVI (129), ARC (211), NVT (193)
  String customPlanSlug = "";   // Caso o usuario informe um ID/URL de bible.com/pt/reading-plans/...

  String lastReference = "Filipenses 4:6-7 (NVI)";
  String lastVerseText = "Da Ansiedade a Paz (Dia 1): Nao andem ansiosos por coisa alguma, mas em tudo, pela oracao e suplicas, apresentem seus pedidos a Deus.";

  void begin() {
    biblePrefs.begin("jarvis_bible", false);
    activePlanIndex = biblePrefs.getInt("plan_idx", 0);
    if (activePlanIndex < 0 || activePlanIndex >= (int)YOUVERSION_PLAN_COUNT) {
      activePlanIndex = 0;
    }
    activePlanDay = biblePrefs.getInt("plan_day", 1);
    bibleVersion  = biblePrefs.getString("version", "NVI");
    customPlanSlug = biblePrefs.getString("custom_slug", "");

    if (customPlanSlug.length() > 0) {
      int idx = customPlanSlug.indexOf("/reading-plans/");
      if (idx >= 0) {
        customPlanSlug = customPlanSlug.substring(idx + 15);
      }
      int dayIdx = customPlanSlug.indexOf("/day/");
      if (dayIdx >= 0) {
        customPlanSlug = customPlanSlug.substring(0, dayIdx);
      }
      customPlanSlug.trim();
      if (customPlanSlug.endsWith("/")) {
        customPlanSlug.remove(customPlanSlug.length() - 1);
      }
    }

    const YouVersionReadingPlan& p = YOUVERSION_READING_PLANS[activePlanIndex];
    if (customPlanSlug.length() > 0) {
      if (activePlanDay < 1 || activePlanDay > 365) activePlanDay = 1;
      lastReference = "Plano Bible.com (" + customPlanSlug + ") - Dia " + String(activePlanDay);
      lastVerseText = "Pressione Devocional de Hoje para ler o Dia " + String(activePlanDay) + " em bible.com/pt/reading-plans.";
    } else {
      if (activePlanDay < 1 || activePlanDay > p.totalDays) activePlanDay = 1;
      lastReference = String(p.title) + " - Dia " + String(activePlanDay);
      lastVerseText = String(p.days[activePlanDay - 1].devotionalText) + " (" +
                      String(p.days[activePlanDay - 1].scriptureRef) + ")";
    }

    Serial.printf("📖 [YouVersion Bible.com] Plano Ativo: '%s' (Dia %d | Traducao: %s)\n",
                  customPlanSlug.length() > 0 ? customPlanSlug.c_str() : p.title,
                  activePlanDay, bibleVersion.c_str());
  }

  void saveProgressToNVS() {
    biblePrefs.putInt("plan_idx", activePlanIndex);
    biblePrefs.putInt("plan_day", activePlanDay);
    biblePrefs.putString("version", bibleVersion);
    biblePrefs.putString("custom_slug", customPlanSlug);
  }

  String getActivePlanStatus() {
    if (customPlanSlug.length() > 0) {
      return "Plano Bible.com (" + customPlanSlug + ") - Dia " + String(activePlanDay);
    }
    const YouVersionReadingPlan& p = YOUVERSION_READING_PLANS[activePlanIndex];
    return String(p.title) + " — Dia " + String(activePlanDay) + " de " + String(p.totalDays);
  }

  static String urlEncode(const String& str) {
    String encoded = "";
    char bufHex[4];
    for (size_t i = 0; i < str.length(); i++) {
      uint8_t c = (uint8_t)str.charAt(i);
      if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == ':') {
        encoded += (char)c;
      } else if (c == ' ') {
        encoded += "%20";
      } else {
        snprintf(bufHex, sizeof(bufHex), "%%%02X", c);
        encoded += bufHex;
      }
    }
    return encoded;
  }

  static String normalizeReference(String ref) {
    ref.trim();
    ref.replace("João", "Joao"); ref.replace("joão", "Joao");
    ref.replace("Gênesis", "Genesis"); ref.replace("gênesis", "Genesis");
    ref.replace("Êxodo", "Exodo"); ref.replace("êxodo", "Exodo");
    ref.replace("Josué", "Josue"); ref.replace("Provérbios", "Proverbios");
    ref.replace("provérbios", "Proverbios"); ref.replace("Isaías", "Isaias");
    ref.replace("isaías", "Isaias"); ref.replace("Coríntios", "Corintios");
    ref.replace("coríntios", "Corintios"); ref.replace("Efésios", "Efesios");
    ref.replace("Filipenses", "Filipenses"); ref.replace("Salmo ", "Salmos ");
    return ref;
  }

  // Extrai o conteudo da meta tag og:description de uma pagina HTML do bible.com/pt
  static String extractMetaContent(const String& html, const char* propName) {
    int idx = html.indexOf(propName);
    if (idx < 0) return "";
    int contentIdx = html.indexOf("content=\"", idx);
    if (contentIdx < 0 || contentIdx - idx > 160) return "";
    contentIdx += 9;
    int endQuote = html.indexOf("\"", contentIdx);
    if (endQuote < 0) return "";
    String val = html.substring(contentIdx, endQuote);
    val.replace("&quot;", "\"");
    val.replace("&#x27;", "'");
    val.replace("&amp;", "&");
    val.trim();
    return val;
  }

  // Extrai o conteudo de uma meta tag HTML (og:description / og:title) em streaming do socket TCP
  // sem alocar o HTML inteiro de ~250KB do Next.js na RAM do ESP-WROOM-32!
  static bool streamExtractOgMeta(WiFiClientSecure& cli, const String& url,
                                  String& outTitle, String& outDescription) {
    outTitle = "";
    outDescription = "";
    cli.stop();
    cli.setInsecure();
    cli.setTimeout(10000);

    HTTPClient http;
    if (!http.begin(cli, url)) return false;
    http.addHeader("User-Agent", "Mozilla/5.0 (ESP32; YouVersionClient)");
    int code = http.GET();
    if (code != 200) {
      http.end();
      return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    String window = "";
    window.reserve(1024);
    uint8_t buf[256];
    uint32_t t0 = millis();
    size_t totalRead = 0;

    // As tags <meta property="og:title"> e <meta property="og:description"> ficam nos primeiros ~25KB do <head>
    while ((http.connected() || stream->available() > 0) &&
           (millis() - t0 < 8000) &&
           totalRead < 36000 &&
           (outTitle.length() == 0 || outDescription.length() == 0)) {
      int avail = stream->available();
      if (avail <= 0) {
        delay(1);
        continue;
      }
      int toRead = min(avail, (int)sizeof(buf));
      int got = stream->readBytes(buf, toRead);
      totalRead += got;

      for (int i = 0; i < got; ++i) {
        char c = (char)buf[i];
        if (c != '\r' && c != '\n') window += c;
      }

      if (outTitle.length() == 0) {
        String t = extractMetaContent(window, "og:title");
        if (t.length() > 0) outTitle = t;
      }
      if (outDescription.length() == 0) {
        String d = extractMetaContent(window, "og:description");
        if (d.length() > 0) outDescription = d;
      }

      if (window.length() > 768) {
        window.remove(0, window.length() - 384);
      }
    }

    http.end();
    return outDescription.length() > 10;
  }
  // 1. Tenta consultar via Servidor Externo Jarvis Bridge (/api/bible/youversion)
  bool callExternalYouVersionBridge(const String& action, const String& planOrTopic,
                                    int day, const String& ref, String& outSpoken) {
    if (strlen(EXTERNAL_SERVER_URL) <= 8) return false;

    HTTPClient http;
    String url = String(EXTERNAL_SERVER_URL) + "/api/bible/youversion?action=" + urlEncode(action) +
                 "&plan=" + urlEncode(planOrTopic) +
                 "&day=" + String(day) +
                 "&ref=" + urlEncode(ref) +
                 "&version=" + urlEncode(bibleVersion);

    if (!http.begin(url)) return false;
    http.setTimeout(8000);
    int code = http.GET();
    if (code != 200) {
      http.end();
      return false;
    }

    String resp = http.getString();
    http.end();

    DynamicJsonDocument doc(3072);
    if (deserializeJson(doc, resp)) return false;

    String spoken = doc["spoken"] | "";
    String rRef   = doc["reference"] | "";
    String rTxt   = doc["text"] | "";
    int rDay      = doc["day"] | activePlanDay;

    if (spoken.length() > 0) {
      activePlanDay = rDay;
      saveProgressToNVS();
      if (rRef.length() > 0) lastReference = rRef;
      if (rTxt.length() > 0) lastVerseText = rTxt;
      outSpoken = spoken;
      return true;
    }
    return false;
  }

  // 2. Busca o Versiculo do Dia diretamente de https://www.bible.com/pt/verse-of-the-day
  String fetchYouVersionVerseOfTheDay(WiFiClientSecure& cli) {
    String bridgeOut;
    if (callExternalYouVersionBridge("verse_of_day", "", 0, "", bridgeOut)) {
      return bridgeOut;
    }

    String title, desc;
    if (streamExtractOgMeta(cli, "https://www.bible.com/pt/verse-of-the-day", title, desc)) {
      lastReference = title.length() > 0 ? title : "Versiculo do Dia (Bible.com)";
      lastVerseText = desc;
      return "Versiculo do Dia no App Biblia YouVersion: " + desc;
    }

    // Fallback para a escritura do dia atual do plano YouVersion
    const YouVersionReadingPlan& p = YOUVERSION_READING_PLANS[activePlanIndex];
    int safeDay = constrain(activePlanDay, 1, p.totalDays);
    const YouVersionPlanDay& d = p.days[safeDay - 1];
    lastReference = d.scriptureRef;
    lastVerseText = d.scriptureText;
    return String("Versiculo do Dia no App Biblia YouVersion, ") + d.scriptureRef + ": " + d.scriptureText;
  }

  // 3. Le o Devocional + Escritura do Dia atual do Plano de Leitura de https://www.bible.com/pt/reading-plans
  String readCurrentPlanDay(WiFiClientSecure& cli, int requestedDay = 0) {
    if (requestedDay > 0) activePlanDay = requestedDay;

    String bridgeOut;
    if (callExternalYouVersionBridge("read_plan_day", customPlanSlug, activePlanDay, "", bridgeOut)) {
      return bridgeOut;
    }

    // Se o usuario definiu um slug especifico de https://www.bible.com/pt/reading-plans/{slug}, busca em streaming!
    if (customPlanSlug.length() > 0) {
      String planUrl = "https://www.bible.com/pt/reading-plans/" + customPlanSlug + "/day/" + String(activePlanDay);
      String title, desc;
      if (streamExtractOgMeta(cli, planUrl, title, desc)) {
        lastReference = title.length() > 0 ? title : ("Plano " + customPlanSlug + " Dia " + String(activePlanDay));
        lastVerseText = desc;
        saveProgressToNVS();
        return lastReference + ". Devocional: " + desc;
      }
    }

    const YouVersionReadingPlan& p = YOUVERSION_READING_PLANS[activePlanIndex];
    if (activePlanDay < 1 || activePlanDay > p.totalDays) activePlanDay = 1;
    saveProgressToNVS();

    const YouVersionPlanDay& d = p.days[activePlanDay - 1];
    lastReference = String(p.title) + " (" + String(d.dayTitle) + ")";
    lastVerseText = String(d.devotionalText) + " — " + String(d.scriptureRef) + ": " + String(d.scriptureText);

    String spoken = String("App Biblia YouVersion, plano ") + p.title + ". " +
                    d.dayTitle + ". " +
                    "Reflexao devocional: " + d.devotionalText + " " +
                    "Leitura Biblica em " + d.scriptureRef + ": " + d.scriptureText;
    Serial.printf("📖 [YouVersion Plan] %s\n", spoken.c_str());
    return spoken;
  }

  // 4. Avanca para o Proximo Dia do Plano de Leitura e ja le o devocional
  String advanceToNextPlanDay(WiFiClientSecure& cli) {
    String bridgeOut;
    if (callExternalYouVersionBridge("next_plan_day", "", 0, "", bridgeOut)) {
      return bridgeOut;
    }

    const YouVersionReadingPlan& p = YOUVERSION_READING_PLANS[activePlanIndex];
    activePlanDay++;
    if (customPlanSlug.length() == 0 && activePlanDay > p.totalDays) {
      activePlanDay = 1;
    }
    saveProgressToNVS();
    return "Avancando para o Dia " + String(activePlanDay) + " do seu plano no App Biblia. " +
           readCurrentPlanDay(cli, activePlanDay);
  }

  // 5. Inicia ou troca de Plano de Leitura do YouVersion (por tema ou ID de bible.com/pt/reading-plans)
  String startReadingPlan(WiFiClientSecure& cli, const String& topicOrPlanName) {
    String q = topicOrPlanName;
    q.toLowerCase();
    q.trim();

    String bridgeOut;
    if (callExternalYouVersionBridge("start_plan", topicOrPlanName, 1, "", bridgeOut)) {
      // Atualiza indice local tambem
      for (size_t i = 0; i < YOUVERSION_PLAN_COUNT; ++i) {
        if (q.indexOf(YOUVERSION_READING_PLANS[i].planKey) >= 0 ||
            String(YOUVERSION_READING_PLANS[i].topics).indexOf(q) >= 0) {
          activePlanIndex = (int)i;
          activePlanDay = 1;
          customPlanSlug = "";
          saveProgressToNVS();
          break;
        }
      }
      return bridgeOut;
    }

    // Procura nos planos YouVersion por tema ou titulo
    for (size_t i = 0; i < YOUVERSION_PLAN_COUNT; ++i) {
      String key = YOUVERSION_READING_PLANS[i].planKey;
      String topics = YOUVERSION_READING_PLANS[i].topics;
      String title = YOUVERSION_READING_PLANS[i].title;
      title.toLowerCase();

      if (q.indexOf(key) >= 0 || topics.indexOf(q) >= 0 || title.indexOf(q) >= 0) {
        activePlanIndex = (int)i;
        activePlanDay = 1;
        customPlanSlug = "";
        saveProgressToNVS();
        return "Iniciando o plano de leitura " + String(YOUVERSION_READING_PLANS[i].title) +
               " no App Biblia YouVersion! " + readCurrentPlanDay(cli, 1);
      }
    }

    // Se o usuario informou um slug ou URL completa de bible.com/pt/reading-plans (ex: "https://www.bible.com/pt/reading-plans/1234-plano")
    if (q.length() > 2) {
      String cleanSlug = q;
      int rpIdx = cleanSlug.indexOf("/reading-plans/");
      if (rpIdx >= 0) {
        cleanSlug = cleanSlug.substring(rpIdx + 15);
      }
      int dIdx = cleanSlug.indexOf("/day/");
      if (dIdx >= 0) {
        cleanSlug = cleanSlug.substring(0, dIdx);
      }
      cleanSlug.trim();
      if (cleanSlug.endsWith("/")) {
        cleanSlug.remove(cleanSlug.length() - 1);
      }
      customPlanSlug = cleanSlug;
      activePlanDay = 1;
      saveProgressToNVS();
      return readCurrentPlanDay(cli, 1);
    }

    return readCurrentPlanDay(cli, activePlanDay);
  }

  // 6. Lista os Planos de Leitura disponiveis em https://www.bible.com/pt/reading-plans
  String listReadingPlans(WiFiClientSecure& cli) {
    String bridgeOut;
    if (callExternalYouVersionBridge("list_plans", "", 0, "", bridgeOut)) {
      return bridgeOut;
    }

    String list = "";
    for (size_t i = 0; i < YOUVERSION_PLAN_COUNT; ++i) {
      if (i > 0) list += "; ";
      list += String(YOUVERSION_READING_PLANS[i].title) + " (" +
              String(YOUVERSION_READING_PLANS[i].totalDays) + " dias)";
    }
    const YouVersionReadingPlan& cur = YOUVERSION_READING_PLANS[activePlanIndex];
    return "No App Biblia YouVersion (bible.com/pt/reading-plans), seu plano ativo e " +
           String(cur.title) + ", no Dia " + String(activePlanDay) + " de " + String(cur.totalDays) +
           ". Planos disponiveis: " + list + ". Diga qual tema deseja iniciar ou peca para ler o devocional de hoje.";
  }

  // 7. Consulta qualquer passagem biblica (ex: Salmos 23, Joao 3:16, Romanos 8:28)
  bool fetchPassage(WiFiClientSecure& cli, const String& reference, String& outText) {
    String cleanRef = normalizeReference(reference);
    if (cleanRef.length() == 0) cleanRef = "Salmos 23:1-4";

    if (callExternalYouVersionBridge("get_passage", "", 0, cleanRef, outText)) {
      return true;
    }

    cli.stop();
    cli.setInsecure();
    cli.setTimeout(12000);

    HTTPClient http;
    String url = String("https://bible-api.com/") + urlEncode(cleanRef) + "?translation=almeida";
    if (!http.begin(cli, url)) {
      return false;
    }

    int code = http.GET();
    String resp = http.getString();
    http.end();

    if (code != 200) {
      return false;
    }

    StaticJsonDocument<256> filter;
    filter["reference"] = true;
    filter["text"] = true;

    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, resp, DeserializationOption::Filter(filter))) {
      return false;
    }

    String refName = doc["reference"] | cleanRef;
    String verseText = doc["text"] | "";
    verseText.replace("\n", " ");
    verseText.replace("  ", " ");
    verseText.trim();

    if (verseText.length() == 0) return false;
    if (verseText.length() > 520) {
      verseText = verseText.substring(0, 515) + "...";
    }

    lastReference = refName + " (" + bibleVersion + ")";
    lastVerseText = verseText;
    outText = lastReference + ". " + verseText;
    return true;
  }

  // Dispatcher unificado para o Gemini Function Calling e Painel Web HUD
  String executeCommand(WiFiClientSecure& cli, const String& action,
                        const String& reference, const String& planOrTheme, int day = 0) {
    if (action == "read_plan_day" || action == "daily_devotional") {
      return readCurrentPlanDay(cli, day);
    }
    if (action == "next_plan_day") {
      return advanceToNextPlanDay(cli);
    }
    if (action == "start_plan") {
      return startReadingPlan(cli, planOrTheme.length() > 0 ? planOrTheme : reference);
    }
    if (action == "list_plans") {
      return listReadingPlans(cli);
    }
    if (action == "verse_of_day") {
      return fetchYouVersionVerseOfTheDay(cli);
    }
    if (action == "get_passage" && reference.length() > 0) {
      String out;
      if (fetchPassage(cli, reference, out)) return out;
      return "Nao consegui localizar a passagem " + reference + " no App Biblia.";
    }
    if (action == "theme_verse" && planOrTheme.length() > 0) {
      return startReadingPlan(cli, planOrTheme);
    }
    return readCurrentPlanDay(cli, day);
  }
};
