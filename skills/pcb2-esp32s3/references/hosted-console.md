# 公開控制台與瀏覽器 USB

`web-console/` 與 `tools/build-web-console.py` 將既有 firmware Web UI 加上 Web Serial transport，產生 `dist-web/pcb2/`。原始 app.js 完整保留，以函式參數傳入 USB fetch adapter，避免公開網站與韌體介面各自分岔。修改來源 UI 後重新建置；不要手動維護 dist 副本。

正式網址 `https://demo.terry878.org/pcb2/`，Cloudflare Worker `pcb2-keypad-console` 的 `wrangler.jsonc` 只使用 `/pcb2` 與 `/pcb2/*` 路由；同網域有 Roomly 與 Trip 服務，不要為此任務改它們的路由或覆寫整個網域。

Web Serial 需要 HTTPS／localhost 與支援的桌面瀏覽器。首次 `requestPort()` 必須由使用者操作；`getPorts()` 只能取得這個網站已授權的裝置。自動連接只用唯一的 VID 303A／PID 4005 候選，再以 PCB2 state／protocol handshake 核對。多個候選由使用者選擇，不透過瀏覽器內部 API 繞過授權。

USB CDC 使用 115200 baud、DTR true、RTS false。保持 RPC id／protocol、PCM 長度、CRC、age、錄音 owner、下載 finally cleanup 與有限冪等音效重送。一般指令不因逾時重送；GUI 選項需由回傳實板狀態確認。公開網站不將 `/api/*` 發給 Cloudflare，控制與聲音留在瀏覽器和板子之間。

測試 `node tools/test-web-serial.cjs`、`python tools/test-web-console-build.py` 及相關既有 Web 測試。部署前 `python tools/build-web-console.py`，檢查只含允許的靜態檔案，再以已授權的 Wrangler 部署。不要將整個 repository、skill、韌體備份或錄音當成網站 assets；不要因網站部署而更新 PCB。

實機驗證另記錄使用的瀏覽器、授權選擇／自動重連、實板 state 與音訊結果；協定模擬不能代替這些檢查。保留麥克風與 SD 尚未通過的限制。
