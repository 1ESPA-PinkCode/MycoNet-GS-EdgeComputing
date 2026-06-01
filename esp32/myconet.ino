// Autoras:
// Lara Mofid Essa Alssabak - RM567947
// Maria Luisa Boucinhas Franco - RM567355
// Maria Luiza Kochnoff da Matta - RM568459
// Roberta Moreira dos Santos - RM567825
//
// ==========================================================
// MycoNet - Nó de Edge Computing
// Colônia Espacial Inteligente
//
// Responsabilidades deste módulo:
// - Coletar dados dos sensores
// - Detectar situações de risco localmente
// - Compartilhar estado com os demais módulos
// - Exibir informações no OLED
// - Publicar dados no FIWARE via MQTT
//
// A comunicação entre os módulos utiliza um sistema
// de heartbeat distribuído para garantir resiliência
// da rede mesmo sem um controlador central.
// ==========================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==========================================================
// IDENTIDADE DO MÓDULO
// ==========================================================
// Cada ESP32 representa um setor da colônia.
//
// Para criar outro módulo basta alterar estas duas
// constantes mantendo os demais parâmetros iguais.
//
//  HABITAT_A/myconetHabitatA001 | HABITAT_B/myconetHabitatB001
//  LAB/myconetLab001 | GARAGEM/myconetGaragem001 | ENERGIA/myconetEnergia001
// ==========================================================
#define MODULE_NAME  "HABITAT_A"
#define DEVICE_ID    "myconetHabitatA001"

// Lista completa da rede MycoNet.
// Todos os dispositivos precisam conhecer os demais
// para monitorar o estado da malha distribuída.
const char* ALL_MODULES[] = { "HABITAT_A", "HABITAT_B", "LAB", "GARAGEM", "ENERGIA" };
const int   NUM_MODULES   = 5;

// ==========================================================
// CONFIGURAÇÕES DE COMUNICAÇÃO
// ==========================================================
// Broker MQTT utilizado para comunicação entre:
// - ESP32 ↔ FIWARE
// - ESP32 ↔ ESP32 (heartbeat)
// ==========================================================
const char* SSID        = "Wokwi-GUEST";
const char* PASSWORD    = "";
const char* BROKER_MQTT = "35.188.129.19";
const int   BROKER_PORT = 1883;
const char* ID_MQTT     = DEVICE_ID;

String TOPICO_PUBLISH, TOPICO_SUBSCRIBE, TOPICO_CMDEXE;
const char* HB_TOPIC = "myconet/hb";

// ==========================================================
// MAPEAMENTO DE HARDWARE
// ==========================================================
// Define em quais portas do ESP32 cada sensor,
// atuador e periférico está conectado.
// ==========================================================
#define DHT_PIN 4
#define DHT_TYPE DHT22
#define LDR_PIN 34
#define MQ2_PIN 35
#define BUZZER_PIN 32
#define LED_ALERT_PIN 5
#define LED_OK_PIN 26

// --- OLED ---
#define SCREEN_W 128
#define SCREEN_H 64
#define OLED_ADDR 0x3C

// ==========================================================
// LIMITES DE SEGURANÇA
// ==========================================================
// Valores considerados aceitáveis para o ambiente.
// Quando algum parâmetro ultrapassa esses limites,
// o módulo entra em estado de alerta.
// ==========================================================
const float TEMP_MIN = 15.0, TEMP_MAX = 30.0;
const float HUM_MIN  = 20.0, HUM_MAX  = 70.0;
const int   LUZ_MAX  = 80;
const int   GAS_MAX  = 60;

// ==========================================================
// CONTROLE DE TEMPO
// ==========================================================
// Utilizados para evitar delays constantes e permitir
// múltiplas tarefas simultâneas no loop principal.
// ==========================================================
const unsigned long PUBLISH_INTERVAL   = 5000;
const unsigned long OLED_INTERVAL       = 250;
const unsigned long HEARTBEAT_INTERVAL = 1000;
const unsigned long FAILURE_TIMEOUT    = 3500;   // ~3 ciclos sem batida
const unsigned long FLASH_MS           = 3500;   // duracao do aviso de falha
const unsigned long PAGE_INTERVAL      = 4000;   // tempo de cada pagina no rodizio

