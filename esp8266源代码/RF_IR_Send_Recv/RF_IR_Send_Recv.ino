#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Arduino.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <RCSwitch.h>
#include <Wire.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRutils.h>
#include "ClosedCube_SHT31D.h"
#include <OneButton.h>  // 引入OneButton库


// 智能网关v1.0.1
// 作者:lzq-hopego
// 最后保存时间:2025年11月12日 21点33分
// 关于代码版权问题，再次开源时需标注原作者，不得商业化，除此之外还需遵守MIT协议


// 配置存储地址和大小
#define EEPROM_SIZE 512
#define CONFIG_ADDR 0          
#define WIFI_ADDR CONFIG_ADDR
#define MQTT_ADDR sizeof(ConfigWiFi)

// 引脚定义（根据硬件调整）
#define BUTTON_PIN 12    // 按钮引脚（一端接IO12，另一端接地）
#define INDICATOR_PIN 15 // 指示灯引脚（高电平点亮）
// 433/315
const int rcsPin = 5; //433发射模块
const int rcrPin = 4; //433接收模块
// 红外接收配置参数
const uint16_t kRecvPin = 0;        // 红外接收引脚（如D3）
const uint32_t kBaudRate = 115200;  // 串口波特率
const uint16_t kCaptureBufferSize = 1024;
const uint8_t kTimeout = 50;        // 信号超时时间（毫秒）
// 红外发射配置参数
const uint16_t kIrLed = D0;

// 全局对象（按钮初始化修改）
RCSwitch mySwitchSend = RCSwitch();
RCSwitch mySwitchRecv = RCSwitch();
ClosedCube_SHT31D sht3xd;
IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
decode_results results;
IRsend irsend(kIrLed);
// 按钮初始化：禁用内部上拉（因为按钮接GND，按下时为低电平）
OneButton button(BUTTON_PIN);  // 第二个参数改为false（禁用内部上拉）
// 板载LED配置（IO2，低电平点亮）
#define LED_PIN 2
bool ledState = false;  // 记录LED当前状态：false=灭，true=亮

// 配置结构体（保持不变）
struct ConfigWiFi {
  char ssid[32];
  char password[32];
};
struct ConfigMQTT {
  char server[40];
  char port[6];
  char user[32];
  char pass[32];
  char topic[64];
};

