"use strict";
const assert = require("node:assert/strict");
const {PCB2SerialTransport, pcb2CRC32, pcb2Binary} = require("../web-console/usb-serial.js");
const state = () => ({device:"PCB2 Keypad", config:{mic_gain:8}, recording:{state:0}, audio_ready:true});
const binary = bytes => ({pcm:Buffer.from(bytes).toString("base64"), bytes:bytes.length, crc32:pcb2CRC32(bytes)});

class Port {
  constructor(reply = () => ({data:state()})) { this.reply = reply; this.commands = []; this.raw = []; this.opens = []; this.signals = []; }
  getInfo() { return {usbVendorId:0x303a, usbProductId:0x4005}; }
  async open(options) {
    this.opens.push(options);
    this.readable = new ReadableStream({start:controller => this.input = controller});
    this.writable = new WritableStream({write:raw => {
      const text = new TextDecoder().decode(raw), request = JSON.parse(text.slice(6));
      this.raw.push(text); this.commands.push(request);
      const reply = this.reply(request);
      if (reply !== null) this.respond(request.id, reply);
    }});
  }
  respond(id, reply) {
    const stale = 'PCB2_RPC {"id":0,"protocol":1,"status":200,"data":{}}\n';
    const response = {id, protocol:1, status:200, ...reply};
    const bytes = new TextEncoder().encode("I (1) keypad: diagnostic log\r\n" + stale + "PCB2_RPC " + JSON.stringify(response) + "\r\n");
    for (let offset = 0; offset < bytes.length; offset += 17) this.input.enqueue(bytes.slice(offset, offset + 17));
  }
  async setSignals(signals) { this.signals.push(signals); }
  async close() { this.closed = true; }
}
async function device(reply) {
  const port = new Port(request => request.path === "/api/state" ? {data:state()} : reply(request));
  const transport = new PCB2SerialTransport({timeout:35}); await transport.connect(port);
  return {port, transport};
}
async function check(name, task) { await task(); console.log("PASS:", name); }
(async () => {
  await check("fragmented RPC, mixed logs, stale IDs, 115200 CDC signals, JSON and error status", async () => {
    const {port, transport} = await device(request => request.path === "/api/config" ? {data:{ok:true}} : {status:409, error:"ESP_ERR_INVALID_STATE"});
    try {
      assert.deepEqual(port.opens, [{baudRate:115200, bufferSize:16384}]);
      assert.deepEqual(port.signals, [{dataTerminalReady:true, requestToSend:false}]);
      const response = await transport.fetch("/api/config", {body:JSON.stringify({mic_gain:8})});
      assert.equal(response.status, 200); assert.deepEqual(await response.json(), {ok:true});
      assert.equal((await transport.fetch("/api/trigger", {body:'{"key":0}'})).status, 409);
      assert.equal(port.commands.filter(c => c.path === "/api/trigger").length, 1);
      const live = await (await transport.fetch("/api/state")).json(); assert.equal(live.transport, "usb"); assert.equal(live.local_preview, false);
    } finally { await transport.disconnect(); }
    assert.equal(port.closed, true);
  });
  await check("signed PCM, real CRC32 vector, owner metadata, stale/odd/bad CRC rejection", async () => {
    assert.equal(pcb2CRC32(new TextEncoder().encode("123456789")), 0xcbf43926);
    const pcm = Buffer.alloc(8); [-32768, -120, 454, 32767].forEach((v,i) => pcm.writeInt16LE(v,i*2));
    let payload = {...binary(pcm), rate:16000, frames:4000, age_ms:2};
    const {port, transport} = await device(request => ({data:request.path === "/api/audio" ? {...binary(new Uint8Array()), state:0, frames:4000, dropped:0} : payload}));
    try {
      const response = await transport.fetch("/api/microphone"); assert.equal(response.headers.get("X-PCB2-Sample-Rate"), "16000");
      assert.deepEqual(Buffer.from(await response.arrayBuffer()), pcm);
      for (const change of [{crc32:0}, {age_ms:1001}, {frames:1}, {rate:48000}, binary(new Uint8Array()), binary(new Uint8Array([1]))]) {
        payload = {...binary(pcm), rate:16000, frames:4000, age_ms:2, ...change};
        assert.equal((await transport.fetch("/api/microphone")).status, 502);
      }
      const audio = await transport.fetch("/api/audio", {headers:{"X-PCB2-Recording":"0123456789abcdef"}});
      assert.equal(audio.headers.get("X-PCB2-State"), "0"); assert.equal(port.commands.at(-1).body.owner, "0123456789abcdef");
      assert.throws(() => pcb2Binary({...binary(pcm), pcm:"!!!="}));
    } finally { await transport.disconnect(); }
  });
  await check("WAV EOF and file session cleanup after CRC failure", async () => {
    let corrupt = false, chunk = 0;
    const {port, transport} = await device(request => {
      if (request.path === "/api/file/begin") { chunk = 0; return {data:{filename:"INTERNAL.WAV"}}; }
      if (request.path === "/api/file/read") return {data:corrupt ? {...binary(Buffer.from("RIFF")), crc32:0} : binary(chunk++ ? new Uint8Array() : Buffer.from("RIFF"))};
      return {data:{ok:true}};
    });
    try {
      const response = await transport.fetch("/api/recording.wav"); assert.equal(await response.text(), "RIFF");
      assert.match(response.headers.get("Content-Disposition"), /INTERNAL.WAV/);
      corrupt = true; assert.equal((await transport.fetch("/api/recording.wav")).status, 502);
      assert.equal(port.commands.at(-1).path, "/api/file/end"); assert.equal(transport.fileActive, false);
      const before = port.commands.length;
      assert.equal((await transport.fetch("/api/sd/recording.wav?name=../NVS")).status, 400); assert.equal(port.commands.length, before);
    } finally { await transport.disconnect(); }
  });
  await check("bounded identical sound retries; trigger is never replayed", async () => {
    const {port, transport} = await device(request => ({status:request.path === "/api/sound/chunk" ? 400 : 503, error:request.path === "/api/sound/chunk" ? "Invalid chunk" : "USB 未回應"}));
    try {
      const body = {owner:"0123456789abcdef", storage:"flash", frames:1, crc32:0};
      assert.equal((await transport.fetch("/api/sound/begin", {body:JSON.stringify(body)})).status, 503);
      const attempts = port.commands.filter(c => c.path === "/api/sound/begin"); assert.equal(attempts.length, 5);
      attempts.forEach(c => assert.deepEqual(c.body, body));
      assert.equal((await transport.fetch("/api/trigger", {body:'{"key":0}'})).status, 503);
      assert.equal(port.commands.filter(c => c.path === "/api/trigger").length, 1);
      const raw = {...binary(new Uint8Array([1,2])), owner:body.owner, offset:0};
      assert.equal((await transport.fetch("/api/sound/chunk", {body:JSON.stringify(raw)})).status, 400);
      assert.equal(port.commands.filter(c => c.path === "/api/sound/chunk").length, 1);
    } finally { await transport.disconnect(); }
  });
  await check("timeout and aborted queued mutations are not sent twice", async () => {
    const {port, transport} = await device(() => null);
    try {
      const blocked = transport.fetch("/api/trigger", {body:'{"key":0}'});
      const controller = new AbortController();
      const queued = transport.fetch("/api/config", {body:'{"mic_gain":8}', signal:controller.signal});
      controller.abort(); const rejected = assert.rejects(queued, {name:"AbortError"});
      assert.equal((await blocked).status, 503); await rejected;
      assert.equal(port.commands.filter(c => c.path === "/api/trigger").length, 1);
      assert.equal(port.commands.filter(c => c.path === "/api/config").length, 0);
    } finally { await transport.disconnect(); }
  });
  await check("duplicate JSON fields reach firmware intact; ROM/other USB devices rejected", async () => {
    const {port, transport} = await device(() => ({status:400, error:"ESP_ERR_INVALID_ARG"}));
    try {
      assert.equal((await transport.fetch("/api/microphone/channel", {body:'{"slot":"left","slot":"right"}'})).status, 400);
      assert.equal((port.raw.at(-1).match(/"slot"/g)||[]).length, 2);
    } finally { await transport.disconnect(); }
    const wrong = new Port(); wrong.getInfo = () => ({usbVendorId:0x303a, usbProductId:0x1001});
    await assert.rejects(transport.connect(wrong)); assert.equal(wrong.opens.length, 0);
  });
  console.log("PASS: hosted Web Serial transport");
})().catch(error => { console.error(error); process.exitCode = 1; });