DHT dht(DHT_PIN, DHT_TYPE);
WiFiClient   espClient;
PubSubClient MQTT(espClient);
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

bool remoteAlertOn = false, buzzerSilenced = false, ledBlinkState = false;
unsigned long lastPublish=0, lastBlink=0, lastOLED=0, lastHeartbeat=0, lastPageSwitch=0;
float lastTemp = NAN, lastHum = NAN;
int   curLuz = 0, curGas = 0;
bool  curDhtOk = false, curAlert = false;
int   page = 0;                       // 0 = metricas, 1 = estado da rede

// ==========================================================
// MONITORAMENTO DA REDE
// ==========================================================
// Cada módulo mantém uma visão local da rede,
// registrando a última vez que recebeu heartbeat
// dos demais participantes.
// ==========================================================
unsigned long lastSeen[NUM_MODULES] = {0};   // ultimo batimento de cada modulo
bool   peerAlert[NUM_MODULES] = {false};     // ultimo status recebido (alerta?)
bool   failureActive = false;
String failedModule = "";
int    failedCount = 0;
unsigned long alertFlashUntil = 0;           // mostra tela de aviso ate este instante

// ==========================================================
// ANIMAÇÃO DE INICIALIZAÇÃO
// ==========================================================
// Exibida durante o boot do dispositivo para indicar
// que todos os componentes estão sendo carregados.
// ==========================================================
void aberturaAnimada() {
  const int N = 32;
  int sx[N], sy[N], sv[N];
  for (int i=0;i<N;i++){ sx[i]=random(0,SCREEN_W); sy[i]=random(0,SCREEN_H); sv[i]=random(1,4); }
  for (int frame=0; frame<46; frame++) {
    display.clearDisplay();
    for (int i=0;i<N;i++){
      display.drawPixel(sx[i], sy[i], SSD1306_WHITE);
      if (sv[i]>=3) display.drawPixel(sx[i]+1, sy[i], SSD1306_WHITE);
      sx[i]-=sv[i];
      if (sx[i]<0){ sx[i]=SCREEN_W-1; sy[i]=random(0,SCREEN_H); sv[i]=random(1,4); }
    }
    if (frame>14){ display.setTextSize(2); display.setCursor(22,20); display.print("MycoNet"); display.setTextSize(1); }
    if (frame>30){ display.setCursor(34,44); display.print(MODULE_NAME); }
    display.display();
    delay(45);
  }
  delay(500);
}

// ==========================================================
// FUNÇÕES DE INICIALIZAÇÃO
// ==========================================================
// Responsáveis por configurar os recursos utilizados
// pelo sistema antes do início da operação.
// ==========================================================
void initTopics() {
  TOPICO_PUBLISH   = String("/TEF/") + DEVICE_ID + "/attrs";
  TOPICO_SUBSCRIBE = String("/TEF/") + DEVICE_ID + "/cmd";
  TOPICO_CMDEXE    = String("/TEF/") + DEVICE_ID + "/cmdexe";
}
void initOLED() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) { Serial.println("OLED nao encontrado!"); return; }
  display.cp437(true); display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
}
void initWiFi() { Serial.print("Conectando WiFi: "); Serial.println(SSID); reconectWiFi(); }
void initMQTT() { MQTT.setServer(BROKER_MQTT, BROKER_PORT); MQTT.setCallback(mqtt_callback); MQTT.setBufferSize(512); }
void initOutput() {
  pinMode(LED_ALERT_PIN, OUTPUT); pinMode(LED_OK_PIN, OUTPUT); pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_ALERT_PIN, LOW); digitalWrite(LED_OK_PIN, LOW);
}

