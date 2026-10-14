"use strict";
class PCB2CompanionTransport {
  constructor() { this.connected = false; this.busy = false; this.port = null; this.onChange = null; }
  async fetch(path, options = {}) {
    const response = await window.fetch(path, options);
    if (path === "/api/state" && response.ok) {
      const state = await response.clone().json(); this.connected = true;
      this.busy = [1,2,3].includes(state.recording?.state) || !!state.recording?.downloading || !!state.custom_sound?.uploading;
      this.onChange?.();
    } else if (path === "/api/state" && !response.ok) { this.connected = false; this.onChange?.(); }
    return response;
  }
  async connect() { const response = await this.fetch("/api/state"); if (!response.ok) throw new Error((await response.json()).error || "請接上 PCB2 的 USB 資料線。"); }
  async disconnect() { throw new Error("桌面工具正在管理 USB；請從終端機停止工具。"); }
}