// 全局变量（保持不变）
ConfigWiFi configWiFi;
ConfigMQTT configMQTT;
bool mqttUnconfigured = false;  // 控制MQTT未配置提示只显示一次
ESP8266WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
int reconnectAttempts = 0;
int mqttReconnectAttempts = 0;
String clientId;  // 保存唯一客户端ID用于过滤消息
// AP模式关键参数
const char* apPassword = "12345678";
const int apChannel = 6;
const bool apHidden = false;
const int apMaxConn = 3;
// HTML页面内容（保持不变）
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>设备配置</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { 
            font-family: 'Arial', sans-serif; 
            max-width: 500px; 
            margin: 0 auto; 
            padding: 20px; 
            background: #f0f4f8; 
            color: #3a4a5c;
        }
        .container { 
            background: white; 
            padding: 30px; 
            border-radius: 12px; 
            box-shadow: 0 4px 15px rgba(100, 120, 180, 0.1);
        }
        h1 { 
            color: #5c6bc0; 
            margin-bottom: 25px; 
            text-align: center;
            font-weight: 600;
        }
        .form-group { 
            margin-bottom: 20px; 
        }
        label { 
            display: block; 
            margin-bottom: 8px; 
            font-weight: 500;
            color: #4a5ca0;
        }
        input, select { 
            width: 100%; 
            padding: 12px; 
            border: 1px solid #bdd0e0; 
            border-radius: 6px; 
            font-size: 16px;
            transition: border 0.3s;
        }
        input:focus, select:focus { 
            border-color: #7986cb; 
            outline: none; 
            box-shadow: 0 0 0 2px rgba(100, 120, 180, 0.1);
        }
        button { 
            width: 100%; 
            padding: 14px; 
            background: #64b5f6; 
            color: white; 
            border: none; 
            border-radius: 6px; 
            font-size: 16px; 
            font-weight: 600; 
            cursor: pointer;
            transition: background 0.3s;
            margin-bottom: 15px;
        }
        button:hover { 
            background: #42a5f5; 
        }
        .scan-btn {
            background: #90caf9;
            margin-bottom: 10px;
        }
        .scan-btn:hover {
            background: #64b5f6;
        }
        .status {
            margin-top: 20px;
            padding: 15px;
            border-radius: 6px;
            display: none;
            text-align: center;
        }
        .success {
            background: #e8f5e9;
            color: #2e7d32;
            display: block;
        }
        .error {
            background: #ffebee;
            color: #c62828;
            display: block;
        }
        .tab-content {
            display: none;
        }
        .tab-content.active {
            display: block;
        }
        .nav-tabs {
            display: flex;
            margin-bottom: 25px;
            border-bottom: 1px solid #d1d9e6;
        }
        .nav-tab {
            padding: 10px 15px;
            cursor: pointer;
            color: #7986cb;
            border-bottom: 3px solid transparent;
            transition: all 0.3s;
        }
        .nav-tab.active {
            color: #5c6bc0;
            border-bottom: 3px solid #5c6bc0;
        }
        .nav-tab:hover {
            color: #5c6bc0;
        }
        .loading {
            text-align: center;
            color: #5c6bc0;
            padding: 10px;
            display: none;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>设备配置</h1>
        <div class="nav-tabs">
            <div class="nav-tab active" onclick="showTab('wifi-tab')">WiFi 配置</div>
            <div class="nav-tab" onclick="showTab('mqtt-tab')">MQTT 设置</div>
        </div>
        <div id="wifi-tab" class="tab-content active">
            <div class="form-group">
                <label for="ssid">WiFi名称</label>
                <select id="ssid" required>
                    <option value="">请选择WiFi</option>
                </select>
            </div>
            <button class="scan-btn" onclick="scanWiFi()">扫描附近WiFi</button>
            <div class="loading" id="wifi-loading">正在扫描WiFi...</div>
            <div class="form-group">
                <label for="wifi_password">WiFi密码</label>
                <input type="password" id="wifi_password" placeholder="请输入WiFi密码">
            </div>
            <button onclick="saveWiFiConfig()">保存WiFi配置</button>
        </div>
        <div id="mqtt-tab" class="tab-content">
            <div class="form-group">
                <label for="mqtt_server">服务器地址</label>
                <input type="text" id="mqtt_server" placeholder="MQTT服务器IP或域名" required>
            </div>
            <div class="form-group">
                <label for="mqtt_port">服务器端口</label>
                <input type="number" id="mqtt_port" value="1883" required>
            </div>
            <div class="form-group">
                <label for="mqtt_user">用户名</label>
                <input type="text" id="mqtt_user" placeholder="MQTT用户名">
            </div>
            <div class="form-group">
                <label for="mqtt_pass">密码</label>
                <input type="password" id="mqtt_pass" placeholder="MQTT密码">
            </div>
            <div class="form-group">
                <label for="mqtt_topic">主题</label>
                <input type="text" id="mqtt_topic" placeholder="MQTT主题" required>
            </div>
            <button onclick="saveMQTTConfig()">保存MQTT配置</button>
        </div>
        <div id="status" class="status"></div>
    </div>
    <script>
        function showTab(tabId) {
            document.querySelectorAll('.tab-content').forEach(tab => tab.classList.remove('active'));
            document.querySelectorAll('.nav-tab').forEach(nav => nav.classList.remove('active'));
            document.getElementById(tabId).classList.add('active');
            document.querySelector(`.nav-tab[onclick="showTab('${tabId}')"]`).classList.add('active');
        }
        function scanWiFi() {
          document.getElementById('wifi-loading').style.display = 'block';
          fetch('/scanWiFi')
            .then(response => {
              if (!response.ok) {
                throw new Error(`HTTP错误：${response.status}`);
              }
              return response.json();
            })
            .then(networks => {
              const ssidSelect = document.getElementById('ssid');
              while (ssidSelect.options.length > 1) {
                ssidSelect.remove(1);
              }
              if (networks.length === 0) {
                showStatus('未发现可用WiFi网络', 'error');
                return;
              }
              networks.sort((a, b) => b.rssi - a.rssi);
              networks.forEach(network => {
                const option = document.createElement('option');
                option.value = network.ssid;
                const signal = Math.min(100, Math.max(0, (network.rssi + 100) * 2));
                option.textContent = `${network.ssid} (信号: ${signal}%)`;
                ssidSelect.appendChild(option);
              });
              showStatus(`成功扫描到 ${networks.length} 个WiFi网络`, 'success');
            })
            .catch(error => {
              showStatus(`WiFi扫描失败：${error.message}`, 'error');
              console.error('扫描错误详情：', error);
            })
            .finally(() => {
              document.getElementById('wifi-loading').style.display = 'none';
            });
        }
        function saveWiFiConfig() {
            const config = {
                ssid: document.getElementById('ssid').value,
                password: document.getElementById('wifi_password').value
            };
            fetch('/saveWiFiConfig', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(config)
            })
            .then(response => response.text())
            .then(data => {
                showStatus(data, data.includes('成功') ? 'success' : 'error');
                if (data.includes('成功')) {
                    setTimeout(() => window.location.href = '/', 2000);
                }
            })
            .catch(error => showStatus('WiFi配置保存失败', 'error'));
        }
        function saveMQTTConfig() {
            const config = {
                server: document.getElementById('mqtt_server').value,
                port: document.getElementById('mqtt_port').value,
                user: document.getElementById('mqtt_user').value,
                pass: document.getElementById('mqtt_pass').value,
                topic: document.getElementById('mqtt_topic').value
            };
            fetch('/saveMQTTConfig', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(config)
            })
            .then(response => response.text())
            .then(data => {
                showStatus(data, data.includes('成功') ? 'success' : 'error');
                if (data.includes('成功')) {
                    setTimeout(() => window.location.href = '/', 2000);
                }
            })
            .catch(error => showStatus('MQTT配置保存失败', 'error'));
        }
        function showStatus(message, type) {
            const statusEl = document.getElementById('status');
            statusEl.textContent = message;
            statusEl.className = `status ${type}`;
        }
        window.onload = function() {
            fetch('/getWiFiConfig')
                .then(response => response.json())
                .then(config => {
                    if (config.ssid) {
                        const ssidSelect = document.getElementById('ssid');
                        let hasOption = false;
                        for (let i = 0; i < ssidSelect.options.length; i++) {
                            if (ssidSelect.options[i].value === config.ssid) {
                                ssidSelect.selectedIndex = i;
                                hasOption = true;
                                break;
                            }
                        }
                        if (!hasOption) {
                            const option = document.createElement('option');
                            option.value = config.ssid;
                            option.textContent = config.ssid;
                            option.selected = true;
                            ssidSelect.appendChild(option);
                        }
                    }
                });
            fetch('/getMQTTConfig')
                .then(response => response.json())
                .then(config => {
                    if (config.server) document.getElementById('mqtt_server').value = config.server;
                    if (config.port) document.getElementById('mqtt_port').value = config.port;
                    if (config.user) document.getElementById('mqtt_user').value = config.user;
                    if (config.topic) document.getElementById('mqtt_topic').value = config.topic;
                });
        }
    </script>
