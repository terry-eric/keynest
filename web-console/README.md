# KeyNest 網頁 USB 控制台

控制台 v9 以「連接鍵盤 → 設定按鍵 → 儲存變更」為主要流程，分成按鍵、動作總表、燈光音效、收音錄音四頁。未連線時只顯示簡潔的連接卡片，隱藏尚不可用的鍵盤與編輯器；連線後縮成狀態列，顯示 USB 與文字／巨集是否可用。左側 2×2 鍵帽選擇要編輯的鍵，右側直接選快捷鍵、多鍵組合、輸入文字或連續動作。主要操作只有「儲存到鍵盤」，暫時套用收在更多裝置選項；儲存期間暫停編輯，完成後恢復操作。

維持 `firmware/keyboard/main/web/app.js` 的音效、錄音與普通快捷鍵行為，建置時重用其功能面板與 control IDs；新的 `studio.js` 負責分頁、快捷鍵擷取與巨集編輯。v8 加入獨立 NVS 巨集保存和實體鍵事件，不改分割區、原設定 blob、Flash 錄音或按鍵音儲存區。

唯一正式網址：[KeyNest 控制台](https://demo.terry878.org/keynest/)。GitHub 專案網站連結與原 [GitHub Pages](https://terry-eric.github.io/keynest/) 均導向此網址。Cloudflare Worker 只服務 `/keynest`、`/keynest/*`，已移除 `/pcb2`、`/pcb2/*` 舊路由及資源；同網域 `/roomly`、`/trip` 維持原服務。

使用電腦版 Chrome 或 Edge，以 USB 資料線連接已燒錄支援 PCB2 RPC 的板子。第一次按「連接鍵盤」，授權執行中 VID 303A／PID 4005 的裝置。之後可手動選擇已授權裝置，或自動連接唯一的已授權裝置；多個候選不會任意選擇。ROM PID 1001 不用於控制。正式控制台只有 Web Serial 直連，不提供本機工具／另一個控制台的切換入口，也不載入 companion.js。

網頁先用 `/api/state` 核對 PCB2 協定，再提供按鍵、RGB、音效、即時麥克風、三種錄音及 WAV 下載。首次授權需要使用者操作，頁面不能繞過瀏覽器權限。若 USB 埠被 Python 預覽工具／Serial Monitor 占用，先釋放埠。此網頁的 `/api/*` 呼叫在瀏覽器轉成 USB RPC，不送往 Cloudflare；音檔在瀏覽器解碼與下載。

普通快捷鍵支援直接按下擷取，並增加符號／數字鍵盤選項。多鍵組合與巨集中的每個按鍵步驟亦可直接擷取，最多 16 鍵，支援移除、清除與取消。v10 的多鍵、文字與多步巨集直接由板子執行；舊版韌體會提示更新，原有巨集不會刪除。

介面依 frontend-design 與官方 gsap-core skill 整理。GSAP 3.15.0 隨控制台提供，用於進場、分頁滑入、鍵帽彈跳、設定區切換、展開內容、巨集步驟及擷取回饋。儲存中顯示轉圈，只有成功回應才顯示綠色完成回饋；RGB 彩虹／呼吸預覽跟隨選取的燈效。右上角「動畫開啟／關閉」立即生效並記住偏好；未手動選擇時遵循系統 reduced-motion，明確開啟時使用完整動畫。切換偏好與離開頁面會清理 GSAP context，快速連點不等待動畫完成。來源、雜湊與授權連結在 `vendor/README.txt`，不需執行時存取 CDN。

麥克風人聲問題、SD 不穩定及 USB 音效 CRC 根因仍未解決，見 `firmware/keyboard/VALIDATION.md`。Web Serial 主機測試不代表特定實板或瀏覽器授權已通過。

v9 音效頁可選第 1～4 顆鍵，各自上傳／裁切最多 2 秒音檔，先試聽再存到 Flash 或 SD，並可恢復共用音效。瀏覽器及 SD 錄音上限為 10 分鐘；Flash 短錄音仍為 2.048 秒。介面以裝置實際回報上限顯示，舊版韌體不會被誤標為 10 分鐘。

## KeyNest：單鍵、動作總表與更多按法

公開網站已改名 KeyNest，GitHub repository 為 `terry-eric/keynest`，正式網址為 <https://demo.terry878.org/keynest/>，原 Pages 網址轉到同一處。Cloudflare `/pcb2/` 舊入口已移除；舊的 GitHub Pages 專案網址不會隨 repository 改名自動轉址。

- **單鍵**：可選 A、Enter 或獨立的左右 Ctrl／Shift／Alt／Win。未啟用特殊按法時，按下維持、放開釋放；例如第 1 鍵 Ctrl、第 2 鍵 A，可合成 Ctrl+A。
- **動作總表**：28 個單鍵、快捷鍵、連續動作範例、Windows 程式與媒體動作。可以搜尋／分類，選第幾顆與短按／長按／雙擊，連續安排多顆，最後一次儲存；離線也能瀏覽。
- **連續動作範例**：全選並複製、貼上並送出（Enter 可能送出訊息）、常用英文回覆。每一步可編輯。
- **開啟應用程式**：Windows 的記事本、小算盤、檔案總管，或英文 `.exe` 絕對路徑；空格自動加引號。使用 Win+R → 等待 → 選取舊內容 → 輸入路徑 → Enter，需要美式英文輸入，沒有背景程式。網站儲存不會啟動程式，須按實體鍵。
- **v11 特殊按法**：每鍵可另設長按及連按兩下，長按 300–2000ms（預設 600），雙擊間隔 150–600ms（預設 300）。短按放開後執行；啟用雙擊時再等判定間隔。長按只執行一次，不會同時送出短按或雙擊。啟用後這顆鍵改為點按動作，不再當按住的修飾鍵；需要分開 Ctrl+A 時保持特殊按法關閉。

長按／雙擊可選總表動作、自訂按鍵組合與文字，或複製上方已編輯的多步巨集／自訂程式路徑。三種按法合計仍須在每鍵 1200 UTF-8 bytes 內。舊 v10 板子仍可用一般單鍵／巨集，但網站會阻止把特殊按法儲存到不支援的韌體。

## 建置與部署

```powershell
python tools/build-web-console.py
node tools/test-web-serial.cjs
node tools/test-keypad-web.cjs
node tools/test-custom-sound-web.cjs
node tools/test-studio.cjs
node tools/test-actions.cjs
node tools/test-connection-view.cjs
python tools/test-web-console-build.py
python tools/test-macro-store.py
python tools/test-usb-macro.py
npx wrangler@4.151.0 deploy
```

`dist-web/` 是可再生成的唯一部署目錄，沒有備份、錄音、MCU 識別碼或 Cloudflare 憑證。`wrangler.jsonc` 使用既有 Cloudflare OAuth／環境設定，請勿把登入憑證放入 repository。另可將 `dist-web` 上傳到其他靜態 HTTPS 主機。

本機測試：`python -m http.server 8032 --bind 127.0.0.1 --directory dist-web`，開啟 `http://127.0.0.1:8032/keynest/`。`?demo=1` 才會顯示合成資料，展示模式不會連接 USB。

`test-macro-store.py` 使用已下載的 ESP-IDF cJSON 和 `.tools/tcc` 的 Windows host compiler。C 邏輯以實際來源編譯，替代 NVS、時鐘與鎖；其他平台可沿用同一測試 C 檔搭配自己的 host compiler。

## v10 免背景工具的文字與巨集

使用公開 HTTPS 網站、桌面版 Chrome／Edge 和 USB 資料線。首次授權 USB 後，網站讀取及保存設定；動作在板子執行，無需 Python、localhost 或常駐輸入工具。瀏覽器仍負責必要的 USB 通訊、畫面及音訊檔案處理，並不是雲端伺服器直接存取電腦的 USB。

原有 `{name,steps}` program 完全保留，新增選填 `text_mode`：`ascii`（預設）、`linux`、`macos`，同樣計入 1200-byte 限制。英文依美式鍵盤配置輸出，需切到對應英文／Unicode 輸入來源。ASCII 字母會參考主機 Caps Lock LED。Linux Unicode 模式送出 Ctrl+Shift+U／十六進位碼／Enter，需目標輸入環境支援；macOS 需先新增並切換 Unicode Hex Input，目前只接受 U+FFFF 以內字元。整段巨集執行前先檢查，未選模式的中文、macOS 模式的 emoji 會整段拒絕，避免執行前半段後才失敗。介面在儲存時也提示這些限制，原有中文內容仍可讀取。

Windows 未提供在所有軟體通用的免工具中文字串輸入方法；本版不以 Alt+X 或需要登錄檔變更的按法假裝通用支援。參考：[Unicode 官方 FAQ](https://www.unicode.org/faq/font_keyboard.html)、[Ubuntu 字元輸入](https://help.ubuntu.com/stable/ubuntu-help/tips-specialchars.html.en)、[Microsoft HexToUnicode IME](https://learn.microsoft.com/en-us/windows/win32/intl/hextounicode-ime)。Linux／macOS 系統輸入模式目前只有主機邏輯測試，尚未三平台實測。

HID 使用 report 1：8 個修飾鍵與 224-bit NKRO bitmap；report 2：6 個媒體鍵位元，同時組合最多 16 鍵不再截為 6 鍵。改為 report protocol，沒有 BIOS／UEFI boot-keyboard 相容承諾。步驟按下與放開各至少 30ms，只有 TinyUSB 接受 keyboard／consumer report 後才開始計時。按住實體鍵不重複觸發；每次執行使用獨立 program snapshot。一次執行一個巨集，忙碌時其他巨集按下會略過並計數，不排隊；普通實體按鍵中止當前巨集，避免修飾鍵混入文字。USB 斷線／暫停、2 秒無法提交 HID 或執行超過 180 秒時取消，不在重連後補送。

`/api/state.macros` 保留 version 1 與 enabled，新增 `execution:"device"`、busy、completed、failed、dropped、last_key、result。舊 host／ack lease API 回覆 invalid state，event API 無事件，避免舊背景程式重複輸入。網頁 `/api/trigger` 仍然只測試 LED／音效，不能遠端觸發電腦輸入。NVS namespace、設定 blob、Flash 錄音、每鍵音效與 v9 分割表不變；從 v9 只更新 0x10000 app。

## GitHub Pages 轉址

`.github/workflows/pages.yml` 在 main 的介面或部署來源更新時，自動檢查並發布轉址頁，也能在 Actions 手動執行。原 `https://terry-eric.github.io/keynest/` 透過 HTML refresh 與明確的連結轉到 `https://demo.terry878.org/keynest/`，不再另外提供一個 USB 控制台。GitHub repository 的網站連結也使用 Cloudflare 網址。

`python tools/build-web-console.py` 預設只產生 Cloudflare 的 `dist-web/keynest/`，並移除舊的已知 `/pcb2/` 產物與標頭；若舊目錄包含非建置檔案或指向輸出目錄外則拒絕清理。`.assetsignore` 另排除 `pcb2/`，避免舊資源被誤上傳。

GitHub Actions 使用 `python tools/build-web-console.py --out .tools/github-pages --pages-redirect`，只上傳 `.tools/github-pages/keynest/index.html`。轉址輸出須和 Cloudflare 輸出分開；不包含韌體、錄音或 USB JavaScript，也不為整個 `demo.terry878.org` 設定 Pages CNAME。Cloudflare 的控制資料仍直接經瀏覽器 USB 傳送。
