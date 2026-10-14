"use strict";
function pcb2ConnectionView({companion=false,connected=false,working=false,state=null,error="",supported=true,demo=false}) {
  if(demo)return {title:"介面體驗",description:"目前沒有控制實體鍵盤。",keyboard:"模擬鍵盤",macro:"模擬動作",macroReady:false,note:"所有按鍵與聲音都是展示資料。",issue:false,choose:"連接鍵盤",hideChoose:true};
  const version=state?.macros?.version===1, macroReady=connected&&companion&&!!state?.companion_ready;
  const inputIssue=connected&&companion?state?.companion_error||"":"";
  const keyboard=working?"鍵盤：連接中…":connected?"鍵盤：已連接":"鍵盤：未連接";
  const macro=!connected?"文字與巨集：尚未就緒":!version?"文字與巨集：需更新韌體":!companion?"文字與巨集：需桌面模式":inputIssue?"文字與巨集：需檢查":macroReady?"文字與巨集：可用":"文字與巨集：等待工具";
  const title=working?"正在連接你的鍵盤":!supported?"換到支援 USB 的瀏覽器":!connected?companion?"桌面工具正在等待鍵盤":"先接上你的四鍵鍵盤":companion?macroReady&&!inputIssue?"鍵盤與巨集已就緒":"鍵盤已連接":"鍵盤已連接，可開始設定";
  const description=companion?connected?"在這裡設定按鍵與巨集，保留桌面工具執行即可。":"插上 USB 資料線；桌面工具會自動偵測，不必在網站重複連接。":supported?"快捷鍵、燈光與錄音直接透過 USB 設定。文字與多鍵巨集請使用桌面模式。":"請使用電腦版 Chrome／Edge，或改用桌面模式。";
  const note=error||inputIssue||(working?"正在讀取鍵盤設定…":connected?companion?"切到想操作的程式，再按實體鍵；巨集會輸入到目前使用的程式。":"普通快捷鍵可直接使用；文字與多鍵巨集需切換桌面模式。":companion?"若其他網站或串列監控正連接鍵盤，請先中斷那邊的連線。":"第一次按「連接鍵盤」，選擇 PCB2 的 USB 裝置。電腦保持原本 Wi-Fi。");
  return {title,description,keyboard,macro,macroReady:macroReady&&!inputIssue,note,issue:!!(error||inputIssue),choose:working?"正在連接…":companion?"重新檢查鍵盤":connected?"更換裝置":"連接鍵盤",hideChoose:companion&&connected};
}
if(typeof module==="object"&&module.exports)module.exports={pcb2ConnectionView};
else {
window.PCB2Transport = new URLSearchParams(location.search).get("companion") === "1" && ["127.0.0.1", "localhost"].includes(location.hostname) ? new PCB2CompanionTransport() : new PCB2SerialTransport();
document.addEventListener("DOMContentLoaded", () => {
  const $ = id => document.getElementById(id), serial = navigator.serial, demo = new URLSearchParams(location.search).get("demo") === "1";
  const transport = window.PCB2Transport, companion = transport instanceof PCB2CompanionTransport; let ports = [], working = false, connectionError = "";
  $("desktop-console-help").hidden = companion || demo;
  $("offline").querySelector("span").textContent = "請使用上方 USB 連接功能。若埠被占用，先關閉 Python 預覽工具或串列監控；此網頁不需要切換 Wi-Fi。";
  $("target-help").textContent = "不用 SD 卡。請保持網頁開啟與 USB 連線，完成後下載 WAV。";
  $("device-address").textContent = "USB 控制台 · 電腦維持原本 Wi-Fi";
  function note(text, error = false) { $("serial-note").textContent = text; $("serial-note").classList.toggle("error", error); }
  function update() {
    if (transport.connected) connectionError = "";
    const supported = companion || !!serial && window.isSecureContext;
    const view=pcb2ConnectionView({companion,connected:transport.connected,working,state:transport.lastState,error:transport.connectionError||connectionError,supported,demo});
    ["serial-choose", "serial-ports", "serial-auto"].forEach(id => $(id).disabled = !supported || demo || working || transport.busy);
    $("serial-connect").disabled = !supported || demo || working || transport.busy || !ports.length;
    $("serial-disconnect").disabled = !transport.connected || working || transport.busy;
    $("serial-status").textContent = view.keyboard;
    $("serial-status").classList.toggle("active", transport.connected);
    $("macro-connection-status").textContent=view.macro;$("macro-connection-status").classList.toggle("active",view.macroReady);
    note(view.note,view.issue);
    $("serial-choose").textContent=view.choose;$("serial-choose").hidden=view.hideChoose;
    $("serial-choose").classList.toggle("primary",!transport.connected);$("serial-choose").classList.toggle("secondary",transport.connected);
    $("usb-connect-title").textContent=view.title;$("usb-connect-description").textContent=view.description;
    $("usb-mode-label").textContent=companion?"桌面模式":"瀏覽器模式";
    document.querySelector(".usb-more").hidden=companion;
    if (companion) { $("serial-ports").disabled = $("serial-connect").disabled = $("serial-disconnect").disabled = $("serial-auto").disabled = true; }
  }
  transport.onChange = update;
  try { $("serial-auto").checked = localStorage.getItem("pcb2-auto-connect") !== "false"; } catch {}
  async function refresh() {
    if (!serial || companion) return;
    ports = (await serial.getPorts()).filter(pcb2RuntimePort);
    $("serial-ports").replaceChildren(...(ports.length ? ports.map((port, i) => new Option(`ESP32-S3 USB 裝置 ${i + 1}`, String(i))) : [new Option("尚無已授權裝置", "")]));
    const selected = ports.indexOf(transport.port); if (selected >= 0) $("serial-ports").value = String(selected);
    update();
  }
  async function connect(port) {
    if (working || transport.busy) return;
    working = true; connectionError = ""; update();
    try { await transport.connect(port); await refresh(); }
    catch (error) { connectionError = error.message; note(error.message, true); }
    finally { working = false; update(); }
  }
  $("serial-choose").addEventListener("click", async () => {
    try {
      if (companion) { await connect(); return; }
      if (ports.length === 1 && !transport.connected) { await connect(ports[0]); return; }
      const port = await serial.requestPort({filters:[{usbVendorId:0x303a, usbProductId:0x4005}]});
      await connect(port);
    } catch (error) { note(error.name === "NotFoundError" ? "已取消選擇。接上板子後可再選一次。" : error.message, error.name !== "NotFoundError"); }
  });
  $("serial-connect").addEventListener("click", () => void connect(ports[Number($("serial-ports").value)]));
  $("desktop-console").addEventListener("click", async event => {
    event.preventDefault();
    if (transport.busy || working) { note("請先完成錄音、下載或音效保存，再開啟桌面控制台。",true); return; }
    try { if (transport.connected) await transport.disconnect(); location.assign($("desktop-console").href); }
    catch (error) { note(error.message,true); }
  });
  $("serial-disconnect").addEventListener("click", async () => {
    if (transport.busy) { note("請先完成錄音、下載或音效保存。", true); return; }
    await transport.disconnect(); note("USB 已中斷，已下載或留在頁面內的音檔仍可使用。");
  });
  $("serial-auto").addEventListener("change", () => { try { localStorage.setItem("pcb2-auto-connect", String($("serial-auto").checked)); } catch {} });
  if (!companion) serial?.addEventListener("connect", async () => {
    await refresh(); if (!demo && $("serial-auto").checked && !transport.port && ports.length === 1) await connect(ports[0]);
  });
  if (!companion) serial?.addEventListener("disconnect", async event => {
    if (event.target === transport.port || event.port === transport.port) { await transport.disconnect(); note("USB 已拔除，接回板子後可重新連接。"); }
    await refresh();
  });
  if (demo) { $("serial-demo").textContent = "切換到真實 USB 控制 ↗"; $("serial-demo").href = "./"; note("目前為展示模式，沒有連接或控制 USB 裝置。"); }
  else if (!companion && (!serial || !window.isSecureContext)) note("USB 直連需要電腦版 Chrome 或 Edge；這個瀏覽器可以先體驗介面，或改用桌面工具。", true);
  void refresh().then(async () => {
    if (!demo && companion) await connect();
    else if (!demo && serial && $("serial-auto").checked && ports.length === 1) await connect(ports[0]);
    else if (!demo && ports.length > 1) note("找到多個已授權裝置，請選擇一個再連接。");
  }).catch(error => note(error.message, true));
  update();
});
}
