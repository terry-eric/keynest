# 硬體與程式邊界

以下是 2026-10-13、custom-sound-v7 的已知基準。後續版本以當前程式及新硬體證據更新，不能把保存快照當作最新實板。

## 接腳

來源：`hardware/evidence/hardware-map.json`、`populated-hardware.json`、`firmware/components/pcb2_board/include/pcb2_pins.h`。保存設計的 device identity 曾繼承 C3，實際 target 是 ESP32-S3；用 GPIO 網路與 PAD_NET 核對，不用排針序號猜 GPIO。

| 功能 | GPIO | 備註 |
|---|---:|---|
| KEY1／KEY2／KEY3／KEY4 | 9／8／7／6 | active-low、內部上拉、20 ms 去彈跳 |
| RGB data | 13 | LED3 → LED4 → LED5 → LED6，GRB |
| I²S WS／BCLK | 4／5 | 麥克風與 NS4168 共用 |
| I²S DOUT | 10 | MCU → 喇叭功放 |
| I²S DIN | 11 | 麥克風 → MCU |
| SD CS／MOSI／MISO／SCK | 2／1／44／12 | SPI2 |
| 原生 USB D−／D+ | 19／20 | 保留給 USB |

GPIO44 已用作 SD MISO，避免啟用 UART0 RX 與它衝突。GPIO3／43／48 沒有此板既有外接周邊用途。

## 驅動與記憶體

- 實板已辨識 4 MB Flash、2 MB Quad PSRAM。鍵盤 v7 在 `sdkconfig.defaults` 啟用 Quad PSRAM，音效與錄音緩衝優先使用 PSRAM，沒有 PSRAM 時回退內部 RAM；不要改成 Octal 模式。原始 demo 的 README 不完全反映 v7。
- v7 RGB 使用 `board.c` 的 RMT DMA 實作，20 MHz ticks，針對 WS2812B-2020-V6：T0H/T0L 300/950 ns、T1H/T1L 650/600 ns、reset 350 µs。不要因舊 README 提到 led_strip，就直接移除目前時序實作；先確認實際 LED 版本。
- 音訊使用 I²S0 full duplex、Philips I²S、32-bit stereo slots、64 BCLK/frame。預設 48 kHz WS、3.072 MHz BCLK，沒有 MCLK 接線。喇叭重複 mono 到兩個 slot。
- 麥克風 slot 高 24 位轉為 signed PCM24；開機等待 50 ms。鍵盤錄音／監測是 16 kHz mono PCM16。調整對齊、降採樣或 gain 時，核對 `audio.c`、`board_logic.h`、`audio_worker.c` 及現有 fixture。
- SD mount 為 `/sdcard`，FAT/FAT32；v7 `storage.c` 將 SPI 保持在診斷用 400 kHz，`format_if_mount_failed=false`。CARD1 沒有 MCU 卡片偵測接腳。

## Library 邊界

`pcb2_board_init()` 初始化按鍵／RGB；audio 與 SD 分別初始化。各子系統由管理 task 使用；關閉相關工作與檔案 handle 後才 deinit。音訊每次最多 128 frames，timeout 也可能返回部分資料，先處理 count。

slot 選擇為暫時診斷設定：`pcb2_audio_set_mic_right()`、`pcb2_audio_mic_right()`；只有停止錄音後切換。`pcb2_audio_get_diagnostics()` 是最近 DMA block 的兩個 slot、增益前 PCM24 統計。

電池、充電器與電源切換沒有既有 MCU 感測／控制腳；不能虛構電池百分比或軟體斷電。NS4168 控制腳固定供電，以零 PCM 靜音。
