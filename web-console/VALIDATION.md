# 公開 USB 控制台 v2 驗證

2026-10-14 更新：新版按鍵工作流程搭配 `macro-studio-v8`。裝置內的既有 app.js 保持完整；公開／桌面控制台重組面板，原 control IDs 皆保留。

- 按鍵以點鍵選擇、直接擷取快捷鍵、常用動作、單鍵編輯呈現；點選編輯與效果測試分開。燈光音效、收音錄音分頁，收音進行時可從頁籤辨識。原始數值、聲道、SD 檔案及 USB 裝置設定預設收合。
- 三頁切換、Ctrl+Shift+K 擷取、快捷鍵儲存、中文／emoji／換行、巨集新增及排序，已用瀏覽器操作驗證。以本機模擬裝置確認實際 HTTP program 讀取、保存與重新載入；模擬不會開 USB 或送出系統按鍵。
- 1180 px 桌面、390 px 窄螢幕已檢查；無水平溢出。保留 Tab／方向鍵頁籤導覽、dialog 的 Esc 取消與明確標籤。
- Web Serial、原 Web PCM／WAV、自訂聲音裁切／CRC、前端 program 驗證、建置 control IDs／靜態 allowlist 測試通過；新增 C macro store、桌面工具 7 項測試通過。韌體編譯及 4 個產物、64 個來源校驗完成。
- 同日後續已完成一塊實板 v8 燒錄與獨立校驗，設定及音訊區保留；Windows 桌面工具實際連到板子，本機控制台可讀取四份巨集設定。詳見 `firmware/keyboard/VALIDATION.md`。没有自動輸入真實測試按鍵。
- 實板首次授權、巨集事件、NVS 重啟保存與三平台中文輸入尚未測試；公開網站不繞過 USB 授權，文字／多鍵／多步巨集須用桌面工具。Wayland 依賴 compositor virtual-keyboard 支援。
- 新版已部署到原 `/pcb2/`；正式十個靜態檔案、Web Serial／CSP 標頭逐項核對，排除已知 Cloudflare Analytics 注入後的 HTML 也一致。Roomly、Trip 與 PCB2 六條路由皆已核對。

2026-10-14 巨集連線排查：實機重現 Edge 公開控制台占用 USB，桌面工具無法開啟串列埠；從公開頁面釋放後，同一塊板子恢復 host heartbeat，已有的第一鍵組合設定完整保留。修正 USB 占用提示、斷線時的舊「已連接」文字與重連後未清除的錯誤，加入不含文字內容的巨集執行次數。公開頁面提供釋放 USB 並切換桌面控制台的入口；未儲存動作、錄音或傳輸時禁止切換。USB 14 項、桌面工具 8 項、瀏覽器 transport 斷線與復原、前端巨集及建置測試通過。實板本機控制台已顯示「裝置已連線」「桌面工具已就緒」；實際目標程式是否收到按鍵，仍待使用者的實體按鍵測試。

## 首次部署的歷史紀錄（0f40162）

日期：2026-10-14。網站使用 v7 的原始鍵盤介面，加上瀏覽器 USB transport；既有韌體來源與 4 個建置產物沒有修改，manifest 的 62 個來源檔雜湊仍一致。

- Web Serial 主機測試通過：分段回應、混合日誌、舊 id 排除、115200 CDC signals、HTTP status adapter、signed PCM、CRC32 標準向量、過期／奇數／錯誤 CRC 拒絕、錄音 owner、WAV EOF／錯誤 cleanup、有限冪等音效重送、一般指令不重送、排隊取消與 ROM 裝置拒絕。
- 建置測試確認原始 app.js 完整保留、相對 asset 路徑、script 載入順序、靜態檔案允許清單及非法 base path 拒絕。既有 Web PCM／WAV 與自訂音效裁切／CRC 測試、JavaScript 語法、skill 格式驗證通過。
- Cloudflare Worker `pcb2-keypad-console` 已部署 `/pcb2` 與 `/pcb2/*`；正式 HTTPS 頁面、USB 選擇區及原始六個功能面板可載入。桌面 1180 px 與窄螢幕 390 px 沒有水平溢出。展示模式明確標示合成資料，USB 連線按鈕停用。
- 正式 JS／CSS／build-info 與本機建置逐檔核對。HTML 另由 Cloudflare 插入既有 Web Analytics beacon，因此校驗需只排除已確認的 Cloudflare beacon，再比對完整頁面；網站 CSP 保留此既有 Analytics，沒有調整網域防護設定。
- Roomly 與 Trip 的四條既有 Worker 路由完整保留。Cloudflare 只部署列出的靜態資產，沒有上傳 skill、Flash 備份、私人 WAV 或 MCU 識別碼。
- **本次沒有連接實板：首次授權、真實 USB 連線／重新連線、實體按鍵／LED／喇叭與麥克風反應尚未在公開網站驗證。** 協定模擬及按鈕顯示不能替代實板測試。麥克風人聲與 SD 異常仍維持未通過狀態。

完整原始測試資料與部署紀錄保存在本機；公開摘要只保留必要的驗證結果。
