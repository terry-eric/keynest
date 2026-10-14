# PCB2 網頁 USB 控制台

公開控制台使用 `firmware/keyboard/main/web` 的同一套 v7 介面，建置時加上 Web Serial 連線區。原始 app.js 完整保留，只透過函式參數提供 USB transport；不修改韌體、分割區或既有燒錄檔。

網址：`https://demo.terry878.org/pcb2/`。Cloudflare Worker 僅服務 `/pcb2` 與 `/pcb2/*`，保留同網域的 `/roomly`、`/trip`。

使用電腦版 Chrome 或 Edge，以 USB 資料線連接已燒錄支援 PCB2 RPC 的板子。第一次按「選擇 USB 裝置」，授權執行中 VID 303A／PID 4005 的裝置。之後可手動選擇已授權裝置，或自動連接唯一的已授權裝置；多個候選不會任意選擇。ROM PID 1001 不用於控制。

網頁先用 `/api/state` 核對 PCB2 協定，再提供按鍵、RGB、音效、即時麥克風、三種錄音及 WAV 下載。首次授權需要使用者操作，頁面不能繞過瀏覽器權限。若 USB 埠被 Python 預覽工具／Serial Monitor 占用，先釋放埠。此網頁的 `/api/*` 呼叫在瀏覽器轉成 USB RPC，不送往 Cloudflare；音檔在瀏覽器解碼與下載。

麥克風人聲問題、SD 不穩定及 USB 音效 CRC 根因仍未解決，見 `firmware/keyboard/VALIDATION.md`。Web Serial 主機測試不代表特定實板或瀏覽器授權已通過。

## 建置與部署

```powershell
python tools/build-web-console.py
node tools/test-web-serial.cjs
node tools/test-keypad-web.cjs
node tools/test-custom-sound-web.cjs
npx wrangler@4.151.0 deploy
```

`dist-web/` 是可再生成的唯一部署目錄，沒有備份、錄音、MCU 識別碼或 Cloudflare 憑證。`wrangler.jsonc` 使用既有 Cloudflare OAuth／環境設定，請勿把登入憑證放入 repository。另可將 `dist-web` 上傳到其他靜態 HTTPS 主機。

本機測試：`python -m http.server 8032 --bind 127.0.0.1 --directory dist-web`，開啟 `http://127.0.0.1:8032/pcb2/`。`?demo=1` 才會顯示合成資料，展示模式不會連接 USB。
