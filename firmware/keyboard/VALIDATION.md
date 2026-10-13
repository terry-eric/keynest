# PCB2 Keypad v7 公開驗證摘要

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
