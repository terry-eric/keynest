"use strict";

class PCB2SerialError extends Error {
  constructor(status, message) { super(message); this.name = "PCB2SerialError"; this.status = status; }
}
function pcb2CRC32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; ++bit) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function pcb2Binary(data) {
  try {
    if (typeof data.pcm !== "string" || data.pcm.length % 4 || !/^[A-Za-z0-9+/]*={0,2}$/.test(data.pcm)) throw new Error();
    const text = atob(data.pcm), bytes = Uint8Array.from(text, c => c.charCodeAt(0));
    if (!Number.isInteger(data.bytes) || data.bytes !== bytes.length || bytes.length > 2048 ||
        !Number.isInteger(data.crc32) || data.crc32 < 0 || data.crc32 > 0xffffffff || pcb2CRC32(bytes) !== data.crc32) throw new Error();
    return bytes;
  } catch { throw new PCB2SerialError(502, "USB 音訊長度或 CRC 檢查失敗，請重新操作。"); }
}
function pcb2Integer(data, key, maximum = Number.MAX_SAFE_INTEGER) {
  if (!Number.isSafeInteger(data[key]) || data[key] < 0 || data[key] > maximum) throw new PCB2SerialError(502, "USB 音訊資訊不正確。");
  return String(data[key]);
}
function pcb2RuntimePort(port) {
  const info = port.getInfo();
  return info.usbVendorId === 0x303a && info.usbProductId === 0x4005;
}

