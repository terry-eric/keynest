# PCB2 ESP32-S3 board library

原生 ESP-IDF component，提供 4 顆按鍵、4 顆 WS2812B RGB LED、ZTS6672／ICS-43434 麥克風、NS4168 擴大機與 microSD 的控制介面。固定使用 ESP-IDF 5.5 系列與官方 `espressif/led_strip` 3.0.3；沒有 Arduino 相依。

完整的 **USB HID 鍵盤 + LED 樣態 + 喇叭音效 + 收音／錄音 + Web App** 位於 [keyboard/README.md](keyboard/README.md)。錄音可選瀏覽器、內建 Flash 或 microSD，沒有 SD 卡也能使用；從專案根目錄執行 `./tools/build.ps1 -Keyboard` 建置。以下內容為底層 board library 與原始硬體示範程式。

## 硬體依據

接腳來自 **2026-09-19 21:59 的 EasyEDA 最後保存快照**。本次即時橋接在開啟 `127.0.0.1:8765` 時回報 `EACCES`，因此沒有宣稱核對過目前開啟的 PCB。若此後改過接線，請先更新 `pcb2_pins.h`。

電路原本的裝置 library identity 仍繼承 C3；本程式採用實際原理圖覆寫後的 **ESP32-S3 SuperMini 18-pin symbol**，再將 symbol 的 GPIO 名稱與 PCB 的 `PAD_NET` 配對。腳位不是以排針編號猜測。

| 功能 | GPIO | MOD1 排針 pad | 元件／網路 |
|---|---:|---:|---|
| KEY1 | 9 | 11 | U2／KEY1 |
| KEY2 | 8 | 10 | U5／KEY2 |
| KEY3 | 7 | 9 | U6／KEY3 |
| KEY4 | 6 | 8 | U7／KEY4 |
| RGB data | 13 | 15 | LED3 → LED4 → LED5 → LED6 |
| I²S WS | 4 | 6 | U10 pin 1、U9 pin 2 |
| I²S BCLK | 5 | 7 | U10 pin 4、U9 pin 3 |
| I²S output | 10 | 12 | MCU → U9 pin 4 |
| I²S input | 11 | 13 | U10 pin 6 → MCU |
| SD CS | 2 | 4 | CARD1 pin 2 |
| SD MOSI | 1 | 3 | CARD1 pin 3 |
| SD MISO | 44 | 2 | CARD1 pin 7 |
| SD SCK | 12 | 14 | CARD1 pin 5 |

完整 pad/net 證據在 `../hardware/evidence/hardware-map.json`；來源雜湊在 `provenance.json`。13 個 GPIO 均無重複。GPIO44 已供 SD MISO 使用，因此範例 console 使用 SuperMini USB 的 Serial/JTAG，沒有啟用 UART0 RX。未使用 GPIO3、GPIO43、GPIO48 或 USB GPIO19/20 作為外接周邊。

## 編譯與燒錄

標準 ESP-IDF 5.5 環境，從本目錄執行：

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

Windows 的 ESP-IDF／PlatformIO 不接受某些含空白的工具鏈與 build 路徑。此專案目前在 `PCB 2`；可從專案根目錄使用提供的 helper：

```powershell
./tools/build.ps1
./tools/build.ps1 -Upload -Port COMx
```

helper 將維護中的程式複製到 `%TEMP%/pcb2-espidf-01a10781/firmware`，以 junction 指向專案 `.tools/platformio` 工具快取。第一次會下載工具，之後可沿用。沒有移動原始專案。成功後輸出 `firmware/artifacts/firmware.bin`、ELF、bootloader 和 partitions；`firmware.bin` 是 app image，不能當作包含 bootloader 的合併 image 燒在 0x0。使用 helper 的 `-Upload` 或 `idf.py flash` 會處理正確位址。

採用 PlatformIO `espressif32@6.12.0` 對應的 ESP-IDF 5.5.0。Flash 設定為 SuperMini 的 4 MB，程式不依賴 PSRAM；`esp32-s3-devkitc-1` 只作為晶片建置目標，實際外接接腳全部由 `pcb2_pins.h` 指定。

## 使用範例

開機會初始化低亮度 RGB、嘗試掛載 SD 並列出最多 20 個根目錄項目，以及開啟 48 kHz I²S。按鍵、SD 工作和音訊工作使用分開的 task，因此 SD 掛載不會阻塞按鍵輪詢。

