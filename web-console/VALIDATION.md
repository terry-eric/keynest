# 公開 USB 控制台驗證

日期：2026-10-14。網站使用 v7 的原始鍵盤介面，加上瀏覽器 USB transport；既有韌體來源與 4 個建置產物沒有修改，manifest 的 62 個來源檔雜湊仍一致。

- Web Serial 主機測試通過：分段回應、混合日誌、舊 id 排除、115200 CDC signals、HTTP status adapter、signed PCM、CRC32 標準向量、過期／奇數／錯誤 CRC 拒絕、錄音 owner、WAV EOF／錯誤 cleanup、有限冪等音效重送、一般指令不重送、排隊取消與 ROM 裝置拒絕。
- 建置測試確認原始 app.js 完整保留、相對 asset 路徑、script 載入順序、靜態檔案允許清單及非法 base path 拒絕。既有 Web PCM／WAV 與自訂音效裁切／CRC 測試、JavaScript 語法、skill 格式驗證通過。
- Cloudflare Worker `pcb2-keypad-console` 已部署 `/pcb2` 與 `/pcb2/*`；正式 HTTPS 頁面、USB 選擇區及原始六個功能面板可載入。桌面 1180 px 與窄螢幕 390 px 沒有水平溢出。展示模式明確標示合成資料，USB 連線按鈕停用。
- 正式 JS／CSS／build-info 與本機建置逐檔核對。HTML 另由 Cloudflare 插入既有 Web Analytics beacon，因此校驗需只排除已確認的 Cloudflare beacon，再比對完整頁面；網站 CSP 保留此既有 Analytics，沒有調整網域防護設定。
- Roomly 與 Trip 的四條既有 Worker 路由完整保留。Cloudflare 只部署列出的靜態資產，沒有上傳 skill、Flash 備份、私人 WAV 或 MCU 識別碼。
- **本次沒有連接實板：首次授權、真實 USB 連線／重新連線、實體按鍵／LED／喇叭與麥克風反應尚未在公開網站驗證。** 協定模擬及按鈕顯示不能替代實板測試。麥克風人聲與 SD 異常仍維持未通過狀態。

完整原始測試資料與部署紀錄保存在本機；公開摘要只保留必要的驗證結果。
