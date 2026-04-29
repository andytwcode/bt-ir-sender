/*
 * ESP32 藍芽接收 → 紅外線發射程式
 * 
 * 透過 Classic Bluetooth (SPP) 接收手機指令，
 * 查詢 IR 對照表後發射對應的 NEC 紅外線訊號。
 * 
 * 手機端可使用「Serial Bluetooth Terminal」等 SPP App 連線。
 * 傳送 "1", "0", "+", "-", "m" 等指令即可發射紅外線。
 * 組合指令："3" = 1→0, "4" = 1→+→0, "5" = 1→+→+→0, "6" = 1→+→+→+→0（含延遲）
 * 
 * 硬體接線：
 *   ESP32 GPIO 32 ──[470Ω]── BC337-40 Base
 *   5V (VIN) ──[47Ω]── IR LED (+) ── IR LED (-) ── BC337-40 Collector
 *   BC337-40 Emitter ── GND
 */

#include "BluetoothSerial.h"
#include <IRsend.h>

#define DEVICE_NAME "ESP32-BT-IR"
#define IR_SEND_PIN 32

BluetoothSerial SerialBT;
IRsend irsend(IR_SEND_PIN);

// IR 對照表：藍芽指令 → NEC 數據
struct IrMapping {
  const char *key;
  uint64_t data;
};

const IrMapping irMap[] = {
  { "1", 0xFFA25D },
  { "0", 0xFFE21D },
  { "+", 0xFF22DD },
  { "-", 0xFFC23D },
  { "m", 0xFFE01F },
};
const int irMapSize = sizeof(irMap) / sizeof(irMap[0]);

// 查詢對照表，找到回傳 NEC data，找不到回傳 0
uint64_t lookupIrCode(const String &key) {
  for (int i = 0; i < irMapSize; i++) {
    if (key == irMap[i].key) {
      return irMap[i].data;
    }
  }
  return 0;
}

// 發射單一 IR 指令並記錄 log
void sendIrCode(const char *label, uint64_t code) {
  irsend.sendNEC(code, 32);
  Serial.print("  發射 \"");
  Serial.print(label);
  Serial.print("\" → NEC 0x");
  Serial.println(String((uint32_t)code, HEX));
}

// 處理單一藍芽命令。協定以單字元為主，換行會被忽略。
void handleCommand(const String &cmd) {
  Serial.print("收到: \"");
  Serial.print(cmd);
  Serial.print("\"");

  // 優先檢查組合指令
  if (handleComboCommand(cmd)) {
    return;
  }

  uint64_t irCode = lookupIrCode(cmd);

  if (irCode != 0) {
    Serial.println(" → 執行單一指令");
    sendIrCode(cmd.c_str(), irCode);

    SerialBT.print("OK: ");
    SerialBT.println(cmd);
  } else {
    Serial.println(" → 未知指令，忽略");
    SerialBT.print("ERR: unknown command \"");
    SerialBT.print(cmd);
    SerialBT.println("\"");
  }
}

// 處理組合指令，回傳 true 表示已處理
bool handleComboCommand(const String &cmd) {
  if (cmd == "3") {
    // "3": 送1 → 等0.5s → 送0
    Serial.println(" → 執行組合指令 [1 → 0]");
    SerialBT.println("combo: 1 -> 0");
    sendIrCode("1", lookupIrCode("1"));
    delay(500);
    sendIrCode("0", lookupIrCode("0"));
    SerialBT.println("OK: 3");
    return true;
  }
  if (cmd == "4") {
    // "4": 送1 → 等0.5s → 送+ → 等1s → 送0
    Serial.println(" → 執行組合指令 [1 → + → 0]");
    SerialBT.println("combo: 1 -> + -> 0");
    sendIrCode("1", lookupIrCode("1"));
    delay(500);
    sendIrCode("+", lookupIrCode("+"));
    delay(1000);
    sendIrCode("0", lookupIrCode("0"));
    SerialBT.println("OK: 4");
    return true;
  }
  if (cmd == "5") {
    // "5": 送1 → 等0.2s → 送+ → 等0.2s → 送+ → 等1s → 送0
    Serial.println(" → 執行組合指令 [1 → + → + → 0]");
    SerialBT.println("combo: 1 -> + -> + -> 0");
    sendIrCode("1", lookupIrCode("1"));
    delay(200);
    sendIrCode("+", lookupIrCode("+"));
    delay(200);
    sendIrCode("+", lookupIrCode("+"));
    delay(1000);
    sendIrCode("0", lookupIrCode("0"));
    SerialBT.println("OK: 5");
    return true;
  }
  if (cmd == "6") {
    // "6": 送1 → 等0.2s → 送+ → 等0.2s → 送+ → 等0.2s → 送+ → 等1s → 送0
    Serial.println(" → 執行組合指令 [1 → + → + → + → 0]");
    SerialBT.println("combo: 1 -> + -> + -> + -> 0");
    sendIrCode("1", lookupIrCode("1"));
    delay(200);
    sendIrCode("+", lookupIrCode("+"));
    delay(200);
    sendIrCode("+", lookupIrCode("+"));
    delay(200);
    sendIrCode("+", lookupIrCode("+"));
    delay(1000);
    sendIrCode("0", lookupIrCode("0"));
    SerialBT.println("OK: 6");
    return true;
  }
  return false;
}

void btCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  if (event == ESP_SPP_SRV_OPEN_EVT) {
    Serial.println(">> 裝置已連線");
  } else if (event == ESP_SPP_CLOSE_EVT) {
    Serial.println(">> 裝置已斷線");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== ESP32 藍芽→紅外線發射器 ===");
  Serial.print("裝置名稱: ");
  Serial.println(DEVICE_NAME);
  Serial.print("IR 發射腳位: GPIO ");
  Serial.println(IR_SEND_PIN);

  // 初始化 IR 發射
  irsend.begin();

  // 初始化藍芽
  SerialBT.register_callback(btCallback);
  if (!SerialBT.begin(DEVICE_NAME)) {
    Serial.println("Bluetooth 初始化失敗！");
    while (1) { delay(1000); }
  }

  // 顯示可用指令
  Serial.println("Bluetooth 已啟動，等待配對連線...");
  Serial.println("可用指令：");
  for (int i = 0; i < irMapSize; i++) {
    Serial.print("  \"");
    Serial.print(irMap[i].key);
    Serial.print("\" → 0x");
    Serial.println(String(irMap[i].data, HEX));
  }
  Serial.println("組合指令：");
  Serial.println("  \"3\" → 1 → 等0.5s → 0");
  Serial.println("  \"4\" → 1 → 等0.5s → + → 等1s → 0");
  Serial.println("  \"5\" → 1 → 等0.2s → + → 等0.2s → + → 等1s → 0");
  Serial.println("  \"6\" → 1 → 等0.2s → + → 等0.2s → + → 等0.2s → + → 等1s → 0");
  Serial.println("-----------------------------------");
}

void loop() {
  while (SerialBT.available()) {
    char c = SerialBT.read();

    // 單字元命令協定：逐字元立即處理，避免快速連送時被拼成同一筆。
    if (c != '\n' && c != '\r') {
      String cmd = "";
      cmd += c;
      handleCommand(cmd);
    }
  }

  delay(10);
}