// ==========================================================
// SETUP
// ==========================================================
// Executado apenas uma vez durante a inicialização.
//
// Ordem:
// 1. Inicializa hardware
// 2. Exibe animação
// 3. Conecta ao WiFi
// 4. Conecta ao broker MQTT
// 5. Publica status inicial
// ==========================================================
void setup() {
  Serial.begin(115200);
  initOutput(); initTopics(); initOLED();
  aberturaAnimada();
  dht.begin(); initWiFi(); initMQTT();
  delay(2000);
  MQTT.publish(TOPICO_PUBLISH.c_str(), "s|on");
}

// ==========================================================
// LOOP PRINCIPAL
// ==========================================================
// Mantém o funcionamento contínuo do módulo.
//
// Executa:
// - Verificação de conexões
// - Heartbeat da rede
// - Monitoramento dos vizinhos
// - Leitura dos sensores
// - Processamento MQTT
// ==========================================================
void loop() {
  verificaConexoes();
  enviaHeartbeat();
  checkPeers();
  handleSensors();
  MQTT.loop();
}

// ==========================================================
// RECUPERAÇÃO DE CONEXÕES
// ==========================================================
// Caso WiFi ou MQTT sejam perdidos,
// o módulo tenta se reconectar automaticamente.
// ==========================================================
void reconectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.begin(SSID, PASSWORD, 6);
  while (WiFi.status() != WL_CONNECTED) { delay(100); Serial.print("."); }
  Serial.print("\nWiFi OK. IP: "); Serial.println(WiFi.localIP());
}
void reconnectMQTT() {
  while (!MQTT.connected()) {
    Serial.print("Conectando ao broker: "); Serial.println(BROKER_MQTT);
    if (MQTT.connect(ID_MQTT)) {
      Serial.println("Broker conectado!");
      MQTT.subscribe(TOPICO_SUBSCRIBE.c_str());
      MQTT.subscribe(HB_TOPIC);
    } else { Serial.println("Falha. Nova tentativa em 2s."); delay(2000); }
  }
}
void verificaConexoes() { if (!MQTT.connected()) reconnectMQTT(); reconectWiFi(); }

// ==========================================================
// HEARTBEAT DISTRIBUÍDO
// ==========================================================
// Publica periodicamente o estado do módulo.
//
// Formato:
// HABITAT_A:ok
// HABITAT_A:alert
//
// Os demais nós utilizam esta informação para
// detectar falhas e recalcular rotas.
// ==========================================================
void enviaHeartbeat() {
  if (!MQTT.connected()) return;
  unsigned long now = millis();
  if (now - lastHeartbeat < HEARTBEAT_INTERVAL) return;
  lastHeartbeat = now;
  String hb = String(MODULE_NAME) + ":" + ((curAlert || remoteAlertOn) ? "alert" : "ok");
  MQTT.publish(HB_TOPIC, hb.c_str());
}

// ==========================================================
// DETECÇÃO DE FALHAS DE REDE
// ==========================================================
// Caso um módulo pare de enviar heartbeat por
// alguns segundos, ele é considerado offline.
//
// A falha é informada localmente através do
// display e do buzzer.
// ==========================================================
void checkPeers() {
  unsigned long now = millis();
  int count = 0; String firstFailed = "";
  for (int i=0;i<NUM_MODULES;i++) {
    if (strcmp(ALL_MODULES[i], MODULE_NAME)==0) continue;
    if (lastSeen[i] > 0 && (now - lastSeen[i] > FAILURE_TIMEOUT)) {
      count++; if (firstFailed=="") firstFailed = ALL_MODULES[i];
    }
  }
  bool nowFailure = (count > 0);
  if (nowFailure && !failureActive) {              // falha NOVA -> aviso + bip
    failedModule = firstFailed; failedCount = count;
    alertFlashUntil = now + FLASH_MS;
    if (!buzzerSilenced) for (int i=0;i<3;i++){ tone(BUZZER_PIN,1800,120); delay(160); }
  }
  if (nowFailure) { failedModule = firstFailed; failedCount = count; }
  failureActive = nowFailure;
}