| 按鍵 | 行為 |
|---|---|
| KEY1 | 200 ms、440 Hz、約 3% peak PCM 測試音，具有淡入淡出 |
| KEY2 | 開／關約每 0.5 秒的麥克風 RMS 與 dBFS 顯示 |
| KEY3 | 重試 SD 掛載；已掛載時顯示狀態 |
| KEY4 | 切換紅／綠／藍 |

每個按鍵對應同位置的 RGB；按下時變亮。SD 未插入時會記錄錯誤，其他功能仍可操作。範例沒有格式化卡片，也沒有建立測試檔。麥克風訊號沒有直接放大後送回喇叭；若要實作監聽，可透過下面的 PCM API 自行安排音訊流程。

## 放進其他 ESP-IDF 專案

將整個 `components/pcb2_board` 複製到新專案的 `components/`。保留其中的 `idf_component.yml`；Component Manager 會取得指定版本的官方 LED driver。在使用它的 component 宣告：

```cmake
idf_component_register(SRCS "main.c" INCLUDE_DIRS "." REQUIRES pcb2_board)
```

```c
#include "pcb2_board.h"

void app_main(void) {
    ESP_ERROR_CHECK(pcb2_board_init());
    ESP_ERROR_CHECK(pcb2_led_fill(0, 0, 255));
    ESP_ERROR_CHECK(pcb2_led_show());
    // 在自己的 task 中每 1~10 ms 呼叫 pcb2_keys_poll()。
    // 需要音訊／SD 時，再分別呼叫 pcb2_audio_init(48000)、pcb2_sd_mount()。
}
```

`pcb2_board_init()` 只初始化按鍵和 LED。音訊和 SD 各自有獨立的 init/deinit。每個子系統由一個 task 管理；GPIO/LED、音訊、SD 可以由不同 task 並行。停止相關 task／I/O 後才可呼叫 deinit。卸載 SD 前，先關閉所有 `FILE*`／目錄 handle。`pcb2_board_deinit()` 不會自動關閉音訊或 SD。

按鍵為 active-low 並啟用內部上拉，使用 20 ms 去彈跳；`pressed`／`released` 只表示兩次 poll 間產生的狀態變化。上電時已按住的鍵會反映在 `down`，不會製造一次虛假的按下事件。

LED 顏色使用 RGB 0~255，底層輸出 GRB。索引 0~3 對應 LED3~LED6。`set`、`fill`、`brightness` 都先更新緩衝，呼叫 `show()` 才送出；亮度縮放保留原始顏色，來回調整不會逐次損失色彩。預設 brightness 為 32/255。

音訊使用 I2S0 full duplex、Philips 格式、每聲道 32-bit slot／每 frame 64 BCLK。使用者於 2026-10-13 確認實際裝的是 **ZTS6672**，不是保存快照的 ICS-43434。型號由 `menuconfig → PCB2 board` 選擇；這是建置標示，不會自動偵測。

ZillTek DS-13 第 6 頁寫明 CONFIG 懸空、SELECT 接地對應右聲道；但本板右聲道實測全零，先前左聲道有非零訊號。**兩者不一致的原因仍未確定**，不可據此宣稱讀取格式或焊接已正確。本版預設左聲道以保留現有輸入；可用 `CONFIG_PCB2_MIC_RIGHT_SLOT` 更改開機聲道。啟動等待 50 ms。ICS-43434 的 LR 接地按規格使用左聲道。

鍵盤網頁提供暫時的左右聲道診斷切換，錄音期間禁止切換，重啟恢復建置聲道。API 狀態另提供兩個原始 slot 的 min／max、PCM24 平均值與扣除均值後的 AC RMS（取自最近 DMA block，增益之前）。`pcb2_audio_set_mic_right()`／`pcb2_audio_mic_right()` 可安全跨 task 呼叫；停止錄音後才可切換。`pcb2_audio_get_diagnostics()` 取得同時讀到的雙 slot 統計。

ZTS6672 的 CONFIG／SELECT 實際焊接狀態與底部進音通道尚需實板確認；PCB 圖面上的圓形不能代替製造鑽孔證據。料件報告與設計快照分開保存在 [populated-hardware.json](../hardware/evidence/populated-hardware.json)。

NS4168 收到相同的左右聲道單聲道 PCM。共用時鐘意味著收、播必須使用同一採樣率。支援 16／32／44.1／48 kHz；預設 48 kHz，BCLK 為 3.072 MHz。沒有 MCLK 接線。

