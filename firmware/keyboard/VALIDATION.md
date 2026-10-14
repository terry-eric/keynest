# PCB2 Keypad v10 公開驗證摘要

2026-10-15，`keypad-studio-v10`，ESP-IDF 5.5.0／PlatformIO 6.2.0。實板 app SHA-256：`e9341d6e3176303b901eaccc1f1cf82eac6206fd8f80fb4236c77c060110222a`。本節只列 v10 本次證據；音訊品質與 10 分鐘錄音的歷史結果見 v9。

- 純公開控制台與板上巨集：正式 Edge 網站 Web Serial 直接連接，不啟動 localhost／Python companion。USB 原已授權，自動連接偏好恢復為啟用，網站不含 localhost 連結或 companion.js；GSAP 動畫保留。畫面顯示「文字與巨集：板上執行」。
- 使用者在記事本選取測試文字、按第 1 顆實體鍵並 Ctrl+V，確認成功複製貼上。韌體 counter 回報 completed=1、failed=0、dropped=0；原第 1 鍵的 Ctrl+C program 保留。這是 v10 板上 HID 的實際輸入證據，不是舊背景工具。
- 完整 4 MB 私人 Flash 備份核對原 v9 app／分割表。只更新 0x10000 app；獨立 verify_flash 通過。0x000000–0x00FFFF 的 bootloader／分割表／NVS／PHY 與 0x210000–0x38FFFF 的錄音／全部按鍵音區核對一致。重啟後原 config 與四份 program 完整相同；原始備份、識別碼與音訊未公開。
- 一次長音訊唯讀校驗因 USB packet transfer stopped 中斷，沒有改寫音訊；後續分成六段讀回，全部與原始備份一致。最終 app、設定區及音訊區另用 verify_flash 的裝置雜湊核對通過。一次重枚舉暫時出現無法開啟的舊 COM 記錄，待 native USB 就緒後唯讀查詢成功；沒有重送任何輸入動作。
- test-macro-store.py 編譯真實 NVS store 與文字引擎：UTF-8／欄位／長度、NVS 失敗、執行 snapshot、舊 host lease 拒絕、16 鍵位元圖、六種媒體鍵、英文符號／換行、Caps Lock、等待與 Linux／macOS Unicode 序列通過。
- test-usb-macro.py 編譯真實 USB tick：HID descriptor 長度、初始化、長按不重複、傳輸背壓、兩種 report 放鍵、同時按下略過、斷線不重播、普通按鍵中止、逾時、同鍵中止時持續保留 all-up 報告，全部通過。測試後端是虛擬 HID，未向使用者軟體自動輸入文字。
- Node：studio 的擷取／UTF-8／中文模式預檢／動畫偏好、連線能力、Web Serial CRC／owner／取消／冪等重送通過。公開網站建置保留原 control IDs，10 個正式 assets／安全標頭／Roomly 與 Trip 六條路由核對通過；USB 14 項與 GPIO 接腳檢查通過。
- 尚未實板驗證 16 鍵／媒體鍵、英文長文字／多步巨集與 macOS／Linux 系統文字輸入。Windows 免背景工具中文沒有通用支援；Linux 需目標輸入環境支援 Ctrl+Shift+U；macOS 需 Unicode Hex Input，擴充字元／emoji 整段拒絕。沒有聲稱跨平台已通過。
- v10 不改音訊實作及 v9 分割區；麥克風人聲品質、SD 間歇容量／掛載、USB 音效 CRC、喇叭主觀混音品質及長 SD 錄音／下載仍待確認。

## v9 歷史驗證摘要

2026-10-15，`keypad-studio-v9`，ESP-IDF 5.5.0／PlatformIO 6.2.0。該版產物與驗證紀錄位於 Git commit `1d8e8e3`；當前 manifest 已更新為 v10。

