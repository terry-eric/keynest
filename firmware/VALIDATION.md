# 驗證紀錄

日期：2026-10-05（Asia/Taipei）。

## 已完成

| 項目 | 結果 | 證據 |
|---|---|---|
| 13 個 GPIO 對照 PCB pad/net 與 S3 symbol | 通過；無重複 | `../tools/check-pin-map.py`、`../hardware/evidence/` |
| 本機 C 邏輯測試 | 通過 | `../tools/test-board-logic.c` |
| ESP32-S3 demo 交叉編譯與連結 | 通過 | `build.log`、`artifacts/` |
| Unity 測試 app 交叉編譯與連結 | 通過 | `test-build.log`、`artifacts/tests/` |

使用 PlatformIO 6.2.0、espressif32 6.12.0、ESP-IDF 5.5.0、Xtensa GCC 14.2.0 與官方 led_strip 3.0.3。程式為原生 ESP-IDF component，未使用 Arduino。Windows 建置由 `../tools/build.ps1` 在不含空白的 temp 路徑完成。

本機測試以 TinyCC `-Wall -Werror -run` 執行，涵蓋按鍵彈跳、開機按住、超過 32-bit 毫秒範圍的時間戳、所有 65,536 個 PCM16 輸入、麥克風正負端點，以及 65,536 種顏色／亮度組合。

Demo 使用 RAM 15,056 bytes，程式 Flash 360,145 bytes（2 MB app partition）；測試 app 使用 RAM 13,916 bytes，程式 Flash 199,613 bytes（1 MB app partition）。兩份成功建置紀錄均無 compiler warning、compiler error 或未解析符號。SDK 設定為 4 MB Flash、USB Serial/JTAG console、無 PSRAM 相依。

另以最終 app image 確認四個 Unity 案例名稱都存在，避免測試的 constructor 註冊被 archive linker 移除。這只是連結驗證，沒有在實機執行案例。

## 映像

`firmware.bin` 是 app image，demo 與測試 app 的位址均為 `0x10000`；`bootloader.bin` 為 `0x0`；`partitions.bin` 為 `0x8000`。位址來自 ESP-IDF 產生的 `flasher_args.json`。使用 README 的 build／flash 指令會自動帶入位址。不要將 app image 單獨寫到 `0x0`。

兩份 app image 的 checksum 與 validation hash 均經官方 esptool 4.9.0 檢查通過。檔案大小、SHA-256 與燒錄位址記錄在 `artifacts/manifest.json`。

## 尚待實機確認

沒有燒錄 PCB，也沒有執行實機 Unity 測試。因此編譯通過不代表 LED、音訊、SD 或電源已經實測。

建議依序確認 USB console、四顆按鍵與 RGB、FAT32 microSD 掛載、KEY1 測試音、KEY2 麥克風 RMS，以及 48 kHz WS／3.072 MHz BCLK。Unity 測試 app 燒錄後，可在 USB console 輸入 `*` 執行四個案例。

接腳依據為 2026-09-19 21:59 保存的 EasyEDA 快照。本次 live adapter 無法監聽 `127.0.0.1:8765`（EACCES），未核對目前開啟文件；此後若改過 PCB 接線，需重新擷取證據並更新 `pcb2_pins.h`。
