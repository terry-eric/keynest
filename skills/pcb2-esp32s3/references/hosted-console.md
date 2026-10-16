# 公開控制台與瀏覽器 USB

`web-console/` 與 `tools/build-web-console.py` 重用 firmware 的功能面板／control IDs 與完整 app.js，僅產生正式入口 `dist-web/keynest/`。`studio.html`／`studio.css`／`studio.js` 提供四個工作分頁、點鍵編輯、快捷鍵擷取與巨集編輯；原始 app.js 透過函式參數呼叫動態 transport。修改來源後重新建置，不手動維護 dist 副本。建置會清理已知舊入口產物，遇到未知檔案或目錄外連結時拒絕刪除。

正式網址 `https://demo.terry878.org/keynest/`，Cloudflare Worker `pcb2-keypad-console` 的 `wrangler.jsonc` 使用 `/keynest` 與 `/keynest/*`，`/pcb2` 舊入口已移除；GitHub Pages 只轉到 Cloudflare 正式網址；同網域有 Roomly 與 Trip 服務，不要為此任務改它們的路由或覆寫整個網域。

Web Serial 需要 HTTPS／localhost 與支援的桌面瀏覽器。首次 `requestPort()` 必須由使用者操作；`getPorts()` 只能取得這個網站已授權的裝置。自動連接只用唯一的 VID 303A／PID 4005 候選，再以 PCB2 state／protocol handshake 核對。多個候選由使用者選擇，不透過瀏覽器內部 API 繞過授權。

USB CDC 使用 115200 baud、DTR true、RTS false。保持 RPC id／protocol、PCM 長度、CRC、age、錄音 owner、下載 finally cleanup 與有限冪等音效重送。一般指令不因逾時重送；GUI 選項需由回傳實板狀態確認。公開網站不將 `/api/*` 發給 Cloudflare，控制與聲音留在瀏覽器和板子之間。

測試 `node tools/test-web-serial.cjs`、`python tools/test-web-console-build.py` 及相關既有 Web 測試。部署前 `python tools/build-web-console.py`，檢查只含允許的靜態檔案，再以已授權的 Wrangler 部署。不要將整個 repository、skill、韌體備份或錄音當成網站 assets；不要因網站部署而更新 PCB。

實機驗證另記錄使用的瀏覽器、授權選擇／自動重連、實板 state 與音訊結果；協定模擬不能代替這些檢查。保留麥克風與 SD 尚未通過的限制。

## v8／v9 歷史：主機文字與巨集

`macro_store.c` 以獨立 `pcb2_macros` NVS namespace 保存 4 份 UTF-8 program，每份 name／steps，最大 1200 bytes、16 步、每段文字 512 bytes、每個組合 16 鍵。原 keypad settings 結構及所有分割區維持相容。實體鍵按下緣產生事件，網頁 `/api/trigger` 只測燈光與聲音。

普通快捷鍵仍是獨立 HID。所有多鍵／文字／多步巨集透過明確啟用的 `tools/keypad_companion.py` 執行，以支援中文跨作業系統。`python tools/start-companion.py --enable-input` 準備本機環境，控制台網址為 `http://127.0.0.1:8029/pcb2/?companion=1`。桌面工具與 Web Serial 同時只有一方占用 CDC，不另切 Wi-Fi、不使用剪貼簿；Linux Wayland 需要 wtype 和 compositor virtual-keyboard 支援。不要聲稱已在三平台實測。

`/api/macro/0..3` 讀取，POST `/api/macro` 保存一鍵 program，host／event／ack API 用於工具。host heartbeat 與事件有效期都是 2 秒；忙碌時丟棄，取走事件後才輸入，不因 USB ACK 不確定重播巨集。維持這些語意，避免重連後輸入過期文字。NVS 只在儲存鍵時寫入，錯誤需顯示，不能自動 erase 或格式化。

適合檢查：`node tools/test-studio.cjs`、`python tools/test-macro-companion.py`、`python tools/test-macro-store.py`、公開建置／Web Serial 測試及 ESP-IDF 編譯。主機輸入用 fake backend，未經明確實機測試安排不要在使用者目前視窗自動打字。

## v10 現行：板上執行

使用單一公開 Web Serial 控制台，不啟動本機 companion。所有實體巨集由 macro_engine.c 與 usb_keyboard.c 執行；host lease API 已停用，避免重複輸入。舊 program 保留，新增選填 text_mode（ascii／linux／macos）；中文須配合系統輸入來源，Windows 尚無通用免工具模式，macOS 暫只支援 BMP。不要假稱跨平台實測通過。詳見 firmware/keyboard/README.md 的 v10 規範。執行 test-macro-store.py、test-usb-macro.py、test-studio.cjs 及連線／Web Serial／公開建置測試。更新 v9 板子只寫 app，完整保存 NVS 與音訊。

## v11 特殊按法與 KeyNest

KeyNest 使用 actions.js 的總表與單鍵映射，四分頁包含獨立動作總表。長按與雙擊儲存在 program 的 gestures，先核對 macros.gestures===1；沒有該能力不可假稱可用。未啟用時保留獨立 Ctrl／A 的 raw 按住語意，啟用後是點按動作且短按有判定延遲。參數、1200-byte 共用上限與協定見 firmware/keyboard/README.md。維持 key_gestures.c、store、USB scheduler 與 JS 的測試一致。
