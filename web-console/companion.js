"use strict";
class PCB2CompanionTransport {
  constructor() { this.connected = false; this.busy = false; this.port = null; this.onChange = null; this.connectionError = ""; }
  async fetch(path, options = {}) {
    let response;
    try { response = await window.fetch(path, options); }
    catch (error) {
      if (path === "/api/state") { this.connected = this.busy = false; this.connectionError = "桌面工具沒有回應，請重新啟動工具。"; this.onChange?.(); }
      throw error;
    }
    if (path === "/api/state" && response.ok) {
      const state = await response.clone().json(); this.lastState = state; this.connected = true; this.connectionError = "";
      this.busy = [1,2,3].includes(state.recording?.state) || !!state.recording?.downloading || !!state.custom_sound?.uploading;
      this.onChange?.();
    } else if (path === "/api/state" && !response.ok) {
      this.connected = this.busy = false;
      this.connectionError = (await response.clone().json().catch(()=>({}))).error || "鍵盤尚未連接，請確認 USB 資料線。";
      this.onChange?.();
    }
    return response;
  }
  async connect() { const response = await this.fetch("/api/state"); if (!response.ok) throw new Error((await response.json()).error || "請接上 PCB2 的 USB 資料線。"); }
  async disconnect() { throw new Error("桌面工具正在管理 USB；請從終端機停止工具。"); }
}
