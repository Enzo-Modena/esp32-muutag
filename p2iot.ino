// ===== BLYNK CONFIG =====
#define BLYNK_TEMPLATE_ID "TMPL2zQwAKRZc"
#define BLYNK_TEMPLATE_NAME "muutag"
#define BLYNK_AUTH_TOKEN "fpOagnaNgC5HnlJrFRvMpHQAwCa82OP2"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <RTClib.h>

#include <PubSubClient.h>
#include <ArduinoJson.h>

// ===== WIFI =====
char ssid[] = "BDAG";
char pass[] = "bdag2018";

// ===== MQTT =====
const char* mqtt_server = "10.64.79.42";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

const int ID_COCHO = 1;

// ===== RFID =====
#define SS_PIN 5
#define RST_PIN 4

MFRC522 rfid(SS_PIN, RST_PIN);

// ===== RTC =====
RTC_DS1307 rtc;

// ===== RELÉ =====
#define RELE_PIN 26

// ===== CONTROLE MQTT =====
float tempoRecebido = 0;
bool liberarRacao = false;

bool releLigado = false;

unsigned long filaTempoMs = 0;
unsigned long ultimoTick = 0;

// ===== RELÉ VIA BLYNK =====
BLYNK_WRITE(V0)
{
  int estado = param.asInt();

  digitalWrite(RELE_PIN, !estado);

  Serial.print("Rele via Blynk: ");
  Serial.println(estado ? "LIGADO" : "DESLIGADO");
}

// ===== CALLBACK MQTT =====
void callback(char* topic, byte* payload, unsigned int length)
{
  String mensagem = "";

  for (unsigned int i = 0; i < length; i++)
  {
    mensagem += (char)payload[i];
  }

  Serial.println();
  Serial.println("===== MQTT RECEBIDO =====");
  Serial.print("Topico: ");
  Serial.println(topic);
  Serial.println(mensagem);

  DynamicJsonDocument doc(512);

  DeserializationError erro = deserializeJson(doc, mensagem);

  if (erro)
  {
    Serial.println("Erro ao interpretar JSON");
    return;
  }

  tempoRecebido = doc["tempo_segundos"].as<float>();

  Serial.print("Tempo recebido: ");
  Serial.print(tempoRecebido);
  Serial.println(" segundos");

  liberarRacao = true;
}

// ===== CONEXÃO MQTT =====
void reconnectMQTT()
{
  while (!client.connected())
  {
    Serial.print("Conectando MQTT...");

    if (client.connect("ESP32_MUU_TAG"))
    {
      Serial.println("OK");

      String topico = "cocho/" + String(ID_COCHO) + "/rele";

      client.subscribe(topico.c_str());

      Serial.print("Inscrito em: ");
      Serial.println(topico);
    }
    else
    {
      Serial.print("Erro MQTT: ");
      Serial.println(client.state());

      delay(2000);
    }
  }
}

void setup()
{
  Serial.begin(115200);

  // ===== RFID =====
  SPI.begin(18, 19, 23, 5);
  rfid.PCD_Init();

  // ===== RTC =====
  Wire.begin(21, 22);

  if (!rtc.begin())
  {
    Serial.println("RTC nao encontrado!");

    while (1);
  }

  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  // ===== RELÉ =====
  pinMode(RELE_PIN, OUTPUT);
  digitalWrite(RELE_PIN, HIGH);

  // ===== WIFI =====
  WiFi.begin(ssid, pass);

  Serial.print("Conectando WiFi");

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi conectado!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // ===== BLYNK =====
  Blynk.begin(
    BLYNK_AUTH_TOKEN,
    ssid,
    pass
  );

  Serial.println("Blynk conectado!");

  // ===== MQTT =====
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  Serial.println("Sistema de Monitoramento de Gado");
  Serial.println("Aproxime a tag RFID...");
}

