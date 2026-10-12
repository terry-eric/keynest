# PCB2 四鍵鍵盤：ESP-IDF 韌體與 Web App

這個應用使用專案的 `pcb2_board` component，整合 USB HID 鍵盤、四顆 RGB LED、喇叭音效、麥克風音量與 WAV 錄音。Web App 直接放在韌體中，由 ESP32-S3 提供；連到裝置 Wi-Fi 即可操作，無需網際網路或 SD 卡。

目前已完成 ESP-IDF 5.5.0 交叉編譯與軟體驗證，**尚未燒錄或在實際 PCB 上測試**。接腳沿用 2026-09-19 的 EasyEDA 保存快照，詳細對照見 [board library](../README.md)。

## 編譯、燒錄與開啟

從專案根目錄執行：

```powershell
.\tools\build.ps1 -Keyboard
```

helper 使用 PlatformIO 作為 ESP-IDF 的建置工具，將含空白的 Windows 專案路徑暫存到無空白路徑。這是原生 ESP-IDF C 程式，沒有 Arduino 相依。成功後輸出到 `firmware/artifacts/keyboard/`。

準備好連接 PCB 後，自行指定實際燒錄埠：

```powershell
.\tools\build.ps1 -Keyboard -Upload -Port COMx
```

若無法自動進入下載模式，按住 BOOT、按一下 RESET，再放開 BOOT，選擇新出現的下載埠。燒錄後 USB 會切換成 **HID 鍵盤 + CDC 串列埠**，Windows 的 COM 編號可能改變。GPIO44 用於 SD MISO；韌體沒有啟用 UART0 console，執行日誌走 TinyUSB CDC。

1. 以支援資料傳輸的 USB 線連接 ESP32-S3 的原生 USB。
2. 連接 Wi-Fi `PCB2-Keypad-XXXX`，預設密碼為 `pcb2play`。
3. 瀏覽器開啟 `http://192.168.4.1`。手機若提示此 Wi-Fi 沒有網際網路，選擇保持連線。
4. 設定按鍵、燈光或音效，按「套用變更」。按「保存到裝置」才會寫入 NVS，重開機後保留。
5. 在「錄音與保存」選擇目的地，按開始／停止，再下載 WAV。

Wi-Fi 名稱、密碼與錄音時間上限可在 `menuconfig → PCB2 Keypad` 修改。預設為 WPA2 AP、頻道 6，最多四個 Wi-Fi 用戶端。沒有 captive portal，請直接輸入上述 IP。

### 使用既有 ESP-IDF 環境

保留 `firmware/keyboard` 與 `firmware/components/pcb2_board` 的相對位置。Windows 請先將整個 `firmware` 複製到沒有空白的路徑，再進入其中的 `keyboard`：

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

Component Manager 會依 manifest 與 lock file 取得官方相依。本次實際驗證的是 PlatformIO 執行的 ESP-IDF 5.5.0 建置，沒有另行執行原生 `idf.py` CLI。

## 功能

| 功能 | 行為 |
|---|---|
| USB 鍵盤 | 四鍵預設 F13–F16；每顆可設定字母、數字、功能鍵、方向鍵、常用控制鍵及 Ctrl／Shift／Alt／Win |
| 同時按鍵 | 可同時按住四鍵；重複 HID usage 合併，修飾鍵合併；放開後送出 release |
| LED | 關閉、常亮、呼吸、彩虹、按鍵漣漪、麥克風音量律動；主色、亮度與動畫速度可調 |
| 喇叭 | Click、Chime、Retro、Bubble 四種合成音效，加靜音；四鍵不同音高，可混合四個聲部 |
| 麥克風 | 即時音量、歷史曲線與 1–16× 數位增益；dBFS 是數位音量，不是校準的環境 dB SPL |
| 設定保存 | 只在按下保存時寫入 NVS；錄音或裝置錄音下載期間不寫設定 |

