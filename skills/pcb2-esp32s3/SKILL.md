---
name: pcb2-esp32s3
description: "開發、更新與診斷 PCB2 四鍵 ESP32-S3 板子的 ESP-IDF 韌體、board library、USB HID／Web App、RGB、喇叭、麥克風及 Flash／SD 音訊功能。適用這塊 PCB2 的實際接腳與硬體限制，不用於其他 ESP32 板子的通用教學。"
---

# PCB2 ESP32-S3

協助維護 PCB2 四鍵鍵盤專案，沿用原生 ESP-IDF 與現有 `pcb2_board` component。以當前程式、硬體證據和實際測量決定修改，避免重複猜測接腳或聲道。

## 先定位專案與需求

- 在目前工作區或使用者指定路徑找到 `firmware/components/pcb2_board/include/pcb2_pins.h`、`firmware/keyboard/main/` 與 `tools/preview-keypad.py`。Repository 是 `terry-eric/pcb2-esp32s3-keypad`；不要將技能安裝目錄當成程式工作區，也不要寫死其他電腦的絕對路徑。
- 讀取適用的 AGENTS.md、Git 狀態、目前 `firmware/artifacts/keyboard/manifest.json` 及 `firmware/keyboard/VALIDATION.md`。下載專案、建置、更新 PCB 與 PCB 設計修改是不同工作；按使用者這次要求執行。
- 執行下方唯讀 helper 可取得目前接腳、分割區及產物校驗。建置後 manifest 可能未更新，校驗失敗時先辨認過期紀錄或產物差異。

將 `SKILL_ROOT` 與 `REPOSITORY_ROOT` 換成實際目錄：

```powershell
python "SKILL_ROOT/scripts/inspect_board.py" --repo "REPOSITORY_ROOT" --verify-sources
```

## 按工作讀取參考

- 接腳、I²S、RGB、PSRAM 或 board component 修改：讀 [硬體與程式邊界](references/hardware.md)，並核對當前 header／實作。
- 建置、USB／Web 控制、錄音、自訂按鍵音或韌體更新：讀 [開發與更新流程](references/workflows.md)。
- 公開網頁 USB 直連或 Cloudflare 部署：讀 [公開控制台](references/hosted-console.md)。
- 人聲太小、只有吹氣有聲、波形／USB／SD 異常：讀 [診斷與證據](references/diagnostics.md)。

## 保留這塊板子的實際限制

- 電腦維持原本 Wi-Fi，優先用 USB CDC 與本機 Web App。不要為了測試自行切換至板子 AP；使用者明確要求 Wi-Fi 測試時才使用該方式。
- U10 實際回報為 ZTS6672，歷史原理圖標示 ICS-43434。2026-10-13 的測量為左 slot 有資料、右 slot 全零，人聲收音尚未通過；型號建置標示、USB 傳輸成功或自動放大的波形都不能證明聲學功能正常。
- 內建音訊儲存是 SPI Flash，沒有已確認的獨立 EEPROM 音訊晶片。瀏覽器、Flash、SD 是可選目的地，功能需能免 SD 使用。
- 實體 SD 插槽曾回報方向不便插卡，後續同板有成功測試與間歇讀卡異常。先確認這次實板狀態，避免把任何一次結果當成所有版本的結論。
- 更新韌體時保留 NVS、Flash 錄音與按鍵音；核對目前及目標分割區，避免全片 erase。不要因建立或使用這個 skill 自行燒錄、格式化 SD 或改 PCB。
- 私人 WAV、完整 Flash／NVS 備份、裝置識別碼與本機機密只留在本機；GitHub 發布使用程式、產物雜湊及必要的驗證摘要。

## 完成與紀錄

按修改選擇現有測試，區分主機測試、交叉編譯、燒錄校驗與實際硬體反應。更新相關驗證文件，記錄未解決項目。只有使用者要求或目前任務已授權時，才提交或推送 GitHub；技能本身不授予外部寫入權限。