```c
int32_t microphone[PCB2_AUDIO_BLOCK_FRAMES]; // signed 24-bit，右對齊
int16_t speaker[PCB2_AUDIO_BLOCK_FRAMES];    // signed 16-bit mono
size_t count = 0, written = 0;
esp_err_t err = pcb2_audio_read(microphone, 128, &count, 100);
// ESP_ERR_TIMEOUT 也可能有部分資料；先處理 count 再重試。
// speaker 填好後：
err = pcb2_audio_write(speaker, 128, &written, 100);
// 部分寫入時只重送 speaker + written 的剩餘部分。
```

API 一次接受 1~128 frames；DSP、音量控制、編碼、WAV／MP3、網路串流由上層應用負責。停供 TX 資料時 DMA buffer 自動清零。麥克風開啟後保留 20 ms 建立時間；關閉音訊後 BCLK／WS 拉低。RMS 只是數位碼相對 full-scale 的值，不是校準的 dB SPL。

SD 使用 SPI2、10 MHz，mount path 固定為 `/sdcard`，可用標準 `fopen`／`fread`／`fwrite`。請使用 FAT/FAT32；沒有啟用 exFAT。CARD1 的卡片偵測觸點沒有接 GPIO，所以以 mount／I/O 錯誤判斷失敗；更換卡片需先關閉檔案、unmount，再 mount。

## 與程式有關的硬體限制

- 電池、充電器、穩壓器、電源切換和充電指示 LED 沒有 MCU 控制／量測接腳，因此沒有虛構電池百分比、充電狀態或軟體關機 API。
- 保存電路的 MOD1 5V header 直連模組 USB。原理圖已註明電池模式須拔掉 USB；程式不能提供缺少的電源隔離。
- NS4168 控制腳在電路中固定接供電；library 以零 PCM 靜音，不能透過 GPIO 關閉功放電源。
- 使用此 library 的鍵盤 USB 診斷版已燒錄，麥克風資料傳輸與控制結果見 [鍵盤驗證紀錄](keyboard/VALIDATION.md)。獨立 bring-up／Unity 測試尚未在板子上執行；GPIO 核對、編譯與 USB 傳輸不代表 LED 的 3.3V→5V data 電位、SD 完整性、聲學路徑或供電能力已實測。

## 檢查與測試

從專案根目錄：

```powershell
python tools/check-pin-map.py
./tools/build.ps1
./tools/build.ps1 -Tests
```

`check-pin-map.py` 將 firmware 常數與獨立擷取的硬體證據比對，另核對 I²S 周邊實際 pin、麥克風 LR、SD CS 與 RGB 串接順序。Unity 測試 app 包含按鍵彈跳、正負 PCM 邊界／低 8-bit 排除、亮度邊界及未初始化／錯誤 buffer 行為。`-Tests` 是編譯；要實際執行，燒錄測試 app 後在 USB console 輸入 `*`。測試 app 會取代 demo app。

`tools/test-board-logic.c` 可由本機 C 編譯器執行，涵蓋完整 65,536 個 signed 16-bit PCM 輸入、65,536 種色彩／亮度組合，以及去彈跳、開機按住、超過 32-bit 毫秒範圍的時間戳。這是無硬體邏輯測試，與實機 Unity 測試分開記錄。

實機檢查：確認 USB 紀錄、逐一按鍵與 RGB、插 FAT32 SD、KEY1 測試音、KEY2 麥克風 meter，最後確認 48 kHz WS 與 3.072 MHz BCLK。最新結果記錄在 `VALIDATION.md`。

## 查核來源

- [ESP-IDF 5.5 I²S：full duplex 與標準模式](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/i2s.html)
- [ICS-43434 原廠資料表](https://invensense.tdk.com/wp-content/uploads/2016/02/DS-000069-ICS-43434-v1.2.pdf)
- [官方 led_strip 3.0.3](https://components.espressif.com/components/espressif/led_strip/versions/3.0.3/readme)
- [PlatformIO 6.12.0：ESP-IDF 5.5.0](https://github.com/platformio/platform-espressif32/releases/tag/v6.12.0)
- [SuperMini 製造商文件](https://wiki.nologo.tech/product/esp32/esp32s3/esp32s3supermini/esp32S3SuperMini.html)；該頁部分 CPU 描述不正確，本程式以 ESP-IDF 的 ESP32-S3 target 為準。