預設喇叭音量 10%、LED 亮度 64/255。網頁中的預覽鍵與「測試音效」會觸發 LED／喇叭；**USB 鍵盤輸入由實體按鍵產生**。

映射使用 USB HID usage，實際輸出的字元由電腦鍵盤布局決定。每鍵是一個 usage 加修飾鍵，沒有文字巨集。映射「停用」且取消修飾鍵可完全停用該鍵的 USB 輸入；若保留修飾鍵，仍可作為單獨的 Ctrl 等按鍵使用。

## 可選錄音位置

所有目的地都使用 **16 kHz、單聲道、16-bit PCM WAV**，約 32 KB/秒。麥克風在 48 kHz I²S 下工作，程式平均每三個樣本產生一個錄音樣本。這是簡單的降採樣，不是完整的高品質音訊濾波器。

| 目的地 | 預設上限 | 保存方式 | 無 SD 卡可用 |
|---|---:|---|---|
| 瀏覽器（預設） | 60 秒 | Wi-Fi 傳送 PCM，在瀏覽器組成 WAV，再下載到手機／電腦 | 是 |
| 內建 Flash | 2.048 秒 | 先錄入 64 KiB RAM；停止後保存裝置最新一段，重開機後可下載 | 是 |
| microSD（選用） | 60 秒 | 建立 `REC00001.WAV` 等新檔，不覆蓋既有檔案 | 否 |

瀏覽器與 SD 的上限可由 `CONFIG_PCB2_RECORD_SECONDS` 調成 10–300 秒。Flash 的 RAM 限制固定為 32,768 個樣本，本版不依賴 PSRAM。

**EEPROM／NVS 適合按鍵與效果設定；音訊由獨立 Flash partition 保存。** 現有硬體證據沒有額外 EEPROM，因此沒有假設存在未接線的晶片。

### 瀏覽器錄音

錄音期間請保持頁面在前景與 Wi-Fi 連線。裝置的串流緩衝約 512 ms；網路中斷或瀏覽器讀取太慢時會停止並回報資料遺失，不提供看似完整的損壞錄音。完成後按「下載本次瀏覽器錄音」。重新整理或關閉網頁會失去尚未下載的瀏覽器錄音。

串流有每次錄音的 owner ID，其他頁面無法拿走該段的 PCM。目的地每次錄音前可選；錄音進行中先停止，才可切換。

### 內建 Flash

4 MB Flash 中保留 `recording` partition，位置 `0x210000`、大小 `0x40000`。分成兩個 128 KiB slot，交替保存；每段附 CRC32，最後才提交 metadata。保存失敗或在提交前斷電時，可保留先前有效的一段。開機選擇最新的有效 slot，網頁提供 `INTERNAL.WAV` 下載。

錄音期間先用 RAM；停止後才進行 Flash erase／write。保存階段可能短暫影響按鍵、LED 與音訊的即時性，實機時間尚未量測。介面保留到保存完成才允許下一段錄音。這裡提供最新錄音下載，沒有多段檔案管理介面。

### microSD

**開機不嘗試掛載 SD。** 插入可用卡片後，按「重新偵測 SD」，成功才啟用 microSD 選項。偵測失敗仍可選瀏覽器或 Flash。程式不格式化卡片，並以 exclusive create 避免覆寫。

停止錄音時會更新 WAV header 並 flush／sync。若寫入途中斷電，SD 上可能留下未完成檔案；SD 沒有 Flash backend 的雙 slot 提交保護。

## 程式結構

