# PCB2 Keypad 驗證紀錄

日期：2026-10-05。此紀錄區分實際交叉編譯、主機軟體測試、Web 展示模式與待實機驗證項目。

## 建置通過

- 執行 `tools/build.ps1 -Keyboard`，以 PlatformIO 6.2.0／espressif32 6.12.0 呼叫 ESP-IDF 5.5.0、Xtensa GCC 14.2.0，結果 SUCCESS。
- 主程式、board component、TinyUSB HID + CDC、Wi-Fi／HTTP、錄音 backend 與三個內嵌網頁資源均成功編譯、連結。
- 最後建置紀錄沒有 compiler warning、error 或 undefined reference。
- 靜態 RAM：55,620 / 327,680 bytes；app 使用量：1,048,069 / 2,097,152 bytes。動態 Wi-Fi、task、錄音配置另計，不能將上述 RAM 數值當作最壞使用量。
- `firmware.bin` 實際大小 1,048,480 bytes。官方 esptool 4.9.0 確認 ESP32-S3（chip ID 9）、4 MB Flash、DIO、80 MHz，checksum 與 validation hash 均有效。
- 產物在 `../artifacts/keyboard/`，對應 SHA-256 與燒錄位址在 `manifest.json`。
- 最後來源與暫存建置目錄的 C／H、配置與 Web 資源內容核對一致，內嵌資源亦核對出現在 app image。

## 主機測試通過

| 測試 | 實際涵蓋 |
|---|---|
| `tools/test-keypad-logic.c` | 16 種四鍵按住組合、重複 usage 合併、修飾鍵、release、modifier-only、設定範圍案例、麥克風正負飽和、WAV header、標準 CRC32 fixture、音效靜音／幅度／起止、LED 關閉／常亮／漣漪 |
| `tools/test-keypad-flash.c` | 直接編譯 production `flash_store.c`；模擬 NOR erase／write 規則、雙 slot、新開機掃描、部分 erase／header／PCM／metadata 失敗，以及 CRC 損壞後回復先前錄音 |
| `tools/test-keypad-web.cjs` | WAV 格式／大小、時間範圍與非法 frame count；奇數 byte 或過大 PCM chunk 的拒絕 |
| JavaScript 語法 | `node --check firmware/keyboard/main/web/app.js` 通過 |

C 測試在 Windows 使用本機 TinyCC，連接 UCRT 的實際數學函式執行；測試回報 PASS。Flash 為主機模擬，沒有測量實體 Flash 延遲或磨耗。

在具備 GCC 的環境，可從專案根目錄重跑同一組 production 邏輯與 Flash 模擬測試：

```sh
gcc -std=c11 -Wall -Wextra tools/test-keypad-logic.c firmware/keyboard/main/app_logic.c -lm -o keypad-logic-test
./keypad-logic-test
gcc -std=c11 -Wall -Wextra -Itools/flash-test-includes tools/test-keypad-flash.c firmware/keyboard/main/flash_store.c firmware/keyboard/main/app_logic.c -lm -o keypad-flash-test
./keypad-flash-test
node --check firmware/keyboard/main/web/app.js
node tools/test-keypad-web.cjs
```

## Web App 展示模式通過

使用本機靜態伺服器與明確的 `?demo=1` 模式，沒有連到 MCU：

- 設定 KEY1 為 Ctrl+A，選彩虹 LED，套用後預覽及「尚未保存」狀態正確。
- 保存操作後狀態顯示已保存；這是展示資料的保存流程，沒有寫入真實 NVS。
- 切換內建 Flash，約 2.048 秒自動停止，顯示 `INTERNAL.WAV` 可下載。
- 瀏覽器錄音到 60 秒自動停止。實際下載的 WAV 由 Python `wave` 開啟，確認 mono、16 kHz、16-bit、960,000 frames、60.0 秒、1,920,044 bytes；聲音是展示模式合成的 220 Hz，不是 PCB 麥克風。
- 另一次瀏覽器錄音手動停止於約 18.2 秒，完成後下載連結生效、沒有錄音錯誤。
- microSD 未掛載時該選項不可選，瀏覽器與 Flash 仍可選。
- 沒有 `demo=1` 且實際 API 不存在時，呈現離線狀態、未知 USB／收音值，套用與錄音按鈕停用。
- 檢查 390 × 844 手機及 1280 × 900 桌面 viewport，六個面板均可用，沒有水平溢出。
- 展示模式操作期間沒有 JavaScript error／warning。

畫面證據：[桌面版](validation/web-desktop.png)、[手機版](validation/web-mobile.png)。展示模式不驗證實體 Wi-Fi 傳輸、韌體 HTTP handler 或麥克風資料。

## 待實機驗證

本次沒有燒錄板子。接腳依 2026-09-19 EasyEDA 保存快照，沒有即時橋接驗證最新 PCB。使用者已回報 SD 卡座方向阻礙插卡，因此本版開機不依賴 SD 掛載。

燒錄後請依序確認：

1. USB HID + CDC 枚舉、F13–F16／自訂映射、四鍵組合與按放 release。
2. 四顆 RGB 的實際對應位置、六種效果；NS4168 喇叭四種音效及音量。
3. ICS-43434 收音左右 slot／資料對齊、音量增益及削波。
4. Wi-Fi AP、手機控制、實際麥克風串流至瀏覽器 WAV；弱網路下的溢出提示。
5. Flash 短錄音、重開機後下載、保存期間的即時性；真實斷電恢復。
6. 若卡座可使用，再測試 SD 掛載、錄音、停止、下載及空間不足行為。

主機測試與成功建置不代表以上電氣、傳輸與即時性項目已通過。