void loop()
{
  Blynk.run();

  if (!client.connected())
  {
    reconnectMQTT();
  }

  client.loop();

  // ===== ACIONA RELÉ APÓS RESPOSTA DO BACKEND =====
// ===== NOVO TEMPO RECEBIDO =====
  // ===== NOVO TEMPO RECEBIDO =====
if (liberarRacao)
{
  liberarRacao = false;

  Serial.println();
  Serial.println("===== NOVA RACAO ADICIONADA =====");

  Serial.print("Tempo restante antes: ");
  Serial.print(filaTempoMs / 1000);
  Serial.println(" segundos");

  filaTempoMs += (unsigned long)(tempoRecebido * 1000);

  Serial.print("Tempo recebido: ");
  Serial.print(tempoRecebido);
  Serial.println(" segundos");

  Serial.print("Novo total na fila: ");
  Serial.print(filaTempoMs / 1000);
  Serial.println(" segundos");
}

// ===== CONTROLE DO RELÉ =====
if (filaTempoMs > 0)
{
  if (!releLigado)
  {
    releLigado = true;

    digitalWrite(RELE_PIN, LOW);

    ultimoTick = millis();

    Serial.println("Rele ligado");
  }

if (millis() - ultimoTick >= 1000)
{
  ultimoTick = millis();

  if (filaTempoMs >= 1000)
  {
    filaTempoMs -= 1000;
  }
  else
  {
    filaTempoMs = 0;
  }
}
}
else
{
  if (releLigado)
  {
    digitalWrite(RELE_PIN, HIGH);

    releLigado = false;

    Serial.println("Rele desligado");
    Serial.println("==========================");
  }
}

  // ===== VERIFICA NOVA TAG =====
  if (!rfid.PICC_IsNewCardPresent())
  {
    return;
  }

  if (!rfid.PICC_ReadCardSerial())
  {
    return;
  }

  DateTime now = rtc.now();

  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++)
  {
    if (rfid.uid.uidByte[i] < 0x10)
    {
      uid += "0";
    }

    uid += String(rfid.uid.uidByte[i], HEX);

    if (i < rfid.uid.size - 1)
    {
      uid += " ";
    }
  }

  uid.toUpperCase();

  Serial.println();
  Serial.println("========================");

  bool animalReconhecido = false;

  // ===== IDENTIFICA ANIMAL =====
  if (uid == "E7 88 6E 62")
  {
    Serial.println("Animal identificado: Vaca 2");
    animalReconhecido = true;
  }
  else if (uid == "F9 7D B5 A2")
  {
    Serial.println("Animal identificado: Vaca 1");
    animalReconhecido = true;
  }
  else
  {
    Serial.println("Animal nao cadastrado");
  }

  // ===== UID =====
  Serial.print("UID: ");
  Serial.println(uid);

  // ===== DATA =====
  Serial.print("Data: ");

  if (now.day() < 10) Serial.print("0");
  Serial.print(now.day());

  Serial.print("/");

  if (now.month() < 10) Serial.print("0");
  Serial.print(now.month());

  Serial.print("/");

  Serial.println(now.year());

  // ===== HORA =====
  Serial.print("Hora: ");

  if (now.hour() < 10) Serial.print("0");
  Serial.print(now.hour());

  Serial.print(":");

  if (now.minute() < 10) Serial.print("0");
  Serial.print(now.minute());

  Serial.print(":");

  if (now.second() < 10) Serial.print("0");
  Serial.println(now.second());

  if (animalReconhecido)
  {
    DynamicJsonDocument doc(256);

    doc["UID"] = uid;
    doc["id_cocho"] = ID_COCHO;

    // Formato ISO semelhante ao que seu amigo mostrou
    char dataHora[25];

    sprintf(
      dataHora,
      "%04d-%02d-%02dT%02d:%02d:%02d",
      now.year(),
      now.month(),
      now.day(),
      now.hour(),
      now.minute(),
      now.second()
    );

    doc["created_at"] = dataHora;

    char payload[256];

    serializeJson(doc, payload);

    Serial.println();
    Serial.println("Enviando MQTT...");
    Serial.println(payload);

    client.publish("teste", payload);

    Serial.println("Mensagem enviada para topico: teste");
  }

  Serial.println("========================");

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  delay(100);
}