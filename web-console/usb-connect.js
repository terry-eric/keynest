"use strict";

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
    ["serial-choose", "serial-ports", "serial-auto"].forEach(id => $(id).disabled = !supported || demo || working || transport.busy);
    $("serial-connect").disabled = !supported || demo || working || transport.busy || !ports.length;
    $("serial-disconnect").disabled = !transport.connected || working || transport.busy;
    $("serial-status").textContent = demo ? "介面體驗" : transport.connected ? "已連接" : working ? "連接中…" : "尚未連接";
    $("serial-status").classList.toggle("active", transport.connected);
    if (!demo) note(transport.connectionError || connectionError || (working ? "正在確認 PCB2 USB 裝置…" : transport.connected ? transport.busy ? "錄音或檔案操作中，完成後可切換裝置。" : companion ? "桌面工具已連接鍵盤；文字與巨集會輸入到目前使用的程式。" : "USB 已連接。文字與多鍵巨集請改用下方桌面控制台。" : "尚未連接鍵盤；接上 USB 資料線後再連接。"), !!(transport.connectionError || connectionError));
    $("serial-choose").textContent = transport.connected ? "切換鍵盤" : working ? "正在連接…" : "連接鍵盤";
    $("usb-connect-title").textContent = demo ? "先試試怎麼設定" : transport.connected ? "鍵盤已連接" : "先連接你的鍵盤";
    $("usb-connect-description").textContent = transport.connected ? "選一顆鍵，開始設定常用操作。" : "插上 USB 資料線，第一次連接時選擇 PCB2 裝置。";
    if (companion) { $("serial-choose").textContent = "重新連接鍵盤"; $("serial-ports").disabled = $("serial-connect").disabled = $("serial-disconnect").disabled = $("serial-auto").disabled = true; $("usb-connect-description").textContent = "桌面工具管理 USB；請保持工具執行，公開網站的 USB 連線需先中斷。"; }
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