</body>
</html>
)rawliteral";

// 指示灯控制函数（高电平点亮）
#line 386 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
void publishWithClientId(const char* content);
#line 396 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
bool connectMQTT();
#line 441 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
void onButtonClick();
#line 449 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
void onButtonLongPress();
#line 470 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
void mqttCallback(char* topic, byte* payload, unsigned int length);
#line 603 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
void initEEPROM();
#line 622 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
void saveWiFiToEEPROM();
#line 628 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
void saveMQTTToEEPROM();
#line 634 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
void clearWiFiConfig();
#line 642 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
void startAP();
#line 652 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
bool connectWiFi();
#line 693 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
void scanWiFiNetworks();
#line 722 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
void handleRoot();
#line 723 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
void handleGetWiFiConfig();
#line 730 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
void handleGetMQTTConfig();
#line 740 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
void handleSaveWiFiConfig();
#line 758 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
void handleSaveMQTTConfig();
#line 782 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
void setup();
#line 833 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
void loop();
#line 376 "E:\\Arduino_program\\IRsendDemo\\IRsendDemo.ino"
void indicatorLight(int times = 1, int duration = 100) {
  for (int i = 0; i < times; i++) {
    digitalWrite(INDICATOR_PIN, HIGH);  // 高电平点亮指示灯（适配你的硬件）
    delay(duration);
    digitalWrite(INDICATOR_PIN, LOW);   // 低电平熄灭
    if (i < times - 1) delay(duration); // 多次闪烁时的间隔
  }
}

