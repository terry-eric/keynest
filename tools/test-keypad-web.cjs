const assert = require('node:assert/strict');
const {wavHeader, validateAudioChunk, analyzePCM, validateMicrophonePacket} = require('../firmware/keyboard/main/web/app.js');
const header = Buffer.from(wavHeader(32768));
assert.equal(header.length,44); assert.equal(header.toString('ascii',0,4),'RIFF');
assert.equal(header.readUInt32LE(4),65572); assert.equal(header.readUInt32LE(24),16000);
assert.equal(header.readUInt32LE(28),32000); assert.equal(header.readUInt16LE(34),16);
assert.equal(header.readUInt32LE(40),65536);
const tenMinutes=Buffer.from(wavHeader(16000*600));assert.equal(tenMinutes.readUInt32LE(4),19200036);assert.equal(tenMinutes.readUInt32LE(40),19200000);
for(const invalid of [-1,1.5,Infinity,NaN,9600001]) assert.throws(()=>wavHeader(invalid));
assert.equal(validateAudioChunk(new ArrayBuffer(0)),0); assert.equal(validateAudioChunk(new ArrayBuffer(2048)),1024);
assert.throws(()=>validateAudioChunk(new ArrayBuffer(3))); assert.throws(()=>validateAudioChunk(new ArrayBuffer(2050)));
const pcm = values => {const buffer = new ArrayBuffer(values.length * 2), view = new DataView(buffer);values.forEach((v,i)=>view.setInt16(i*2,v,true));return buffer;};
for (const dc of [454,-454,32767,-32768]) {
  const stats = analyzePCM(pcm(Array(128).fill(dc)));
  assert.equal(stats.dc,dc);assert.equal(stats.acDbfs,-120);assert.equal(stats.acPeak,0);
}
const signal = analyzePCM(pcm([450,458,450,458]));
assert.equal(signal.dc,454);assert.equal(signal.acDbfs,20*Math.log10(4/32768));
assert.equal(signal.acPeak,4/32768);assert.ok(signal.rawDbfs > signal.acDbfs+40);
assert.equal(analyzePCM(pcm([-32768,32767])).clipped,2);
assert.throws(()=>analyzePCM(new ArrayBuffer(0)));
const packet={buffer:pcm([1,-1]),rate:16000,frames:2,ageMs:0};
assert.equal(validateMicrophonePacket(packet),packet);
for(const fields of [{rate:48000},{frames:1},{frames:NaN},{frames:1.5},{ageMs:1001},{ageMs:-1},{ageMs:NaN},{buffer:new ArrayBuffer(0)}])
  assert.throws(()=>validateMicrophonePacket({...packet,...fields}));
console.log('PASS: WAV layout, PCM validation, DC removal, signed samples, clipping and stale preview rejection');
