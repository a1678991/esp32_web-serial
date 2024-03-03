#include <Arduino.h>
#include <cmath>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <HardwareSerial.h>
#include <freertos/task.h>
#include <WiFi.h>

#define SERIAL_RX_PIN 16 // シリアル受信ピン
#define SERIAL_TX_PIN 17 // シリアル送信ピン
#define SERIAL_BAUD_RATE 115200 // シリアルのボーレート
#define LED_RX_PIN 2 // 受信用LEDのピン
#define LED_TX_PIN 4 // 送信用LEDのピン
#define LED_ON_DURATION_MS 20 // 信号が流れたときにLEDを点灯させる時間

unsigned long lastTxTime = 0;
unsigned long lastRxTime = 0;

constexpr auto ssid = "";
constexpr auto password = "";


// LED制御タスク
[[noreturn]] void ledControlTask(void* pvParameters) {
    while (true) {
        if (millis() - lastRxTime < LED_ON_DURATION_MS) {
            digitalWrite(LED_RX_PIN, HIGH);
        }
        else {
            digitalWrite(LED_RX_PIN, LOW);
        }

        if (millis() - lastTxTime < LED_ON_DURATION_MS) {
            digitalWrite(LED_TX_PIN, HIGH);
        }
        else {
            digitalWrite(LED_TX_PIN, LOW);
        }

        vTaskDelay(pdMS_TO_TICKS(1)); // 1msごとに状態を更新
    }
}


AsyncWebServer httpServer(80);
AsyncWebSocket ws("/ws");

void onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, const AwsEventType type, void* arg, const uint8_t* data, const size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.println("WebSocket client connected");
    }
    else if (type == WS_EVT_DISCONNECT) {
        Serial.println("WebSocket client disconnected");
    }
    else if (type == WS_EVT_ERROR) {
        Serial.println("WebSocket error");
    }
    else if (type == WS_EVT_DATA) {
        Serial.println("WebSocket data received");
        // if len is 0, send \n
        if (len == 0) {
            Serial1.write('\n');
        }
        else {
            Serial1.write(data, len);
        }
        lastTxTime = millis();
    }
}

constexpr auto html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WebSocket Auto-Connect</title>
    <style>
        body {
            display: flex;
            flex-direction: column;
            height: 100vh;
            padding: 0;
            margin: 0;
            font-family: monospace
        }
        #messages {
            height: 100%;
            overflow: auto;
            padding: 0;
            margin: 0;
        }
        #messageBox {
            padding: 0;
            margin: 0;
        }
        code {
            white-space: pre;
        }
    </style>
</head>
<body>
<div id="messages">
    <pre><code id="code"></code></pre>
</div>
<input type="text" id="messageBox" placeholder="Type a message..." oninput="sendMessage()" onkeypress="if(event.keyCode==13)sendMessage();if(event.KeyCode==8)backspace();" />

<script>
    let ws;
    const messagesDiv = document.getElementById('messages');
    const messageBox = document.getElementById('messageBox');
    const codeElement = document.querySelector('#messages code');

    function scrollToBottom() {
        messagesDiv.scrollTop = messagesDiv.scrollHeight;
    }

    function writeMessage(message) {
        codeElement.textContent += message;
        scrollToBottom();
    }

    function backspace() {
        ws.send('\b');
    }

    function connect() {
        // Use current host for WebSocket connection
        const host = window.location.hostname;
        ws = new WebSocket(`ws://${host}:80/ws`);

        ws.onopen = function() {
            writeMessage("Connected to the WebSocket server\n");
        };

        ws.onmessage = function(event) {
            writeMessage(event.data);
        };

        ws.onclose = function() {
            writeMessage("Connection closed... trying to reconnect\n")
            setTimeout(connect, 1000); // Try to reconnect after 1 second
        };

        ws.onerror = function(err) {
            console.error(err);
            writeMessage("WebSocket encountered error: " + err.message + "Closing socket\n")
            ws.close();
        };
    }

    function sendMessage() {
        const message = messageBox.value;
        ws.send(message);
        messageBox.value = ''; // Clear the input after sending
    }

    // Automatically connect when the page loads
    window.onload = connect;
</script>
</body>
</html>
)rawliteral";

void setup() {
    // バッファサイズの計算と設定
    constexpr int bufferSize = 512;
    Serial.setRxBufferSize(bufferSize);
    Serial1.setRxBufferSize(bufferSize);

    Serial.begin(SERIAL_BAUD_RATE);
    Serial1.begin(SERIAL_BAUD_RATE, SERIAL_8N1, SERIAL_RX_PIN, SERIAL_TX_PIN);
    Serial.println();

    // LED制御
    pinMode(LED_RX_PIN, OUTPUT);
    pinMode(LED_TX_PIN, OUTPUT);
    xTaskCreate(ledControlTask, "LEDControlTask", 1024, nullptr, 1, nullptr);

    // Wi-Fi
    Serial.printf("Connecting to WiFi %s\n", ssid);
    WiFiClass::mode(WIFI_MODE_STA);
    WiFi.begin(ssid, password);
    while (WiFiClass::status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("WiFi Connected");
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());

    // HTTP server
    ws.onEvent(onWebSocketEvent);
    httpServer.addHandler(&ws);
    httpServer.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", html);
    });
    httpServer.begin();
    Serial.println("Async HTTP server started");

    // mDNS
    Serial.println("mDNS begin");
    if (MDNS.begin("web-serial")) {
        Serial.println("mDNS responder started");
    }
    MDNS.addService("http", "tcp", 80);
}

void loop() {
    // なんか来るまで待機
    while (!(Serial.available() || Serial1.available())) {
        delay(1);
    }
    // 来たら読み込んで送信
    if (Serial1.available()) {
        const auto buf_len = Serial1.available();
        const auto buf = static_cast<uint8_t *>(malloc(buf_len));
        Serial1.readBytes(buf, buf_len);
        Serial.write(buf, buf_len);
        ws.textAll(buf, buf_len);
        free(buf);
        lastRxTime = millis();
    }
    // デバッグ用
    if (Serial.available()) {
        Serial1.write(Serial.read());
    }
}
