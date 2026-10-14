# PCB2 網頁 USB 控制台

公開控制台以「選鍵 → 設定動作 → 儲存」為主要流程，分成按鍵、燈光音效、收音錄音三頁。按鍵圖用來選擇編輯；燈光／聲音測試使用獨立按鈕，避免點選編輯時意外套用。USB 第一次只需一個連接按鈕，已授權裝置與診斷資訊收在進階選項。

維持 `firmware/keyboard/main/web/app.js` 的音效、錄音與普通快捷鍵行為，建置時重用其功能面板與 control IDs；新的 `studio.js` 負責分頁、快捷鍵擷取與巨集編輯。v8 加入獨立 NVS 巨集保存和實體鍵事件，不改分割區、原設定 blob、Flash 錄音或按鍵音儲存區。

網址：`https://demo.terry878.org/pcb2/`。Cloudflare Worker 僅服務 `/pcb2` 與 `/pcb2/*`，保留同網域的 `/roomly`、`/trip`。

使用電腦版 Chrome 或 Edge，以 USB 資料線連接已燒錄支援 PCB2 RPC 的板子。第一次按「連接鍵盤」，授權執行中 VID 303A／PID 4005 的裝置。之後可手動選擇已授權裝置，或自動連接唯一的已授權裝置；多個候選不會任意選擇。ROM PID 1001 不用於控制。連線區分別顯示鍵盤與巨集是否可用；桌面模式自動管理 USB，已連接時不要求再次連接。

網頁先用 `/api/state` 核對 PCB2 協定，再提供按鍵、RGB、音效、即時麥克風、三種錄音及 WAV 下載。首次授權需要使用者操作，頁面不能繞過瀏覽器權限。若 USB 埠被 Python 預覽工具／Serial Monitor 占用，先釋放埠。此網頁的 `/api/*` 呼叫在瀏覽器轉成 USB RPC，不送往 Cloudflare；音檔在瀏覽器解碼與下載。

普通快捷鍵支援直接按下擷取，並增加符號／數字鍵盤選項。多鍵組合與巨集中的每個按鍵步驟亦可直接擷取，最多 16 鍵，支援移除、清除與取消。多鍵組合、中文文字和多步巨集需要 v8 板子韌體及[桌面工具](COMPANION.md)。Windows 實體 Ctrl+C 已由使用者確認；中文、多步及 macOS／Linux 輸入仍待實測。舊韌體可繼續改普通快捷鍵，巨集保存會清楚提示更新。

介面依 frontend-design 與官方 gsap-core skill 整理。GSAP 3.15.0 隨控制台提供，擷取回饋尊重 reduced-motion；來源、雜湊與授權連結在 `vendor/README.txt`，不需執行時存取 CDN。

麥克風人聲問題、SD 不穩定及 USB 音效 CRC 根因仍未解決，見 `firmware/keyboard/VALIDATION.md`。Web Serial 主機測試不代表特定實板或瀏覽器授權已通過。

## 建置與部署

```powershell
python tools/build-web-console.py
node tools/test-web-serial.cjs
node tools/test-keypad-web.cjs
node tools/test-custom-sound-web.cjs
node tools/test-studio.cjs
node tools/test-connection-view.cjs
node tools/test-companion-transport.cjs
python tools/test-web-console-build.py
python tools/test-macro-companion.py
python tools/test-macro-store.py
npx wrangler@4.151.0 deploy
```

`dist-web/` 是可再生成的唯一部署目錄，沒有備份、錄音、MCU 識別碼或 Cloudflare 憑證。`wrangler.jsonc` 使用既有 Cloudflare OAuth／環境設定，請勿把登入憑證放入 repository。另可將 `dist-web` 上傳到其他靜態 HTTPS 主機。

本機測試：`python -m http.server 8032 --bind 127.0.0.1 --directory dist-web`，開啟 `http://127.0.0.1:8032/pcb2/`。`?demo=1` 才會顯示合成資料，展示模式不會連接 USB。

`test-macro-store.py` 使用已下載的 ESP-IDF cJSON 和 `.tools/tcc` 的 Windows host compiler。C 邏輯以實際來源編譯，替代 NVS、時鐘與鎖；其他平台可沿用同一測試 C 檔搭配自己的 host compiler。
