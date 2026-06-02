# MycoNet

**Rede de comunicação distribuída bioinspirada para colônias espaciais**
Global Solution 2026 · FIAP · Engenharia de Software · Equipe **PINK CODE**

> O código que mantém a colônia viva quando nenhum engenheiro consegue intervir a tempo.

---

## Sobre o projeto

O MycoNet é uma plataforma de telemetria e comunicação distribuída para os módulos de uma colônia espacial (ou para a cápsula Dragon, no recorte da Global Solution). Cada módulo monitora seu ambiente, transmite os dados por MQTT e — inspirado nas redes de micélio dos fungos — **detecta sozinho quando um módulo vizinho cai, avisa os demais e se recupera**, sem intervenção humana.

A inspiração vem da natureza: redes de micélio conectam florestas inteiras sem nenhum ponto central de controle e se reorganizam quando parte da rede é danificada. O MycoNet traduz esse comportamento para engenharia de software aplicada a ambientes onde o delay de comunicação com a Terra (4 a 24 minutos) torna o controle centralizado inviável.

Este repositório contém a camada de **Edge Computing** e a **integração com o FIWARE**: o firmware dos módulos ESP32, o back-end que serve os dados e o dashboard de controle em terra.

---

## Arquitetura



A solução é organizada em três camadas:

- **Application** — Dashboard Web (Flask + HTML/Chart.js), Postman e a equipe em terra.
- **Back-end (VM Google Cloud / Docker)** — FIWARE: Orion Context Broker, IoT Agent UltraLight, STH-Comet, Mosquitto e MongoDB, além do back-end Flask (`app.py`) que serve os dados ao dashboard.
- **IoT** — 5 módulos ESP32 (Habitat A, Habitat B, Laboratório, Garagem, Energia), cada um com seus sensores e atuadores, interligados por um **heartbeat MQTT** que dá a resiliência da rede.

O transporte é em topologia estrela (cada módulo fala com o broker, como no modelo Starlink), enquanto a "malha de micélio" é uma camada lógica: os módulos se monitoram pelo tópico compartilhado `myconet/hb`.

---

## Parâmetros monitorados

| Parâmetro | Sensor | Atributo FIWARE |
|---|---|---|
| Temperatura | DHT22 | `temperature` |
| Umidade | DHT22 | `humidity` |
| Luminosidade | Módulo LDR | `luminosity` |
| Gás / fumaça | Sensor MQ-2 | `gasLevel` |
| Estado do ambiente | (derivado) | `environmentStatus`, `alertStatus`, `triggerViolated` |

Quando qualquer parâmetro sai da faixa nominal, o módulo dispara **alerta visual (LED vermelho) e sonoro (buzzer)** localmente e publica o estado para a equipe em terra.

---

## Hardware (por módulo)

ESP32 DevKit simulado no Wokwi, com:

| Componente | Função | Pino |
|---|---|---|
| DHT22 | Temperatura e umidade | GPIO 4 |
| Módulo LDR | Luminosidade (analógico) | GPIO 34 |
| Sensor MQ-2 | Gás / fumaça (analógico) | GPIO 35 |
| OLED SSD1306 | Painel local (I2C) | SDA 21 / SCL 22 |
| LED verde | Status nominal | GPIO 26 |
| LED vermelho | Alerta | GPIO 5 |
| Buzzer | Alerta sonoro | GPIO 32 |

> Os sensores analógicos ficam em pinos ADC1 (32–39) para continuarem funcionando com o Wi-Fi ligado. O DHT22 **nunca** deve usar pinos input-only (34/35/36/39).

### Circuito



> Montagem de um módulo no simulador Wokwi (ESP32 + DHT22 + LDR + MQ-2 + OLED + buzzer + LEDs).

---

## Como funciona

1. Cada **ESP32** lê seus sensores e publica a telemetria via **MQTT UltraLight** a cada 5 s no tópico `/TEF/<device>/attrs`.
2. O **Mosquitto** entrega ao **IoT Agent**, que traduz para NGSI e atualiza o **Orion Context Broker** (estado atual). O **STH-Comet** persiste o histórico.
3. O back-end **Flask (`app.py`)** consulta o Orion e o STH-Comet e expõe uma API limpa para o dashboard.
4. O **dashboard** mostra, em tempo real, o mapa da rede de micélio, a telemetria dos 5 módulos, os gráficos históricos e os alertas.

### Resiliência (heartbeat)

Cada módulo publica um "estou vivo" em `myconet/hb` a cada 1 s e escuta os batimentos dos outros. Se um módulo para de bater por ~3 s (3 ciclos), os vizinhos detectam sozinhos, exibem o aviso de falha no OLED e seguem operando. Quando o módulo volta, é reincorporado automaticamente. Na camada de terra, o dashboard detecta a queda pelo timestamp do Orion (`dateModified`) que congela.

---

## Tecnologias

- **Edge:** ESP32, C/C++ (Arduino), MQTT (PubSubClient), sensores DHT22 / LDR / MQ-2, simulação no Wokwi
- **Back-end:** FIWARE (Orion, IoT Agent UltraLight, STH-Comet), Mosquitto, MongoDB, Docker, Python (Flask)
- **Front-end:** HTML, CSS, JavaScript
- **Infra:** Google Cloud (VM), Ubuntu

---

## Como executar

### 1. Módulos (Wokwi)

Abra o `myconet.ino` no Wokwi. Para cada módulo, altere apenas as duas linhas do topo:

```cpp
#define MODULE_NAME  "HABITAT_A"
#define DEVICE_ID    "myconetHabitatA001"
```

Módulos: `HABITAT_A`, `HABITAT_B`, `LAB`, `GARAGEM`, `ENERGIA`. Ajuste o `BROKER_MQTT` para o IP da sua VM. Rode cada módulo em uma janela separada (janelas lado a lado, não abas em segundo plano).

### 2. FIWARE (VM)

Com a stack FIWARE rodando via Docker na VM (Orion 1026, IoT Agent 4041, STH-Comet 8666, Mosquitto 1883), libere essas portas no firewall do Google Cloud.

### 3. Back-end (Flask)

Na VM:

```bash
python3 -m venv venv && source venv/bin/activate
pip install flask flask-cors requests
python app.py
```

A API sobe em `http://<IP>:5000` com os endpoints `/api/modules` e `/api/history/<entidade>/<attr>`.

### 4. Dashboard

No topo do `myconet_dashboard.html`, configure:

```js
const USE_BACKEND = true;
const API = 'http://<IP_DA_VM>:5000';
```

Abra o arquivo no navegador. O selo no topo indica o modo (`online · fiware` ou `simulacao`).

> Para apresentações offline, defina `USE_BACKEND = false`: o dashboard roda 100% com dados simulados, com a mesma interface.

---

## Demonstração

1. Abra 3+ módulos no Wokwi — todos em verde, telemetria subindo.
2. Force uma anomalia (arraste o slider do gás acima do limiar) → LED vermelho + buzzer + alerta no dashboard.
3. Pare um módulo (o "micro impacto") → os vizinhos detectam pelo heartbeat e o dashboard marca o nó como offline, recalculando a rota.
4. Rode o módulo de novo → reconexão e reincorporação automáticas.

---

## Equipe — PINK CODE

| Nome | RM |
|---|---|
| Lara Mofid Essa Alssabak | 567947 |
| Maria Luisa Boucinhas Franco | 567355 |
| Maria Luiza Kochnoff da Matta | 568459 |
| Roberta Moreira dos Santos | 567825 |

Projeto desenvolvido para a Global Solution 2026 — FIAP.