int modulosAtivos() {
  int n = 1; unsigned long now = millis();
  for (int i=0;i<NUM_MODULES;i++) {
    if (strcmp(ALL_MODULES[i], MODULE_NAME)==0) continue;
    if (lastSeen[i] > 0 && (now - lastSeen[i] <= FAILURE_TIMEOUT)) n++;
  }
  return n;
}

// ==========================================================
// PROCESSAMENTO DE MENSAGENS MQTT
// ==========================================================
//
// Tipos de mensagens recebidas:
//
// 1. Heartbeats dos demais módulos
// 2. Comandos enviados pelo FIWARE
//
// Exemplos:
// @alert_on
// @alert_off
// @silence_buzzer
// ==========================================================
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i=0;i<length;i++) msg += (char)payload[i];

  if (String(topic) == HB_TOPIC) {                 // "NOME:status"
    int c = msg.indexOf(':');
    String name = (c >= 0) ? msg.substring(0, c) : msg;
    String st   = (c >= 0) ? msg.substring(c + 1) : "ok";
    for (int i=0;i<NUM_MODULES;i++)
      if (name == ALL_MODULES[i]) { lastSeen[i] = millis(); peerAlert[i] = (st == "alert"); break; }
    return;
  }

  Serial.print("Comando recebido: "); Serial.println(msg);
  if (msg.indexOf("@alert_on") >= 0) {
    remoteAlertOn = true; buzzerSilenced = false;
    MQTT.publish(TOPICO_CMDEXE.c_str(), "alert_on|OK");
  } else if (msg.indexOf("@alert_off") >= 0) {
    remoteAlertOn = false; buzzerSilenced = false;
    digitalWrite(LED_ALERT_PIN, LOW); noTone(BUZZER_PIN);
    MQTT.publish(TOPICO_CMDEXE.c_str(), "alert_off|OK");
  } else if (msg.indexOf("@silence_buzzer") >= 0) {
    buzzerSilenced = true; noTone(BUZZER_PIN);
    MQTT.publish(TOPICO_CMDEXE.c_str(), "silence_buzzer|OK");
  }
}

// ==========================================================
// PADRÕES DE ALERTA SONORO
// ==========================================================
// Cada tipo de risco possui um padrão diferente
// para facilitar a identificação pelo operador.
// ==========================================================
void alertTemperature(){ if(buzzerSilenced)return; for(int i=0;i<3;i++){tone(BUZZER_PIN,2000,150);delay(250);} }
void alertHumidity(){    if(buzzerSilenced)return; for(int i=0;i<2;i++){tone(BUZZER_PIN,800,400);delay(500);} }
void alertLuminosity(){  if(buzzerSilenced)return; tone(BUZZER_PIN,3500,120); delay(180); }
void alertGas(){         if(buzzerSilenced)return; for(int i=0;i<4;i++){tone(BUZZER_PIN,2500,100);delay(150);} }

// ==========================================================
// SINALIZAÇÃO VISUAL
// ==========================================================
// LED Verde  -> operação normal
// LED Vermelho -> alerta ativo
//
// Em situação crítica o LED vermelho pisca.
// ==========================================================
void handleLeds(bool shouldAlert) {
  unsigned long now = millis();
  if (shouldAlert) {
    digitalWrite(LED_OK_PIN, LOW);
    if (now - lastBlink >= 300) { lastBlink = now; ledBlinkState = !ledBlinkState; digitalWrite(LED_ALERT_PIN, ledBlinkState); }
  } else { digitalWrite(LED_ALERT_PIN, LOW); digitalWrite(LED_OK_PIN, HIGH); ledBlinkState = false; }
}