// 新增：带客户端ID的消息发布函数
void publishWithClientId(const char* content) {
  StaticJsonDocument<256> doc;
  doc["client_id"] = clientId;  // 附加客户端ID
  doc["content"] = content;     // 消息内容
  String json;
  serializeJson(doc, json);
  mqttClient.publish(configMQTT.topic, json.c_str());
}

// MQTT连接函数（修改：初始化客户端ID）
bool connectMQTT() {
  if (strlen(configMQTT.server) == 0) {
    if (!mqttUnconfigured) {
      Serial.println("MQTT服务器未配置，跳过连接");
      mqttUnconfigured = true;  // 已提示过，后续不再提示
    }
    return false;
  }
  // 初始化唯一客户端ID（基于芯片ID）
  clientId = "ESP8266-" + String(ESP.getChipId(), HEX);
  mqttClient.setServer(configMQTT.server, atoi(configMQTT.port));
  Serial.print("连接MQTT服务器: ");
  Serial.print(configMQTT.server);
  Serial.print(":");
  Serial.println(configMQTT.port);
  
  if (mqttClient.connect(
    clientId.c_str(), 
    configMQTT.user,
    configMQTT.pass,
    configMQTT.topic,
    0, 1,
    "设备离线"
  )) {
    Serial.println("MQTT连接成功");
    Serial.print("客户端ID: ");
    Serial.println(clientId);
    mqttReconnectAttempts = 0;
    if (mqttClient.subscribe(configMQTT.topic)) {
      Serial.print("已订阅主题: ");
      Serial.println(configMQTT.topic);
    } else {
      Serial.print("订阅主题失败: ");
      Serial.println(configMQTT.topic);
    }
    return true;
  } else {
    Serial.print("MQTT连接失败，错误代码: ");
    Serial.println(mqttClient.state());
    mqttReconnectAttempts++;
    return false;
  }
}

// 按钮单击事件：重启设备（保持逻辑不变）
void onButtonClick() {
  Serial.println("检测到按钮单击，准备重启设备...");
  indicatorLight(2);  // 闪烁2次表示即将重启
  delay(1000);
  ESP.restart();
}

// 按钮长按事件：清空配置并重新配网（保持逻辑不变）
void onButtonLongPress() {
  Serial.println("检测到按钮长按，准备清空配置...");
  // 清空WiFi配置
  strcpy(configWiFi.ssid, "");
  strcpy(configWiFi.password, "");
  saveWiFiToEEPROM();
  // 清空MQTT配置
  strcpy(configMQTT.server, "");
  strcpy(configMQTT.port, "1883");
  strcpy(configMQTT.user, "");
  strcpy(configMQTT.pass, "");
  strcpy(configMQTT.topic, "");
  saveMQTTToEEPROM();
  
  indicatorLight(3, 300);  // 长亮3次表示配置已清空
  Serial.println("配置已清空，重启进入配网模式");
  delay(1000);
  ESP.restart();
}