- 完整備份 4 MB Flash，核對 v8 分割表及 app，確認新增音效空間原本全為空白。免按 BOOT 切換成功；只寫 `0x8000` 分割表及 `0x10000` app，獨立 `verify_flash` 通過。NVS／PHY、Flash 錄音、原共用音效及擴充空間在重啟前逐 byte 比對一致。
- `key_sound` 僅從尾端擴大至 `0x140000`；原共用雙 slot 留在前 `0x40000`，四顆鍵各增加獨立雙 slot。每鍵 CRC、owner／offset 驗證、舊版共用音效相容、重送及 NOR 擦除／PCM／metadata 中途失敗測試通過。
- 實板四鍵各上傳不同的 32,000 frames（2 秒）Flash PCM；真正重啟後四份 CRC 一致。依序快速觸發形成重疊播放，每鍵均完整渲染 96,000 個 48 kHz 輸出樣本。完成後移除合成測試音效覆寫、恢復原音量／音效風格，四份原按鍵 program 與共用音效維持一致。
- 上傳測試曾有 CRC 及 `ESP_ERR_INVALID_ARG` 拒絕，取消後重做成功；未忽略校驗或加入任意控制命令重送。這個既有 USB 間歇問題仍待定位，成功校驗不代表所有傳輸都穩定。
- 實板連續 USB 錄音 600.0000 秒，接收 9,600,000 frames，掉樣本 0；WAV 19,200,044 bytes，header／PCM 長度一致。透過本機工具持續讀取，沒有改電腦 Wi-Fi。音檔與備份只留在本機，未發佈。
- 編譯後 `CONFIG_PCB2_RECORD_SECONDS=600` 已核對，實板回報上限 600。瀏覽器／SD 長錄音上限改為 10 分鐘，固定串流緩衝維持 8,192 frames；Flash 最新短錄音仍為 32,768 frames（2.048 秒）。10 分鐘 SD 錄音與長檔下載未實測。
- 合成聲與自訂聲使用 32-bit 累加及同一音量限制器，瞬時訊號維持 ±28,000 內，降低增益後約 50 ms 釋放；離線測試確認低音量不變、四聲部超量時不溢位、聲部結束時增益不突跳。喇叭主觀音質及麥克風人聲品質尚未由使用者確認。
- 控制台 v6：未連線只顯示連接卡片，保留動畫；文字／巨集工具說明移至相關按鍵編輯器。瀏覽器實際操作確認四鍵上傳指定的 key／片段長度、裁切預覽、恢復共用；斷線仍可下載既有 WAV，快捷鍵與文字草稿重新連接後保留。390 px 與 1280 px 版面檢查通過。
- 通過 USB 14 項、本機巨集工具 8 項、舊預覽 5 項、Web Serial CRC／owner／取消／重送、WAV 10 分鐘邊界、裁切正規化、按鍵擷取／巨集及網站建置檢查。新增 `python tools/test-key-sound.py` 執行真實音效實作的 NOR／NVS 模擬及既有按鍵邏輯測試，不接 USB、不輸入文字。

## v8 歷史驗證摘要

2026-10-14，`macro-studio-v8`：ESP-IDF 5.5.0／PlatformIO 6.2.0 編譯通過，app image 1,095,648 bytes；4 個產物及 64 個來源雜湊見 `firmware/artifacts/keyboard/manifest.json`。同日已完成一塊 PCB2 實板更新；以下分開記錄本版實機證據與下方 v7 歷史結果。

- 更新前完整備份 4 MB Flash 並核對原分割表與 v8 相同；只寫入 bootloader `0x0` 與 app `0x10000`。兩個映像皆由獨立 `verify_flash` 通過校驗。
- 燒錄後、重啟前，原分割表／NVS／PHY 區及 Flash 錄音、按鍵音區逐 byte 比對一致。自動重啟成功，應用設定與更新前一致；實機辨識 2,097,152 bytes PSRAM，音訊與內建 Flash 就緒。
- USB 回報 macro API version 1，四份 program 可讀取；更新後首次讀取尚無巨集，後續已保存並啟用第 1 顆鍵的 Ctrl+C 組合。Windows pynput 輸入後端已初始化，桌面工具與板子的 host lease 已連接；本機控制台顯示實板連線、可編輯按鍵。
- 使用者在 Windows 以第 1 顆實體鍵執行 Ctrl+C，並確認 Ctrl+V 能貼上選取的測試文字。桌面工具回報收到 3 次事件、完成 3 次、失敗 0 次；此結果驗證實體事件至桌面組合鍵輸出的流程，不涵蓋中文文字、多步巨集或重啟恢復。

- 新增 4 顆鍵的文字／多鍵／巨集 NVS 設定，以獨立 namespace 保存，不修改 v7 設定 blob 或分割區。每顆最多 16 步、每個 chord 最多 16 鍵、文字每段 512 UTF-8 bytes，整段 1200 bytes。
- 普通快捷鍵維持原 USB HID。巨集鍵不會同時送出舊的 HID 按鍵；只在按下緣產生一次實體事件，由明確啟用的桌面工具執行。網頁效果測試不觸發文字。
- C 主機測試通過：中文／emoji、欄位與 NUL 驗證、重複鍵拒絕、NVS 寫入失敗時保留原動作、單一 host lease、忙碌／未連接時丟棄、取走確認、逾期事件不補送。桌面工具先 ACK 再輸入，回覆不確定時不重送輸入。
- 桌面工具 8 項主機測試通過：多鍵與 Unicode 步驟、先驗證整段、ACK 失敗不輸入、等待取消、輸入失敗的按鍵釋放、Wayland 文字 stdin、本機 HTTP 的 Host／Origin／檔案範圍限制及 USB 重連錯誤清除。自動化測試使用模擬輸入；真實輸入另由上述使用者實測確認。
- USB 14 項既有／新增測試、Web Serial、WAV／PCM、自訂音效裁切／CRC、前端巨集驗證與建置測試通過。GPIO、Quad PSRAM 設定、左麥克風 slot 與固定分割區已核對。韌體映像與韌體來源保持不變，manifest 另更新已測試的主機 USB 工具來源雜湊。
- 巨集 program 的 NVS 重啟恢復、Windows／macOS／Linux 中文輸入、多個一般鍵同時輸入與多步巨集尚未實測。Linux Wayland 取決於 virtual-keyboard 協定，不能保證所有桌面環境可用。
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