// ==========================================================
// INTERFACE OLED
// ==========================================================
// O display alterna entre:
//
// 1. Métricas ambientais
// 2. Estado geral da rede
//
// Em caso de falha de comunicação uma tela de
// alerta temporária tem prioridade.
// ==========================================================
void telaFalha() {
  display.clearDisplay(); display.setTextSize(1);
  display.setCursor(0,0); display.println("!! ALERTA DE REDE");
  display.drawLine(0,10,127,10,SSD1306_WHITE);
  display.setCursor(0,16); display.println("Modulo fora do ar:");
  display.setTextSize(2); display.setCursor(0,32); display.print(failedModule);
  display.setTextSize(1); display.setCursor(0,56);
  if (failedCount > 1) { display.print(failedCount); display.print(" nos / recalc rota"); }
  else                 { display.print("recalculando rota..."); }
  display.display();
}

void telaMetricas(float t, float h, bool alerta) {
  display.clearDisplay(); display.setTextSize(1);
  display.setCursor(0,0); display.print(MODULE_NAME); display.println(alerta ? " ALERTA" : " OK");
  display.drawLine(0,10,127,10,SSD1306_WHITE);
  display.setCursor(0,14);
  if (curDhtOk) {
    display.print("Temp: "); display.print(t,1); display.println(" C");
    display.print("Umid: "); display.print(h,1); display.println(" %");
  } else { display.println("DHT: sem leitura"); display.println(""); }
  display.print("Luz : "); display.print(curLuz); display.println(" %");
  display.print("Gas : "); display.print(curGas); display.println(" %");
  display.print("Rede: "); display.print(modulosAtivos()); display.print("/"); display.print(NUM_MODULES); display.print(" ativos");
  display.display();
}

void telaRede() {
  unsigned long now = millis();
  display.clearDisplay(); display.setTextSize(1);
  display.setCursor(0,0); display.println("ESTADO DA REDE");
  display.drawLine(0,10,127,10,SSD1306_WHITE);
  int y = 13;
  for (int i=0;i<NUM_MODULES;i++) {
    display.setCursor(0,y); display.print(ALL_MODULES[i]);
    display.setCursor(78,y);
    if (strcmp(ALL_MODULES[i], MODULE_NAME)==0)
      display.print((curAlert||remoteAlertOn) ? "ALERTA" : "OK");
    else if (lastSeen[i]==0 || (now - lastSeen[i] > FAILURE_TIMEOUT))
      display.print("OFF");
    else
      display.print(peerAlert[i] ? "ALERTA" : "OK");
    y += 10;
  }
  display.display();
}