// MQTT消息回调函数
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("收到主题消息 [");
  Serial.print(topic);
  Serial.print("]: ");
  
  // 读取消息内容
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  // 解析JSON消息
  StaticJsonDocument<128> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (error) {
    Serial.print("JSON解析失败: ");
    Serial.println(error.c_str());
    return;
  }

  // 忽略自身发送的消息
  if (doc.containsKey("client_id") && doc["client_id"].as<String>() == clientId) {
    Serial.println("忽略自身发送的消息");
    return;
  }

  // 处理状态查询指令：{"get":"state"}
  if (doc.containsKey("get") && doc["get"] == "state") {
    StaticJsonDocument<64> stateDoc;
    stateDoc["client_id"] = clientId;  // 附加客户端ID
    stateDoc["light"] = ledState ? "on" : "off";
    SHT31D sht_result=sht3xd.periodicFetchData();
    if (sht_result.error == SHT3XD_NO_ERROR) {
      stateDoc["tem"]=String(sht_result.t, 2);
      stateDoc["hum"]=String(sht_result.rh, 2);
    }else{
      Serial.print("SHT30: 读取失败，错误代码=");
      Serial.println(sht_result.error);
    }
    String stateJson;
    serializeJson(stateDoc, stateJson);
    
    if (mqttClient.publish(configMQTT.topic, stateJson.c_str())) {
      Serial.print("已发布LED状态: ");
      Serial.println(stateJson);
    } else {
      Serial.println("LED状态发布失败");
    }
    return;
  }

  // 处理LED控制指令：{"light":"on"} 或 {"light":"off"}
  if (doc.containsKey("light")) {
    String lightCmd = doc["light"].as<String>();
       
    if (lightCmd == "on") {
      digitalWrite(LED_PIN, LOW);  // 板载LED低电平点亮
      ledState = true;
      Serial.println("LED已开启");
      publishWithClientId("LED已开启");
    } else if (lightCmd == "off") {
      digitalWrite(LED_PIN, HIGH); // 高电平熄灭
      ledState = false;
      Serial.println("LED已关闭");
      publishWithClientId("LED已关闭");
    } else {
      Serial.print("无效灯光指令: ");
      Serial.println(lightCmd);
    }
  }
  
  // 处理433发送指令
  if (doc.containsKey("btn-paser-door") && doc["btn-paser-door"] == "tap") {
      mySwitchSend.send(6717700, 24);
      indicatorLight();  // 433发送成功，指示灯闪一下
  }
  if (doc.containsKey("rf_send")) {
    long code = doc["rf_send"].as<long>();  // 解析要发送的射频代码
    mySwitchSend.send(code, 24);  // 发送24位数据
    Serial.print("已发送射频代码: ");
    Serial.println(code);
    publishWithClientId("射频代码发送成功");
    indicatorLight();  // 433发送成功，指示灯闪一下
  }
  
  // 处理红外发送指令
  if (doc.containsKey("ir_send")) {
    // 获取字符串形式的红外原始数据
    String irDataStr = doc["ir_send"].as<String>();
    Serial.print("收到红外发送指令: ");
    Serial.println(irDataStr);

    // 1. 计算数据长度（按逗号分割的数量）
    int dataCount = 1; // 至少有一个数据
    for (char c : irDataStr) {
      if (c == ',') dataCount++;
    }

    // 2. 分配内存存储转换后的uint16_t数据
    uint16_t* rawData = new uint16_t[dataCount];
    if (rawData == nullptr) {
      Serial.println("内存分配失败，无法发送红外数据");
      return;
    }

    // 3. 解析字符串并转换为uint16_t数组
    int index = 0;
    char* token = strtok((char*)irDataStr.c_str(), ", "); // 按逗号和空格分割
    while (token != nullptr && index < dataCount) {
      rawData[index] = atoi(token); // 字符串转整数
      index++;
      token = strtok(nullptr, ", ");
    }

    // 4. 发送红外数据
    if (index > 0) {
      irsend.sendRaw(rawData, index, 38); // 38kHz载波频率
      Serial.print("已发送红外数据，长度: ");
      Serial.println(index);
      publishWithClientId("红外数据发送成功");
    } else {
      Serial.println("未解析到有效红外数据");
      publishWithClientId("红外数据解析失败");
    }

    // 5. 释放动态分配的内存
    delete[] rawData;
  }
}

