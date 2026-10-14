# 開發與更新流程

在定位的 repository 根目錄執行工具。優先沿用當前工具；以下是 v7 基準，endpoint 或選項變更時讀取實作和 `firmware/keyboard/README.md`。

## 建置與測試

```powershell
python tools/check-pin-map.py
.\tools\build.ps1 -Keyboard
```

原生 ESP-IDF 5.5.0，由 PlatformIO 6.2.0／espressif32 6.12.0 建置。Windows 路徑含空白時，helper 暫存到無空白路徑並使用專案工具快取；沿用 helper 可避免直接在 `PCB 2` 下建置失敗。

底層 demo 使用 `build.ps1`；Unity 測試 app 使用 `-Tests`，會取代鍵盤 app。`-PrepareOnly` 只準備來源。`-Upload -Port COMx` 包含燒錄，不是純建置；只有在更新板子的任務內使用。

依修改選擇檢查，不必每次重跑全部：

| 修改 | 適合的既有檢查 |
|---|---|
| USB／RPC | `python tools/test-keypad-usb.py` |
| 音效傳輸／重送 | `python tools/test-custom-sound-usb-retries.py` |
| WAV／PCM／前端 | `node tools/test-keypad-web.cjs`、`node --check firmware/keyboard/main/web/app.js` |
| 裁切／重採樣／音量正規化 | `node tools/test-custom-sound-web.cjs` |
| 相容預覽 | `python tools/test-keypad-preview.py` |
| GPIO／C DSP／Flash 儲存 | `check-pin-map.py`、`test-board-logic.c`、`test-keypad-logic.c`、`test-keypad-flash.c`、`test-mic-monitor.c`；編譯參數見現有驗證文件 |

## USB Web App

```powershell
python tools/preview-keypad.py --usb --idle-timeout 3600
```

開啟 `http://127.0.0.1:8028/`。需要 pyserial；通常自動偵測 VID 303A／PID 4005 的執行中 USB CDC。ROM 下載模式是 PID 1001。COM 編號會變；不要把某台電腦的 COM 編號當作板子的固定設定。多個匹配裝置時，先核對目標再用 `--usb-port`。

一個工具管理串列埠。不要在預覽服務占用埠時再開 esptool／Serial Monitor。背景啟動服務時使用隱藏視窗、保存 PID／log，重啟前核對錄音已停止，瀏覽器尚未下載的 WAV 已保存。`POST /api/preview/close` 加 `X-PCB2-Control: 1`、JSON `{}` 可結束本機服務。

USB RPC 在日誌中以 `PCB2? `／`PCB2_RPC ` 分隔，JSON envelope 含 id、path、body，回覆含 protocol、status、data。使用 `tools/keypad_usb.py`，保留 id 配對、PCM 長度、CRC32、新鮮度與嚴格 JSON 欄位檢查；更動 endpoint 時同步 firmware HTTP、USB、host proxy 與前端。

Wi-Fi AP 是備用操作方式：`PCB2-Keypad-XXXX`、預設 `pcb2play`、`http://192.168.4.1`。密碼以目前設定為準；電腦 USB 流程不切換 Wi-Fi。展示模式 `?demo=1` 的資料是合成資料，不能用作實機證據。

## 錄音與按鍵音

- 錄音目的地：browser 最長預設 60 秒，需將瀏覽器 RAM 的 WAV 下載；flash 最新一段上限 2.048 秒；sd 預設 60 秒、新建 REC WAV。單一串流需維持 owner，不能讀取或停止別人的錄音。
- 即時監測 `/api/microphone` 讀最近 64 ms PCM 視窗，不消耗錄音串流，不能拼接成連續錄音。聲道控制 `/api/microphone/channel` 只在錄音停止後使用。
- 自訂按鍵音：Web 本機解碼音檔或使用最新錄音，裁切最多 2 秒，轉 16 kHz mono PCM16；四鍵共用音效，可混合四聲部。Flash 音效獨立於最新錄音，SD 使用新建 KEY WAV。
- v7 `/api/sound/begin`、`chunk`、`commit`、`cancel` 使用 owner、offset、block CRC 與完整 CRC。host 只對同一冪等 begin／chunk／commit 的指定暫時錯誤有限重送，最多 5 次；不要普遍重送 trigger、restart 或 Flash 寫入操作。

## 燒錄

先核對實板、app image、manifest、目標分割區與目前配置，再備份需要保留的資料。一般 app 更新寫 `0x10000`；bootloader／partition table 改變時，需一起檢查，不可只假設寫 app 已完成。

| v7 區域 | 起點 | 大小 |
|---|---|---|
| bootloader image | 0x0 | 以產物大小為準 |
| partition table image | 0x8000 | 以產物大小為準 |
| NVS | 0x9000 | 0x6000 |
| PHY | 0xf000 | 0x1000 |
| factory app | 0x10000 | 0x200000 |
| 最新 Flash 錄音 | 0x210000 | 0x40000 |
| Flash 按鍵音 | 0x250000 | 0x40000 |

`firmware.bin` 不是合併映像，不能寫到 `0x0`。保留原始備份及實際燒錄範圍；更新後獨立 `verify_flash`，必要時核對持久資料區，再確認 HID／CDC 重枚舉、設定與音訊反應。

支援新版切換的板子可使用網頁下載模式或：

```powershell
python tools/usb-bootloader.py --port COMx --expected-mac YOUR_BOARD_MAC
```

工具核對執行埠 VID／PID 與 USB serial，再以 1200 baud touch 切換同一板的 ROM 埠。先停止錄音／下載／上傳並釋放埠。舊韌體可能沒有此功能；確認不支援後再提示 BOOT／RESET。正常重啟前 BOOT 要放開。