// ==========================================================
// EDGE COMPUTING
// ==========================================================
// Núcleo de tomada de decisão do módulo.
//
// Responsabilidades:
// - Ler sensores
// - Detectar anomalias
// - Acionar LEDs e buzzer
// - Atualizar OLED
// - Gerar informações para o FIWARE
// ==========================================================
void handleSensors() {
  unsigned long now = millis();

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) lastTemp = t;
  if (!isnan(h)) lastHum  = h;
  curDhtOk = !isnan(lastTemp) && !isnan(lastHum);
  t = lastTemp; h = lastHum;

  curLuz = map(analogRead(LDR_PIN), 0, 4095, 0, 100);
  curGas = map(analogRead(MQ2_PIN), 0, 4095, 0, 100);

  bool tempAlert = curDhtOk && (t < TEMP_MIN || t > TEMP_MAX);
  bool humAlert  = curDhtOk && (h < HUM_MIN  || h > HUM_MAX);
  bool luxAlert  = (curLuz > LUZ_MAX);
  bool gasAlert  = (curGas > GAS_MAX);
  curAlert = tempAlert || humAlert || luxAlert || gasAlert;

  bool flashing = (now < alertFlashUntil);
  handleLeds(curAlert || remoteAlertOn || flashing);

  // OLED: exibe temporariamente alertas de falha de rede.
  // Em condições normais alterna entre:
  // 1. Métricas ambientais
  // 2. Estado geral da rede
  if (now - lastOLED >= OLED_INTERVAL) {
    lastOLED = now;
    if (flashing) {
      telaFalha();
    } else {
      if (now - lastPageSwitch >= PAGE_INTERVAL) { lastPageSwitch = now; page = (page + 1) % 2; }
      if (page == 0) telaMetricas(t, h, curAlert || remoteAlertOn);
      else           telaRede();
    }
  }

  if (now - lastPublish < PUBLISH_INTERVAL) return;
  lastPublish = now;

  if (tempAlert) alertTemperature();
  if (humAlert)  alertHumidity();
  if (luxAlert)  alertLuminosity();
  if (gasAlert)  alertGas();

  String environmentStatus = curAlert ? "alert" : "ok";
  int alertCount = (tempAlert?1:0)+(humAlert?1:0)+(luxAlert?1:0)+(gasAlert?1:0);
  String comfortLevel = (alertCount==0) ? "ideal" : (alertCount==1 ? "warning" : "critical");

  String decisionReason;
  if (!curAlert) decisionReason = "todos os parametros na faixa ideal";
  else {
    decisionReason = "";
    if (tempAlert) decisionReason += "temperatura fora (" + String(t,1) + "C); ";
    if (humAlert)  decisionReason += "umidade fora ("      + String(h,1) + "%); ";
    if (luxAlert)  decisionReason += "luminosidade alta ("  + String(curLuz) + "%); ";
    if (gasAlert)  decisionReason += "gas detectado ("       + String(curGas) + "%); ";
  }

  String alertType;
  if (alertCount==0)     alertType = "none";
  else if (alertCount>1) alertType = "multiple";
  else if (tempAlert)    alertType = "temperature";
  else if (humAlert)     alertType = "humidity";
  else if (luxAlert)     alertType = "luminosity";
  else                   alertType = "gas";

  String trig = "";
  if (tempAlert) trig += "t";
  if (humAlert)  trig += (trig.length()?"+h":"h");
  if (luxAlert)  trig += (trig.length()?"+l":"l");
  if (gasAlert)  trig += (trig.length()?"+q":"q");
  if (trig.length()==0) trig = "none";

  String alertStatus = (curAlert || remoteAlertOn) ? "active" : "inactive";
  String ledState    = (curAlert || remoteAlertOn || flashing) ? "on" : "off";
  String buzzerState = (curAlert && !buzzerSilenced) ? "on" : "off";

  String safeReason = decisionReason;
  safeReason.replace("|","/"); safeReason.replace("(","["); safeReason.replace(")","]");
  safeReason.replace(";",","); safeReason.replace("\"","'");
  safeReason.replace("<","");  safeReason.replace(">","");  safeReason.replace("=","-");

  // Monta o payload enviado ao FIWARE contendo:
  //
  // Temperatura
  // Umidade
  // Luminosidade
  // Nível de gás
  // Estado ambiental
  // Nível de conforto
  // Justificativa da decisão
  // Estado dos atuadores
  //
  // Dessa forma o Orion recebe tanto os dados
  // brutos quanto a interpretação feita na borda.
  String payload = "s|on";
  payload += "|t|" + String(t,1);
  payload += "|h|" + String(h,1);
  payload += "|l|" + String(curLuz);
  payload += "|q|" + String(curGas);
  payload += "|e|" + environmentStatus;
  payload += "|c|" + comfortLevel;
  payload += "|r|" + safeReason;
  payload += "|a|" + alertStatus;
  payload += "|y|" + alertType;
  payload += "|g|" + trig;
  payload += "|b|" + ledState;
  payload += "|z|" + buzzerState;

  // Envia os dados processados para o FIWARE.
  // Caso o envio falhe, registra o erro na serial
  // para facilitar a depuração.

  if (!MQTT.publish(TOPICO_PUBLISH.c_str(), payload.c_str()))
    Serial.println("[ERRO] publish falhou (payload grande ou broker off)");

  Serial.printf("[%s] T=%.1f H=%.1f Luz=%d%% Gas=%d%% Rede=%d/%d -> %s\n",
                MODULE_NAME, t, h, curLuz, curGas, modulosAtivos(), NUM_MODULES,
                curAlert ? "ALERTA" : "OK");
}