// 初始化EEPROM
void initEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(WIFI_ADDR, configWiFi);
  EEPROM.get(MQTT_ADDR, configMQTT);
  
  if (strlen(configWiFi.ssid) == 0) {
    strcpy(configWiFi.ssid, "");
    strcpy(configWiFi.password, "");
  }
  if (strlen(configMQTT.server) == 0) {
    strcpy(configMQTT.server, "");
    strcpy(configMQTT.port, "1883");
    strcpy(configMQTT.user, "");
    strcpy(configMQTT.pass, "");
    strcpy(configMQTT.topic, "");
  }
}

// 保存WiFi配置到EEPROM
void saveWiFiToEEPROM() {
  EEPROM.put(WIFI_ADDR, configWiFi);
  EEPROM.commit();
}

// 保存MQTT配置到EEPROM
void saveMQTTToEEPROM() {
  EEPROM.put(MQTT_ADDR, configMQTT);
  EEPROM.commit();
}

// 清除WiFi配置
void clearWiFiConfig() {
  strcpy(configWiFi.ssid, "");
  strcpy(configWiFi.password, "");
  saveWiFiToEEPROM();
  Serial.println("WiFi配置已清除，重启进入配网模式");
}

// 启动AP模式
void startAP() {
  String apName = "ESP8266-" + String(ESP.getChipId(), HEX);
  WiFi.softAP(apName.c_str(), apPassword, apChannel, apHidden, apMaxConn);
  Serial.print("AP名称: ");
  Serial.println(apName);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

// 连接WiFi函数（新增关闭AP逻辑）
bool connectWiFi() {
  if (strlen(configWiFi.ssid) == 0) return false;
  
  WiFi.begin(configWiFi.ssid, configWiFi.password);
  Serial.print("连接WiFi: ");
  Serial.println(configWiFi.ssid);
  
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    Serial.print(".");
    timeout++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi连接成功");
    Serial.print("IP地址: ");
    Serial.println(WiFi.localIP());
    
    // 关键修改：如果AP模式开启，则关闭它
    if (WiFi.getMode() & WIFI_AP) {
      Serial.println("检测到AP模式已开启，正在关闭...");
      WiFi.softAPdisconnect(true);  // 关闭AP并禁用
      if (!(WiFi.getMode() & WIFI_AP)) {
        Serial.println("AP模式已成功关闭");
      } else {
        Serial.println("关闭AP模式失败");
      }
    }
    
    reconnectAttempts = 0;
    indicatorLight(3);  // 联网成功，指示灯短亮3次
    return true;
  } else {
    Serial.println("\nWiFi连接失败");
    reconnectAttempts++;
    return false;
  }
}

// 扫描WiFi网络
void scanWiFiNetworks() {
  int n = WiFi.scanNetworks(false, true);
  Serial.print("扫描到 ");
  Serial.print(n);
  Serial.println(" 个WiFi网络");
  
  StaticJsonDocument<1024> doc;
  JsonArray networks = doc.to<JsonArray>();
  
  if (n > 0) {
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;
      
      JsonObject network = networks.createNestedObject();
      network["ssid"] = ssid;
      network["rssi"] = WiFi.RSSI(i);
      delay(10);
    }
  }
  
  String json;
  serializeJson(doc, json);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
  WiFi.scanDelete();
}

// Web服务器回调函数
void handleRoot() { server.send_P(200, "text/html", index_html); }
void handleGetWiFiConfig() {
  StaticJsonDocument<256> doc;
  doc["ssid"] = configWiFi.ssid;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}
void handleGetMQTTConfig() {
  StaticJsonDocument<512> doc;
  doc["server"] = configMQTT.server;
  doc["port"] = configMQTT.port;
  doc["user"] = configMQTT.user;
  doc["topic"] = configMQTT.topic;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}
void handleSaveWiFiConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "无效请求");
    return;
  }
  String json = server.arg("plain");
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, json)) {
    server.send(400, "text/plain", "解析失败");
    return;
  }
  strncpy(configWiFi.ssid, doc["ssid"] | "", sizeof(configWiFi.ssid)-1);
  strncpy(configWiFi.password, doc["password"] | "", sizeof(configWiFi.password)-1);
  saveWiFiToEEPROM();
  server.send(200, "text/plain", "WiFi配置成功");
  delay(1000);
  connectWiFi();
}
void handleSaveMQTTConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "无效请求");
    return;
  }
  String json = server.arg("plain");
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, json)) {
    server.send(400, "text/plain", "解析失败");
    return;
  }
  strncpy(configMQTT.server, doc["server"] | "", sizeof(configMQTT.server)-1);
  strncpy(configMQTT.port, doc["port"] | "1883", sizeof(configMQTT.port)-1);
  strncpy(configMQTT.user, doc["user"] | "", sizeof(configMQTT.user)-1);
  strncpy(configMQTT.pass, doc["pass"] | "", sizeof(configMQTT.pass)-1);
  strncpy(configMQTT.topic, doc["topic"] | "", sizeof(configMQTT.topic)-1);
  saveMQTTToEEPROM();
  
  if (WiFi.status() == WL_CONNECTED) {
    connectMQTT();
  }
  server.send(200, "text/plain", "MQTT配置保存成功");
}

void setup() {
  Serial.begin(115200);
  
  // 初始化指示灯引脚（默认低电平熄灭）
  pinMode(INDICATOR_PIN, OUTPUT);
  digitalWrite(INDICATOR_PIN, LOW);  // 初始状态熄灭
  
  // 初始化按钮
  button.attachClick(onButtonClick);                // 单击事件
  button.attachLongPressStart(onButtonLongPress);   // 长按事件（2秒触发，库默认）
  
  Wire.begin(13, 14); // 初始化IIC
  pinMode(LED_PIN, OUTPUT);       // 初始化LED引脚为输出
  digitalWrite(LED_PIN, HIGH);    // 初始状态：LED熄灭（高电平）
  initEEPROM();

  // 初始化传感器
  sht3xd.begin(0x44);
  Serial.print("SHT30传感器序列号: ");
  Serial.println(sht3xd.readSerialNumber());
  if (sht3xd.periodicStart(SHT3XD_REPEATABILITY_HIGH, SHT3XD_FREQUENCY_10HZ) != SHT3XD_NO_ERROR) {
    Serial.println("你的SHT30无法启动周期性测量模式！");
  }

  irrecv.enableIRIn();
  Serial.println("红外接收就绪！请按下红外遥控器按键");

  irsend.begin();

  mySwitchSend.enableTransmit(rcsPin); //初始化433发射
  mySwitchRecv.enableReceive(digitalPinToInterrupt(rcrPin));//初始化433接收
  
  // 连接WiFi或启动AP
  if (!connectWiFi()) {
    startAP();
  }
  
  // 配置MQTT回调
  mqttClient.setCallback(mqttCallback);
  
  // 启动Web服务器
  server.on("/", handleRoot);
  server.on("/scanWiFi", scanWiFiNetworks);
  server.on("/getWiFiConfig", handleGetWiFiConfig);
  server.on("/getMQTTConfig", handleGetMQTTConfig);
  server.on("/saveWiFiConfig", HTTP_POST, handleSaveWiFiConfig);
  server.on("/saveMQTTConfig", HTTP_POST, handleSaveMQTTConfig);
  server.begin();
  Serial.println("Web服务器启动");
}

