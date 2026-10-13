const assert=require('node:assert/strict');
const {soundCRC32,keySoundPCM,wavHeader}=require('../firmware/keyboard/main/web/app.js');
assert.equal(soundCRC32(Buffer.from('123456789')),0xcbf43926);
const sine=(rate,seconds,freq=400,dc=0)=>Float32Array.from({length:Math.floor(rate*seconds)},(_,i)=>dc+0.2*Math.sin(2*Math.PI*freq*i/rate));
for(const rate of [16000,44100,48000]) {
  const bytes=keySoundPCM([sine(rate,3,400,.15)],rate,.5,2);
  assert.equal(bytes.length,64000);
  const view=new DataView(bytes.buffer);let mean=0,peak=0;
  for(let i=0;i<32000;i++){const v=view.getInt16(i*2,true);mean+=v;peak=Math.max(peak,Math.abs(v));}
  assert.ok(Math.abs(mean/32000)<1);assert.ok(peak>6500&&peak<6600);
  assert.ok(view.getInt16(14,true)>0);assert.ok(view.getInt16(54,true)<0);
}
assert.equal(keySoundPCM([sine(16000,.25)],16000,0,2).length,8000);
const a=sine(16000,1),b=Float32Array.from(a,x=>-x);
assert.throws(()=>keySoundPCM([a,b],16000,0,1)); // Stereo cancellation must not silently save a zero clip.
assert.throws(()=>keySoundPCM([Float32Array.from([NaN])],16000,0,.001));
for(const [start,seconds] of [[-1,1],[0,0],[0,2.01],[Infinity,1],[0,NaN],[4,1]])assert.throws(()=>keySoundPCM([a],16000,start,seconds));
assert.throws(()=>keySoundPCM([a,new Float32Array(1)],16000,0,1));
const normalized=keySoundPCM([sine(16000,1)],16000,0,1,true),v=new DataView(normalized.buffer);let peak=0;
for(let i=0;i<16000;i++)peak=Math.max(peak,Math.abs(v.getInt16(i*2,true)));
assert.ok(peak>=27800&&peak<=27853);
assert.equal(Buffer.from(wavHeader(normalized.length/2)).readUInt32LE(40),normalized.length);
console.log('PASS: signed resampling, stereo/DC handling, clip bounds, normalization, silence/NaN rejection, WAV lengths and CRC32 reference vector');
