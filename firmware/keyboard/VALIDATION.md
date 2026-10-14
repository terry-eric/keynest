# PCB2 Keypad v8 公開驗證摘要

2026-10-14，`macro-studio-v8`：ESP-IDF 5.5.0／PlatformIO 6.2.0 編譯通過，app image 1,095,648 bytes；4 個產物及 64 個來源雜湊見 `firmware/artifacts/keyboard/manifest.json`。同日已完成一塊 PCB2 實板更新；以下分開記錄本版實機證據與下方 v7 歷史結果。

- 更新前完整備份 4 MB Flash 並核對原分割表與 v8 相同；只寫入 bootloader `0x0` 與 app `0x10000`。兩個映像皆由獨立 `verify_flash` 通過校驗。
- 燒錄後、重啟前，原分割表／NVS／PHY 區及 Flash 錄音、按鍵音區逐 byte 比對一致。自動重啟成功，應用設定與更新前一致；實機辨識 2,097,152 bytes PSRAM，音訊與內建 Flash 就緒。
- USB 回報 macro API version 1，四份 program 可讀取；本板目前沒有啟用巨集。Windows pynput 輸入後端已初始化，桌面工具與板子的 host lease 已連接；本機控制台顯示實板連線、可編輯按鍵。沒有自動輸入測試文字。

- 新增 4 顆鍵的文字／多鍵／巨集 NVS 設定，以獨立 namespace 保存，不修改 v7 設定 blob 或分割區。每顆最多 16 步、每個 chord 最多 16 鍵、文字每段 512 UTF-8 bytes，整段 1200 bytes。
- 普通快捷鍵維持原 USB HID。巨集鍵不會同時送出舊的 HID 按鍵；只在按下緣產生一次實體事件，由明確啟用的桌面工具執行。網頁效果測試不觸發文字。
- C 主機測試通過：中文／emoji、欄位與 NUL 驗證、重複鍵拒絕、NVS 寫入失敗時保留原動作、單一 host lease、忙碌／未連接時丟棄、取走確認、逾期事件不補送。桌面工具先 ACK 再輸入，回覆不確定時不重送輸入。
- 桌面工具 7 項主機測試通過：多鍵與 Unicode 步驟、先驗證整段、ACK 失敗不輸入、等待取消、輸入失敗的按鍵釋放、Wayland 文字 stdin、本機 HTTP 的 Host／Origin／檔案範圍限制。測試使用模擬輸入，沒有對目前電腦送出真實按鍵。
- USB 13 項既有／新增測試、Web Serial、WAV／PCM、自訂音效裁切／CRC、前端巨集驗證與建置測試通過。GPIO、Quad PSRAM 設定、左麥克風 slot 與固定分割區已核對。
- 實際按鍵巨集事件、巨集 program 的 NVS 重啟恢復、Windows／macOS／Linux 中文輸入、實體多鍵輸入尚未測試。Linux Wayland 取決於 virtual-keyboard 協定，不能保證所有桌面環境可用。
- 麥克風人聲品質、SD 不穩定及 USB 音效 CRC 根因仍未解決。

## v7 實機歷史紀錄

以下記錄屬於 `custom-sound-v7`，保留以供比較。

此文件只包含版本、通用功能測試及尚未完成的項目，不包含原始音訊、裝置識別碼、USB 埠編號或本機工作路徑。

- ESP-IDF 5.5.0／PlatformIO 6.2.0 交叉編譯成功。app image 為 1,095,712 bytes，SHA-256 `74411911b25cf1cd9213693e63b43fcfc1b4e708c27ce7188e7f83520c1d7e2a`。三個燒錄映像均通過獨立 `verify_flash`。
- 實機辨識 2,097,152 bytes Quad PSRAM。音效及 Flash 錄音緩衝優先使用 PSRAM，支援內部 RAM 回退；分段配置避免要求大型連續配置。
- GUI 2 秒 WAV 音效上傳、最新錄音裁切成 1 秒音效、Flash 完整 CRC、四聲部播放計數通過；實體按鍵音效已聽測。Flash 2.048 秒錄音為 32,768 frames、65,580 bytes、0 dropped，重複下載一致。
- 1200 baud USB touch 與網頁下載模式實測成功，下載埠與執行埠切換後可完成燒錄、校驗及正常重啟；音效與設定重啟後恢復。未聲稱所有 Arduino IDE 上傳流程皆已測試。
- USB 原有 12 項測試、Web PCM／WAV 測試、新增裁切／重採樣／正規化／靜音及 NaN 拒絕、CRC32 測試，以及 USB 有限重送／不可重送操作測試通過。
- 實機曾出現 USB PCM 區塊 CRC 錯誤；主機只重送冪等且相同的 sound begin／chunk／commit，最多 5 次，完整 CRC 仍須通過。根因尚未確認。
- SD 曾成功讀到 7,948,206,080 bytes，錄音及兩次下載一致；其後仍出現 32,096,256 bytes／FATFS 掛載失敗或 CID CRC 錯誤。降到 400 kHz 尚未修復，未格式化或修改分割表。SD 音效保存尚未完成實機驗證。
- 麥克風人聲收音品質仍未通過。原始錄音、Flash 備份及裝置識別碼不包含在此次發布。


主機測試：

```powershell
python tools/test-keypad-usb.py
python tools/test-custom-sound-usb-retries.py
node tools/test-keypad-web.cjs
node tools/test-custom-sound-web.cjs
```