void loop() {
  server.handleClient();  // 处理Web请求
  button.tick();          // 按钮状态检测（必须放在loop中）
  
  // 红外接收
  if (irrecv.decode(&results)) {
    Serial.println("\n--- 检测到红外信号 ---");
    // 输出原始数据
    String irData = resultToSourceCode(&results);
    Serial.print("原始数据: ");
    Serial.println(irData);
    // 提取原始数据数组中的数值部分（仅保留{}内的内容）
    int startIdx = irData.indexOf("{") + 1;  // 找到{的位置并+1
    int endIdx = irData.lastIndexOf("}");     // 找到}的位置
    String irValues = irData.substring(startIdx, endIdx);  // 提取中间的数值部分
    
    // 仅在MQTT连接时发布红外数据
    if (mqttClient.connected()) {
      StaticJsonDocument<1024> statusDoc;
      statusDoc["client_id"] = clientId;
      statusDoc["ir_receive"] = irValues;  // 红外接收数据
      String statusJson;
      serializeJson(statusDoc, statusJson);
      
      if (mqttClient.publish(configMQTT.topic, statusJson.c_str())) {
        Serial.println("红外数据已发布: " + statusJson);
      } else {
        Serial.println("红外数据发布失败");
      }
    } else {
      Serial.println("MQTT未连接，红外数据暂不发布");
    }
    
    irrecv.resume();  // 继续接收下一个信号
  }


  // 433接收逻辑
  if (mySwitchRecv.available()) {
    long receivedValue = mySwitchRecv.getReceivedValue();
    if (receivedValue != 0) {
      Serial.print("收到射频数据: ");
      Serial.println(receivedValue);
      indicatorLight();  // 433接收成功，指示灯闪一下

      // 仅在MQTT连接时发布数据
      if (mqttClient.connected()) {
        StaticJsonDocument<128> statusDoc;
        statusDoc["client_id"] = clientId;
        statusDoc["rf_receive"] = receivedValue;
        String statusJson;
        serializeJson(statusDoc, statusJson);
        
        if (mqttClient.publish(configMQTT.topic, statusJson.c_str())) {
          Serial.println("射频数据已发布: " + statusJson);
        } else {
          Serial.println("射频数据发布失败");
        }
      } else {
        Serial.println("MQTT未连接，射频数据暂不发布");
      }
    }
    mySwitchRecv.resetAvailable();  // 重置接收状态
  }
  
  // WiFi重连逻辑
  if (WiFi.status() != WL_CONNECTED) {
    if (strlen(configWiFi.ssid) > 0) {
      Serial.print("WiFi重连(");
      Serial.print(reconnectAttempts + 1);
      Serial.println(")...");
      if (!connectWiFi() && reconnectAttempts >= 3) {
        clearWiFiConfig();
        ESP.restart();
      }
    }
    delay(5000);
    return;
  }
  
  // MQTT重连逻辑
  if (!mqttClient.connected()) {
    if (mqttReconnectAttempts < 5) {
      connectMQTT();
    } else {
      Serial.println("MQTT重连次数过多，等待30秒...");
      delay(30000);
      mqttReconnectAttempts = 0;
    }
  } else {
    mqttClient.loop();
    
    // 定时发布在线状态（30秒一次）
    static unsigned long lastPublish = 0;
    if (millis() - lastPublish > 30000) {
      StaticJsonDocument<128> statusDoc;
      SHT31D sht_result=sht3xd.periodicFetchData();
      if (sht_result.error == SHT3XD_NO_ERROR) {
        statusDoc["tem"]=String(sht_result.t, 2);
        statusDoc["hum"]=String(sht_result.rh, 2);
      }else{
        Serial.print("SHT30: 读取失败，错误代码=");
        Serial.println(sht_result.error);
      }
      statusDoc["client_id"] = clientId;
      statusDoc["status"] = "设备在线";
      statusDoc["ip"] = WiFi.localIP().toString();
      statusDoc["light"] = ledState ? "on" : "off";
      String statusJson;
      serializeJson(statusDoc, statusJson);
      
      if (mqttClient.publish(configMQTT.topic, statusJson.c_str())) {
        Serial.println("发布状态成功: " + statusJson);
      } else {
        Serial.println("发布状态失败");
      }
      lastPublish = millis();
    }
  }
  
  delay(50);
}
