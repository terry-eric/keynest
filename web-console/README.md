# PCB2 網頁 USB 控制台

控制台 v5 以「連接鍵盤 → 設定按鍵 → 儲存變更」為主要流程，分成按鍵、燈光音效、收音錄音三頁。未連線時先提供連接引導；連線後縮成狀態列，顯示 USB 與文字／巨集是否可用。左側 2×2 鍵帽選擇要編輯的鍵，右側直接選快捷鍵、多鍵組合、輸入文字或連續動作。主要操作只有「儲存到鍵盤」，暫時套用收在更多裝置選項；儲存期間暫停編輯，完成後恢復操作。

維持 `firmware/keyboard/main/web/app.js` 的音效、錄音與普通快捷鍵行為，建置時重用其功能面板與 control IDs；新的 `studio.js` 負責分頁、快捷鍵擷取與巨集編輯。v8 加入獨立 NVS 巨集保存和實體鍵事件，不改分割區、原設定 blob、Flash 錄音或按鍵音儲存區。

網址：`https://demo.terry878.org/pcb2/`。Cloudflare Worker 僅服務 `/pcb2` 與 `/pcb2/*`，保留同網域的 `/roomly`、`/trip`。

使用電腦版 Chrome 或 Edge，以 USB 資料線連接已燒錄支援 PCB2 RPC 的板子。第一次按「連接鍵盤」，授權執行中 VID 303A／PID 4005 的裝置。之後可手動選擇已授權裝置，或自動連接唯一的已授權裝置；多個候選不會任意選擇。ROM PID 1001 不用於控制。桌面模式自動管理 USB，已連接時不要求再次連接。公開網站點「切換桌面模式」會先釋放 USB 並關閉該網站的自動連接，避免下次開頁面搶走桌面工具的連線；需要時可在連線設定重新開啟。

網頁先用 `/api/state` 核對 PCB2 協定，再提供按鍵、RGB、音效、即時麥克風、三種錄音及 WAV 下載。首次授權需要使用者操作，頁面不能繞過瀏覽器權限。若 USB 埠被 Python 預覽工具／Serial Monitor 占用，先釋放埠。此網頁的 `/api/*` 呼叫在瀏覽器轉成 USB RPC，不送往 Cloudflare；音檔在瀏覽器解碼與下載。

普通快捷鍵支援直接按下擷取，並增加符號／數字鍵盤選項。多鍵組合與巨集中的每個按鍵步驟亦可直接擷取，最多 16 鍵，支援移除、清除與取消。多鍵組合、中文文字和多步巨集需要 v8 板子韌體及[桌面工具](COMPANION.md)。Windows 實體 Ctrl+C 已由使用者確認；中文、多步及 macOS／Linux 輸入仍待實測。舊韌體可繼續改普通快捷鍵，巨集保存會清楚提示更新。

介面依 frontend-design 與官方 gsap-core skill 整理。GSAP 3.15.0 隨控制台提供，用於進場、分頁滑入、鍵帽彈跳、設定區切換、展開內容、巨集步驟及擷取回饋。儲存中顯示轉圈，只有成功回應才顯示綠色完成回饋；RGB 彩虹／呼吸預覽跟隨選取的燈效。右上角「動畫開啟／關閉」立即生效並記住偏好；未手動選擇時遵循系統 reduced-motion，明確開啟時使用完整動畫。切換偏好與離開頁面會清理 GSAP context，快速連點不等待動畫完成。來源、雜湊與授權連結在 `vendor/README.txt`，不需執行時存取 CDN。

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