class PCB2SerialTransport {
  constructor({timeout = 2000, onChange = () => {}} = {}) {
    this.timeout = timeout; this.onChange = onChange; this.sequence = 0; this.tail = Promise.resolve();
    this.port = null; this.reader = null; this.writer = null; this.pending = null; this.connected = false;
    this.closing = false; this.lastState = null; this.recordOwner = null; this.uploadOwner = null; this.fileActive = false;
  }
  get busy() {
    return this.fileActive || !!this.recordOwner || !!this.uploadOwner ||
      [1, 2, 3].includes(this.lastState?.recording?.state) || !!this.lastState?.custom_sound?.uploading;
  }
  async connect(port) {
    if (!pcb2RuntimePort(port)) throw new PCB2SerialError(400, "請選擇執行中的 ESP32-S3 USB 裝置，下載模式不能控制鍵盤。");
    if (this.busy) throw new PCB2SerialError(409, "請先完成錄音、下載或音效保存，再切換 USB 裝置。");
    if (this.port) await this.disconnect();
    try {
      await port.open({baudRate:115200, bufferSize:16384});
      this.port = port; this.closing = false;
      // Native USB CDC uses DTR for its session; do not use 1200 baud or toggle RTS.
      await port.setSignals({dataTerminalReady:true, requestToSend:false});
      this.reader = port.readable.getReader(); this.writer = port.writable.getWriter();
      this.readTask = this.readLoop(this.reader);
      const state = await this.rpc("/api/state");
      if (state.device !== "PCB2 Keypad" || !state.config || !state.recording) throw new PCB2SerialError(502, "這個 USB 裝置不是支援控制協定的 PCB2 鍵盤。");
      this.lastState = state; this.connected = true; this.onChange(); return state;
    } catch (error) {
      await this.disconnect();
      if (error instanceof PCB2SerialError) throw error;
      throw new PCB2SerialError(503, "無法開啟 USB，請關閉 Python 預覽或串列監控，再重新連接。");
    }
  }
  finish(error, data) {
    const pending = this.pending; if (!pending) return;
    this.pending = null; clearTimeout(pending.timer);
    pending.signal?.removeEventListener("abort", pending.abort);
    if (error) pending.reject(error); else pending.resolve(data);
  }
  consume(line) {
    if (!line.startsWith("PCB2_RPC ")) return;
    let response;
    try { response = JSON.parse(line.slice(9)); } catch { this.finish(new PCB2SerialError(502, "USB 回應格式損壞。")); return; }
    if (!response || typeof response !== "object" || !Number.isInteger(response.id) || response.id !== this.pending?.id) return;
    if (response.protocol !== 1 || !Number.isInteger(response.status)) { this.finish(new PCB2SerialError(502, "USB 控制協定版本不相容。")); return; }
    if (response.status !== 200) {
      const status = response.status >= 400 && response.status <= 599 ? response.status : 502;
      this.finish(new PCB2SerialError(status, typeof response.error === "string" ? response.error : "裝置拒絕操作。")); return;
    }
    if (!response.data || typeof response.data !== "object" || Array.isArray(response.data)) { this.finish(new PCB2SerialError(502, "USB 回應資料不正確。")); return; }
    this.finish(null, response.data);
  }
  async readLoop(reader) {
    const decoder = new TextDecoder(); let line = "", discard = false;
    try {
      for (;;) {
        const {value, done} = await reader.read(); if (done) break;
        for (const char of decoder.decode(value, {stream:true})) {
          if (char === "\n") { if (!discard) this.consume(line.replace(/\r$/, "")); line = ""; discard = false; }
          else if (!discard) {
            line += char;
            if (line.length > 8192) { this.finish(new PCB2SerialError(502, "USB 回應超過大小上限。")); line = ""; discard = true; }
          }
        }
      }
    } catch (error) { if (!this.closing) this.finish(new PCB2SerialError(503, "USB 已中斷，請重新連接。")); }
    finally {
      reader.releaseLock(); if (this.reader === reader) this.reader = null;
      if (!this.closing) {
        this.connected = false; this.finish(new PCB2SerialError(503, "USB 已中斷，請重新連接。")); this.onChange();
        void this.disconnect();
      }
    }
  }
  async disconnect() {
    this.connected = false; this.closing = true;
    this.finish(new PCB2SerialError(503, "USB 連線已結束。"));
    try { await this.reader?.cancel(); } catch {}
    try { await this.readTask; } catch {}
    try { this.writer?.releaseLock(); } catch {}
    this.writer = null;
    const port = this.port; this.port = null;
    try { await port?.close(); } catch {}
    this.recordOwner = null; this.uploadOwner = null; this.fileActive = false; this.lastState = null; this.onChange();
  }
  exclusive(callback, signal) {
    const operation = this.tail.catch(() => {}).then(() => { signal?.throwIfAborted(); return callback(); });
    this.tail = operation.catch(() => {}); return operation;
  }
  rpc(path, body, options = {}) { return this.exclusive(() => this.wire(path, body, options), options.signal); }
  wire(path, body, {signal, rawBody, timeout = this.timeout} = {}) {
    signal?.throwIfAborted();
    if (!this.port || !this.writer || this.closing) return Promise.reject(new PCB2SerialError(503, "請先連接 PCB2 USB 裝置。"));
    const id = this.sequence = this.sequence % 2147483647 + 1;
    const envelope = rawBody === undefined ? JSON.stringify({id, path, ...(body === undefined ? {} : {body})}) :
      `{"id":${id},"path":${JSON.stringify(path)},"body":${rawBody}}`;
    const bytes = new TextEncoder().encode("PCB2? " + envelope + "\n");
    if (bytes.length > 1700) return Promise.reject(new PCB2SerialError(413, "USB 指令超過大小上限。"));
    return new Promise((resolve, reject) => {
      const abort = () => this.finish(signal.reason || new DOMException("操作已取消", "AbortError"));
      const timer = setTimeout(() => this.finish(new PCB2SerialError(503, "USB 未回應；請確認已燒錄支援 USB 控制的韌體。")), timeout);
      this.pending = {id, resolve, reject, timer, signal, abort}; signal?.addEventListener("abort", abort, {once:true});
      this.writer.write(bytes).catch(() => {
        if (this.pending?.id === id) this.finish(new PCB2SerialError(503, "USB 傳送中斷。"));
      });
    });
  }
  async command(path, body, options = {}) {
    for (let attempt = 0; attempt < 5; ++attempt) {
      try { return await this.rpc(path, body, options); }
      catch (error) {
        const retryable = ["/api/sound/begin", "/api/sound/chunk", "/api/sound/commit"].includes(path) &&
          error instanceof PCB2SerialError && ((error.status === 503 && error.message.startsWith("USB 未回應")) ||
            (path === "/api/sound/chunk" && error.message === "ESP_ERR_INVALID_CRC"));
        if (!retryable || attempt === 4 || options.signal?.aborted) throw error;
        await new Promise(resolve => setTimeout(resolve, 100));
      }
    }
  }
  async download(selected, signal) {
    if (selected !== null && !/^(?:REC|KEY)[0-9]{5}\.WAV$/.test(selected)) throw new PCB2SerialError(400, "SD 音訊檔名不正確。");
    return this.exclusive(async () => {
      let began = false; this.fileActive = true; this.onChange();
      try {
        const info = await this.wire("/api/file/begin", selected === null ? undefined : {name:selected}, {signal}); began = true;
        if (typeof info.filename !== "string" || !/^(?:INTERNAL|(?:REC|KEY)[0-9]{5})\.WAV$/.test(info.filename)) throw new PCB2SerialError(502, "裝置錄音檔名不正確。");
        const chunks = []; let count = 0;
        for (;;) {
          const bytes = pcb2Binary(await this.wire("/api/file/read", undefined, {signal}));
          if (!bytes.length) break;
          count += bytes.length; if (count > 44 + 16000 * 600 * 2) throw new PCB2SerialError(502, "錄音超過大小上限。");
          chunks.push(bytes);
        }
        return new Response(new Blob(chunks, {type:"audio/wav"}), {headers:{"Content-Type":"audio/wav", "Content-Disposition":`attachment; filename="${info.filename}"`}});
      } finally {
        if (began) try { await this.wire("/api/file/end"); } catch {}
        this.fileActive = false; this.onChange();
      }
    }, signal);
  }
  async fetch(path, init = {}) {
    try {
      if (!this.connected) throw new PCB2SerialError(503, "請先連接 PCB2 USB 裝置。");
      if (path === "/api/recording.wav" || path.startsWith("/api/sd/recording.wav?")) {
        const selected = path === "/api/recording.wav" ? null : new URL(path, "https://pcb2.invalid").searchParams.get("name");
        return await this.download(selected, init.signal);
      }
      const body = init.body === undefined ? undefined : JSON.parse(init.body);
      if (body !== undefined && (!body || typeof body !== "object" || Array.isArray(body))) throw new PCB2SerialError(400, "請使用 JSON object。");
      const options = {signal:init.signal, rawBody:init.body};
      let argumentsBody = body;
      if (path === "/api/audio") { argumentsBody = {owner:new Headers(init.headers).get("X-PCB2-Recording") || ""}; delete options.rawBody; }
      if (path === "/api/sound/chunk") {
        const bytes = pcb2Binary({...body, bytes:typeof body?.pcm === "string" ? atob(body.pcm).length : -1});
        if (!bytes.length || bytes.length > 1024 || bytes.length % 2) throw new PCB2SerialError(400, "音效區塊大小不正確。");
      }
      const data = await this.command(path, argumentsBody, options);
      if (path === "/api/state") { this.lastState = data; this.onChange(); }
      if (path === "/api/record" && body?.action === "start") {
        this.lastState = {...this.lastState, recording:{...this.lastState?.recording, state:1}};
        if (body.target === "browser") this.recordOwner = body.owner;
        this.onChange();
      }
      if (path === "/api/sound/begin") this.uploadOwner = body.owner;
      if (["/api/sound/commit", "/api/sound/cancel"].includes(path)) this.uploadOwner = null;
      if (path === "/api/microphone" || path === "/api/audio") {
        const bytes = pcb2Binary(data); if (bytes.length % 2) throw new PCB2SerialError(502, "PCM 長度不正確。");
        const headers = {"Content-Type":"application/octet-stream", "X-PCB2-Frames":pcb2Integer(data, "frames")};
        if (path === "/api/microphone") {
          if (!bytes.length || data.rate !== 16000 || data.frames < bytes.length / 2) throw new PCB2SerialError(502, "麥克風視窗不正確。");
          headers["X-PCB2-Sample-Rate"] = "16000"; headers["X-PCB2-Age-Ms"] = pcb2Integer(data, "age_ms", 1000);
        } else {
          headers["X-PCB2-State"] = pcb2Integer(data, "state", 4); headers["X-PCB2-Dropped"] = pcb2Integer(data, "dropped");
          if ([0, 4].includes(data.state) && !bytes.length) this.recordOwner = null;
        }
        return new Response(bytes, {headers});
      }
      const payload = path === "/api/state" ? {...data, transport:"usb", local_preview:false} : data;
      return new Response(JSON.stringify(payload), {headers:{"Content-Type":"application/json"}});
    } catch (error) {
      if (init.signal?.aborted) throw init.signal.reason;
      const status = error instanceof PCB2SerialError ? error.status : 502;
      return new Response(JSON.stringify({error:error.message || "USB 操作失敗。"}), {status, headers:{"Content-Type":"application/json"}});
    }
  }
}
if (typeof module === "object" && module.exports) module.exports = {PCB2SerialTransport, PCB2SerialError, pcb2CRC32, pcb2Binary, pcb2RuntimePort};