| 檔案 | 職責 |
|---|---|
| `main/app_main.c`、`controller.c` | 初始化、NVS 設定、按鍵輪詢、LED 工作 |
| `main/app_logic.c` | 可在主機測試的 HID 報告、效果、PCM、WAV、CRC 邏輯 |
| `main/usb_keyboard.c` | TinyUSB HID + CDC 描述符與按鍵報告 |
| `main/audio_worker.c` | I²S 收音、音效混音、音量與降採樣 |
| `main/recorder.c` | 錄音狀態、佇列、瀏覽器串流、SD WAV 與下載互斥 |
| `main/flash_store.c` | Flash 雙 slot、CRC、提交與恢復 |
| `main/web_server.c` | Wi-Fi AP、HTTP API 與內嵌網頁 |
| `main/web/` | 完全離線的 HTML／CSS／JavaScript |

按鍵工作每 5 ms 輪詢，board library 以 20 ms 穩定時間去彈跳；LED 約每 25 ms 更新。按鍵與音訊在 core 1，錄音工作在 core 0，檔案操作不放在按鍵 callback 中。

### HTTP API

所有 POST 使用 `Content-Type: application/json`、`X-PCB2-Control: 1`；限制 body 1–1536 bytes，拒絕未知、重複、不完整或超出範圍的設定。服務沒有啟用跨來源 CORS。

| 方法／路徑 | 內容 |
|---|---|
| `GET /api/state` | 設定、按鍵、音量、USB 與錄音狀態 |
| `POST /api/config` | 完整設定交易，需包含下面全部九個欄位 |
| `POST /api/save` | `{}`，寫入 NVS |
| `POST /api/trigger` | `{"key":0}`，0–3；測試 LED／音效 |
| `POST /api/record` | `{"action":"start","target":"browser","owner":"0123456789abcdef"}`；target 可為 `browser`、`flash`、`sd` |
| `POST /api/record` | `{"action":"stop"}` |
| `POST /api/sd` | `{}`，要求 SD 掛載 |
| `GET /api/audio` | `X-PCB2-Recording` 帶 owner；最多 1024 個 PCM16 樣本；header 回傳狀態、總樣本與遺失數 |
| `GET /api/recording.wav` | 下載裝置最後保存的一段 Flash／SD WAV；須先完成錄音 |

預設完整設定：

```json
{
  "led_mode": 4, "brightness": 64, "speed": 80,
  "volume": 10, "mic_gain": 2, "sound": 2,
  "color": [120, 88, 255],
  "keycodes": [104, 105, 106, 107],
  "modifiers": [0, 0, 0, 0]
}
```

`modifiers` 各 bit 為左 Ctrl／Shift／Alt／GUI、右 Ctrl／Shift／Alt／GUI。Web App 提供左側修飾鍵 checkbox，會保留由 API 設定的右側 bit。

## 本機預覽與驗證

從專案根目錄啟動：

```powershell
python -m http.server 8027 --bind 127.0.0.1 --directory firmware/keyboard/main/web
```

開啟 `http://127.0.0.1:8027/?demo=1`。**展示模式的音量、按鍵、保存和錄音都是模擬資料**，不控制硬體，也不驗證 MCU 的 HTTP handler。沒有 `?demo=1` 時，頁面會向所在主機請求實際 API；單純靜態預覽伺服器會顯示裝置離線。

軟體測試與實機待驗項目見 [VALIDATION.md](VALIDATION.md)，建置紀錄見 [build.log](build.log)，燒錄檔與雜湊見 [manifest.json](../artifacts/keyboard/manifest.json)。

## 官方相依與參考

- [ESP-IDF 5.5 HTTP Server](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/protocols/esp_http_server.html)
- [ESP-IDF 5.5 Partition API](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/storage/partition.html)
- [ESP-IDF 5.5 NVS](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/storage/nvs_flash.html)
- [官方 TinyUSB HID 範例](https://github.com/espressif/esp-idf/tree/v5.5/examples/peripherals/usb/device/tusb_hid)及 [CDC console 範例](https://github.com/espressif/esp-idf/tree/v5.5/examples/peripherals/usb/device/tusb_console)

固定相依版本：ESP-IDF 5.5.0、`esp_tinyusb` 1.7.6~2、`tinyusb` 0.17.0~2、`led_strip` 3.0.3。
