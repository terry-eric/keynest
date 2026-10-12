# PCB2 ESP32-S3 Keypad

針對 PCB2 製作的原生 ESP-IDF board library、四鍵 USB 鍵盤韌體與離線 Web App。

- 四顆可設定的 USB HID 按鍵，預設 F13–F16，支援修飾鍵與同時按住。
- 四顆 RGB LED，六種樣態，可調色、亮度與速度。
- NS4168 喇叭四種合成音效與音量控制。
- ICS-43434 麥克風即時音量、PCM 波形、直流偏移診斷、數位增益與 WAV 錄音。
- 裝置透過 Wi-Fi 提供 Web App，不需網際網路或 SD 卡。

| 錄音目的地 | 預設上限 | 保存方式 |
|---|---:|---|
| 瀏覽器 | 60 秒 | 下載到手機／電腦 |
| 內建 Flash | 2.048 秒 | 保存最新一段，重開機後仍可下載 |
| microSD（選用） | 60 秒 | 建立新 WAV；需手動偵測卡片 |

按鍵與效果設定保存到 NVS。所有錄音採用 16 kHz、mono、16-bit PCM WAV。

## 使用

在 Windows／PowerShell 從 repository 根目錄執行：

```powershell
.\tools\build.ps1 -Keyboard
# 連接 PCB 並確認實際燒錄埠後：
.\tools\build.ps1 -Keyboard -Upload -Port COMx
```

建置使用 ESP-IDF 5.5.0，由 PlatformIO 呼叫；沒有 Arduino 相依。第一次會下載工具鏈。helper 適用目前含空白的 Windows 專案路徑，成功後輸出到 `firmware/artifacts/keyboard/`。

裝置開機後，連接 Wi-Fi `PCB2-Keypad-XXXX`（預設密碼 `pcb2play`），開啟 `http://192.168.4.1`。按「套用變更」生效，按「保存到裝置」才會在重開機後保留。

完整編譯、燒錄、USB、錄音與 API 說明見 [鍵盤應用文件](firmware/keyboard/README.md)。底層接腳與 component API 見 [board library 文件](firmware/README.md)。

## Web App 預覽

```powershell
python -m http.server 8027 --bind 127.0.0.1 --directory firmware/keyboard/main/web
```

開啟 `http://127.0.0.1:8027/?demo=1`。展示模式使用模擬資料，不會控制實體 PCB。

要在更新韌體前預覽真實麥克風，先連上 PCB Wi-Fi，再執行 `python tools/preview-keypad.py`，開啟 `http://127.0.0.1:8028`，按「開始即時監測」。本機相容預覽不保存音訊；停止後約 2 秒釋放暫存串流。詳細限制見鍵盤應用文件。

![Web App 展示](firmware/keyboard/validation/web-desktop.png)

## 驗證與硬體依據

2026-10-13 已完成即時波形版本的交叉編譯、主機測試與網頁展示驗證。另以實際 PCB 上的既有韌體驗證麥克風取樣及免 SD 的 WAV 串流；收音有明顯直流偏移、聲音偏弱，品質尚未通過。**新增波形韌體尚未燒錄**。接腳依 2026-09-19 EasyEDA 保存快照。詳細結果與待實機測試項目見 [驗證紀錄](firmware/keyboard/VALIDATION.md)。

- [硬體接腳證據](hardware/evidence/hardware-map.json)
- [已建置韌體](firmware/artifacts/keyboard/)及 [SHA-256 manifest](firmware/artifacts/keyboard/manifest.json)
- [測試與建置工具](tools/)

`firmware.bin` 是 app image，燒錄位址為 `0x10000`，不是可直接燒在 `0x0` 的合併映像。請使用 helper 的 `-Upload` 或 ESP-IDF `flash` 指令處理完整燒錄。
