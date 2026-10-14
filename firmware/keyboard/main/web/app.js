"use strict";

function wavHeader(frames) {
  if (!Number.isInteger(frames) || frames < 0 || frames > 16000 * 600) throw new Error("Invalid WAV frame count");
  const bytes = new ArrayBuffer(44), view = new DataView(bytes);
  function text(at, value) { for (let i = 0; i < value.length; i++) view.setUint8(at + i, value.charCodeAt(i)); }
  text(0, "RIFF"); view.setUint32(4, 36 + frames * 2, true); text(8, "WAVEfmt ");
  view.setUint32(16, 16, true); view.setUint16(20, 1, true); view.setUint16(22, 1, true);
  view.setUint32(24, 16000, true); view.setUint32(28, 32000, true);
  view.setUint16(32, 2, true); view.setUint16(34, 16, true); text(36, "data"); view.setUint32(40, frames * 2, true);
  return bytes;
}
function validateAudioChunk(buffer, maximum = 2048) {
  if (!(buffer instanceof ArrayBuffer) || buffer.byteLength % 2 || buffer.byteLength > maximum) throw new Error("Invalid PCM response");
  return buffer.byteLength / 2;
}
function analyzePCM(buffer) {
  const count = validateAudioChunk(buffer);
  if (!count) throw new Error("No microphone samples");
  const view = new DataView(buffer), samples = new Int16Array(count);
  let sum = 0, squares = 0, clipped = 0;
  for (let i = 0; i < count; ++i) {
    const value = view.getInt16(i * 2, true); samples[i] = value;
    sum += value; squares += value * value;
    if (value === -32768 || value === 32767) ++clipped;
  }
  const dc = sum / count;
  let acSquares = 0, acPeak = 0;
  for (const value of samples) {const ac = (value - dc) / 32768;acSquares += ac * ac;acPeak = Math.max(acPeak, Math.abs(ac));}
  const db = value => 20 * Math.log10(Math.max(value, 0.000001));
  return {samples, dc, rawDbfs:db(Math.sqrt(squares / count) / 32768), acDbfs:db(Math.sqrt(acSquares / count)), acPeak, peakDbfs:db(acPeak), clipped};
}
function validateMicrophonePacket(packet) {
  const count = validateAudioChunk(packet.buffer);
  if (!count || packet.rate !== 16000 || !Number.isSafeInteger(packet.frames) || packet.frames < count ||
      !Number.isFinite(packet.ageMs) || packet.ageMs < 0 || packet.ageMs > 1000) throw new Error("Invalid or stale microphone response");
  return packet;
}
function soundCRC32(bytes) {
  let crc=0xffffffff;
  for(const value of bytes){crc^=value;for(let i=0;i<8;i++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}
  return (crc^0xffffffff)>>>0;
}
function keySoundPCM(channels,rate,start,seconds,normalize=false) {
  if(!Array.isArray(channels)||!channels.length||channels.length>8||!Number.isInteger(rate)||rate<8000||rate>192000||
     !Number.isFinite(start)||start<0||!Number.isFinite(seconds)||seconds<=0||seconds>2)throw new Error("音效片段必須是 0～2 秒內的有效音訊。");
  const available=channels[0].length;
  if(!channels.every(c=>c instanceof Float32Array&&c.length===available))throw new Error("音訊聲道資料不完整。");
  const frames=Math.min(32000,Math.floor(seconds*16000),Math.floor((available/rate-start)*16000));
  if(frames<=0)throw new Error("片段起點超過錄音長度。");
  const mono=new Float32Array(frames);let sum=0;
  for(let i=0;i<frames;i++){
    const at=(start+i/16000)*rate,lo=Math.floor(at),hi=Math.min(lo+1,available-1),fraction=at-lo;
    let value=0;for(const channel of channels)value+=(channel[lo]*(1-fraction)+channel[hi]*fraction)/channels.length;
    if(!Number.isFinite(value))throw new Error("音訊含有無效的數值。");
    mono[i]=Math.max(-1,Math.min(1,value));sum+=mono[i];
  }
  const dc=sum/frames;let peak=0;for(const value of mono)peak=Math.max(peak,Math.abs(value-dc));
  const gain=normalize&&peak>0?0.85/peak:1,bytes=new Uint8Array(frames*2),view=new DataView(bytes.buffer);
  let audible=false;
  for(let i=0;i<frames;i++){
    const value=Math.round(Math.max(-1,Math.min(1,(mono[i]-dc)*gain))*32767);
    audible||=value!==0;view.setInt16(i*2,value,true);
  }
  if(!audible)throw new Error("選取的片段沒有可播放的聲音；請重新錄音或選其他片段。");
  return bytes;
}
if (typeof module === "object" && module.exports) {
  module.exports = { wavHeader, validateAudioChunk, analyzePCM, validateMicrophonePacket, soundCRC32, keySoundPCM };
} else {
  document.addEventListener("DOMContentLoaded", initialize);
}

function initialize() {
  const $ = id => document.getElementById(id);
  const demo = new URLSearchParams(location.search).get("demo") === "1";
  const modeClasses = ["off", "solid", "breathe", "rainbow", "ripple", "meter"];
  const keyNames = new Map([[0,"停用"],[40,"Enter"],[41,"Esc"],[42,"Backspace"],[43,"Tab"],[44,"Space"],[57,"Caps Lock"],[73,"Insert"],[74,"Home"],[75,"Page Up"],[76,"Delete"],[77,"End"],[78,"Page Down"],[79,"→"],[80,"←"],[81,"↓"],[82,"↑"]]);
  for (let i=0;i<26;i++) keyNames.set(4+i, String.fromCharCode(65+i));
  for (let i=0;i<10;i++) keyNames.set(30+i, String((i+1)%10));
  for (let i=0;i<12;i++) keyNames.set(58+i, "F"+(i+1));
  for (let i=0;i<12;i++) keyNames.set(104+i, "F"+(i+13));
  let draft = null, state = null, edited = false, connected = false, busy = false, epoch = 0, capture = null;
  let downloadURL = null, toastTimer, polling = false, downloading = false, captureError = "";
  let monitoring = false, monitorGeneration = 0, monitorTimer, micStats = null, micFrames = -1, micReceived = 0;
  const history = Array(70).fill(0), rows = [];
  const defaults = {led_mode:4,brightness:64,speed:80,volume:10,mic_gain:2,sound:2,color:[120,88,255],keycodes:[104,105,106,107],modifiers:[0,0,0,0]};
  const simulation = {config:structuredClone(defaults),dirty:false,keys:0,audio_ready:true,mic_preview:true,mic_model:"ZTS6672",mic_slot:"left",usb_connected:true,ssid:"PCB2-Keypad-DEMO",ip:"展示模式",mic_dbfs:-32,mic_peak:.12,recording:{sd_ready:false,flash_ready:true,mounting:false,downloading:false,state:0,target:0,last_target:1,frames:0,duration_ms:0,dropped_frames:0,limit_seconds:600,flash_limit_seconds:2.048,filename:"",error:""}};
  let demoStarted=0, demoDelivered=0;
  let selectedAudio=null,lastRecordingBlob=null,clipURL=null,demoSound=null;
  simulation.custom_sound={supported:true,per_key:true,ready:false,uploading:false,keys:Array.from({length:4},()=>({ready:false,custom:false,frames:0}))};
  $("demo-banner").hidden = !demo;
  for (let i=0;i<4;i++) {
    const row = document.createElement("div"); row.className="mapping-row";
    const label=document.createElement("kbd"); label.textContent="K"+(i+1); row.append(label);
    const select=document.createElement("select"); select.setAttribute("aria-label", "KEY"+(i+1)+" 輸出的按鍵");
    const entries = [...keyNames].sort((a,b)=>a[0]-b[0]);
    for (const [code,name] of entries) select.add(new Option(name,code)); row.append(select);
    const group=document.createElement("div"); group.className="modifier-group";
    const modifiers=["Ctrl","Shift","Alt","Win"].map((name,bit)=>{
      const label=document.createElement("label"), input=document.createElement("input"); input.type="checkbox"; input.value=1<<bit;
      input.setAttribute("aria-label", "KEY"+(i+1)+" "+name); label.append(input,document.createTextNode(name)); group.append(label); return input;
    });
    row.append(group); $("mappings").append(row); rows.push({select,modifiers});
    select.addEventListener("change", changed); modifiers.forEach(input=>input.addEventListener("change",changed));
  }
  function toast(message,error=false) {
    clearTimeout(toastTimer); $("toast").textContent=message; $("toast").classList.toggle("error",error); $("toast").hidden=false;
    toastTimer=setTimeout(()=>$("toast").hidden=true,4500);
  }
  function setConnection(value) {
    connected=value; $("connection").classList.toggle("online",value);
    $("connection").replaceChildren(Object.assign(document.createElement("i"),{}),document.createTextNode(demo?"展示模式":value?"裝置已連線":"裝置離線"));
    $("offline").hidden=value; controls();
    if (!value) { $("usb-status").textContent="USB 狀態未知"; $("usb-status").classList.remove("active"); $("mic-status").textContent="收音狀態未知"; $("mic-status").classList.remove("active"); $("dbfs").textContent="—"; $("meter-fill").style.width="0%"; document.querySelectorAll(".keycap").forEach(key=>key.classList.remove("down")); }
    if (!value && monitoring) clearMicrophone("裝置離線，等待重新連線。");
  }
  async function request(path,body) {
    if (demo) return demoRequest(path,body);
    const response=await fetch(path,{method:body===undefined?"GET":"POST",cache:"no-store",signal:AbortSignal.timeout(path.startsWith("/api/sound/")||path.startsWith("/api/sd/")?30000:3000),headers:body===undefined?{}:{"Content-Type":"application/json","X-PCB2-Control":"1"},body:body===undefined?undefined:JSON.stringify(body)});
    let json; try {json=await response.json();} catch {throw new Error("裝置回應格式不正確");}
    if (!response.ok) throw new Error(json.error || "裝置拒絕操作"); return json;
  }
  function updateDemo() {
    simulation.mic_dbfs=-32+10*Math.sin(Date.now()/900); simulation.mic_peak=.2+.15*Math.sin(Date.now()/600);
    const r=simulation.recording;
    if (r.state===2) {
      const limit=r.target===1?2.048:r.limit_seconds;
      r.frames=Math.floor(Math.min((Date.now()-demoStarted)/1000,limit)*16000); r.duration_ms=r.frames/16;
      if (r.frames>=limit*16000) {r.state=0;if(r.target===1)r.filename="INTERNAL.WAV";}
    }
  }
  async function demoRequest(path,body) {
    if(path==="/api/microphone/channel")simulation.mic_slot=body.slot;
    updateDemo();
    if (path==="/api/state") return structuredClone(simulation);
    if (path==="/api/config") {simulation.config=structuredClone(body);simulation.dirty=true;}
    if (path==="/api/save") simulation.dirty=false;
    if(path==="/api/sound/begin")demoSound=structuredClone(body);
    if(path==="/api/sound/commit"&&demoSound) {
      const clip={ready:true,custom:demoSound.key!==undefined,frames:demoSound.frames,crc32:demoSound.crc32,storage:demoSound.storage};
      if(demoSound.key===undefined){Object.assign(simulation.custom_sound,clip);simulation.custom_sound.keys=simulation.custom_sound.keys.map(c=>c.custom?c:{...clip});}
      else simulation.custom_sound.keys[demoSound.key]=clip;
      simulation.custom_sound.ready=true;demoSound=null;
    }
    if(path==="/api/sound/cancel")demoSound=null;
    if(path==="/api/sound/default")simulation.custom_sound.keys[body.key]={ready:!!simulation.custom_sound.frames,custom:false,frames:simulation.custom_sound.frames||0,storage:simulation.custom_sound.storage};
    if (path==="/api/record") {
      if(body.action==="start") {simulation.recording.state=2;simulation.recording.target={browser:0,flash:1,sd:2}[body.target];simulation.recording.frames=0;simulation.recording.error="";demoStarted=Date.now();demoDelivered=0;}
      else {updateDemo();simulation.recording.state=0;if(simulation.recording.target===1)simulation.recording.filename="INTERNAL.WAV";}
    }
    return {ok:true};
  }
  function loadConfig(config) {
    draft=structuredClone(config);
    ["brightness","speed","volume"].forEach(id=>$(id).value=config[id]); $("mic-gain").value=config.mic_gain; $("sound").value=config.sound;
    $("color").value="#"+config.color.map(n=>n.toString(16).padStart(2,"0")).join("");
    rows.forEach((row,i)=>{
      if(![...row.select.options].some(option=>Number(option.value)===config.keycodes[i]))row.select.add(new Option("HID 0x"+config.keycodes[i].toString(16),config.keycodes[i]));
      row.select.value=config.keycodes[i]; row.modifiers.forEach((input,bit)=>input.checked=!!(config.modifiers[i]&(1<<bit)));
    }); preview();
  }
  function readConfig() {
    const config=structuredClone(draft || defaults);
    ["brightness","speed","volume"].forEach(id=>config[id]=Number($(id).value)); config.mic_gain=Number($("mic-gain").value); config.sound=Number($("sound").value);
    config.color=$("color").value.match(/[a-f0-9]{2}/gi).map(n=>parseInt(n,16));
    rows.forEach((row,i)=>{config.keycodes[i]=Number(row.select.value);config.modifiers[i]=(config.modifiers[i]&0xf0)|row.modifiers.reduce((mask,input)=>mask|(input.checked?Number(input.value):0),0);});
    return config;
  }
  function changed() { if(!draft)return; draft=readConfig(); edited=true; preview(); $("save-state").textContent="有尚未套用的變更"; }
  function preview() {
    const config=draft || defaults;
    $("brightness-value").textContent=Math.round(config.brightness/255*100)+"%"; $("speed-value").textContent=config.speed+"%"; $("volume-value").textContent=config.volume+"%"; $("gain-value").textContent=config.mic_gain+"×";
    document.documentElement.style.setProperty("--led",$("color").value);document.documentElement.style.setProperty("--preview-brightness",config.brightness/255);document.documentElement.style.setProperty("--preview-speed",250/config.speed+"s");
    $("board").className="board mode-"+modeClasses[config.led_mode];
    document.querySelectorAll("[data-mode]").forEach(button=>{const chosen=Number(button.dataset.mode)===config.led_mode;button.classList.toggle("selected",chosen);button.setAttribute("aria-pressed",String(chosen));});
    rows.forEach((row,i)=>{const code=config.keycodes[i];$("key-label-"+i).textContent=keyNames.get(code)||"0x"+code.toString(16);const names=["Ctrl","Shift","Alt","Win","R Ctrl","R Shift","R Alt","R Win"].filter((_,bit)=>config.modifiers[i]&(1<<bit));$("key-mod-"+i).textContent=names.join("+")||"按鍵";});
  }
  function controls() {
    const r=state?.recording, recording=r && [1,2,3].includes(r.state);
    ["save","apply","test-sound","sd-retry"].forEach(id=>$(id).disabled=!connected||busy||downloading||(id==="save"&&recording));
    document.querySelectorAll("[data-trigger]").forEach(button=>button.disabled=!connected||busy);
    const target=$("record-target").value;
    const available=target==="browser"||target==="flash"&&r?.flash_ready||target==="sd"&&r?.sd_ready;
    $("record-start").disabled=!connected||busy||downloading||recording||!!capture||!state?.audio_ready||!available||r?.mounting;
    $("record-stop").disabled=!connected||busy||!recording;$("record-target").disabled=recording||!!capture||busy;
    $("device-download").disabled=!connected||busy||downloading||recording||!r?.filename;
    $("sd-retry").disabled=!connected||busy||recording||r?.mounting||r?.sd_ready;
    $("record-target").options[1].disabled=!!state&&!r?.flash_ready;$("record-target").options[2].disabled=!!state&&!r?.sd_ready;
    $("monitor-toggle").disabled=!monitoring&&(!connected||!state?.audio_ready||state?.mic_preview===false);
    $("mic-channel").disabled=!connected||busy||recording||!!capture||!state?.mic_slot;
    $("usb-update").disabled=!connected||busy||downloading||recording||!!state?.custom_sound?.uploading;
    const custom=!!state?.custom_sound?.supported,locked=!connected||busy||downloading||recording||!!capture||!!state?.custom_sound?.uploading;
    $("sound-upload").disabled=locked||!custom||!selectedAudio||($("sound-storage").value==="sd"&&!r?.sd_ready);
    $("sound-recording").disabled=locked||!custom||(!lastRecordingBlob&&!r?.filename);
    $("sound-file").disabled=locked||!custom;$("sound-storage").disabled=locked||!custom;
    $("sound-key").disabled=locked||!custom;
    for(let i=0;i<4;i++)$("sound-key").options[i].disabled=!state?.custom_sound?.per_key;
    if(custom&&!state?.custom_sound?.per_key)$("sound-key").value="common";
    const key=$("sound-key").value,clip=key==="common"?state?.custom_sound:state?.custom_sound?.keys?.[Number(key)];
    $("sound-default").hidden=key==="common";
    $("sound-default").disabled=locked||!state?.custom_sound?.per_key||!clip?.custom;
    $("sound-test-key").disabled=locked||!clip?.ready;
    $("sound-storage").options[1].disabled=!r?.sd_ready;
    $("sound").options[5].disabled=!custom||!state?.custom_sound?.ready;
    $("sd-files-refresh").disabled=locked||!custom||!r?.sd_ready;
    ["sd-file-download","sd-file-sound"].forEach(id=>$(id).disabled=locked||!custom||!r?.sd_ready||!$("sd-file").value);
    if(monitoring&&state?.mic_preview_legacy)["record-start","sd-retry","save"].forEach(id=>$(id).disabled=true);
  }
  function targetHelp() {
    const target=$("record-target").value,r=state?.recording;
    const limit=r?.limit_seconds||60,duration=limit>=60&&limit%60===0?`${limit/60} 分鐘`:`${limit} 秒`;
    $("target-help").textContent=target==="browser"?`不用 SD 卡，最長 ${duration}。保持網頁開啟與 ${state?.transport==="usb"?"USB":"Wi-Fi"} 連線，錄完下載 WAV。`:target==="flash"?`保存在鍵盤內，最多 ${(r?.flash_limit_seconds||2.048).toFixed(2)} 秒；最新一段會取代舊錄音。`:`最長 ${duration}，建立新 WAV，不覆蓋舊檔。${r?.sd_ready?"microSD 已就緒。":"目前沒有可用的 microSD。"}`;
    controls();
  }
  function render(next) {
    state=next;
    $("preview-close").hidden=!next.local_preview;
    $("usb-update").hidden=!next.usb_auto_download;
    if (!draft || !edited && JSON.stringify(draft)!==JSON.stringify(next.config)) loadConfig(next.config);
    if(!edited)$("save-state").textContent=next.dirty?"已套用・尚未保存":"設定已保存";
    $("usb-status").textContent=next.usb_connected?"USB HID 已連接":"USB 尚未連接";$("usb-status").classList.toggle("active",next.usb_connected);
    $("mic-status").textContent=next.audio_ready?"收音中":"音訊未就緒";$("mic-status").classList.toggle("active",next.audio_ready);
    if(next.mic_slot)$("mic-channel").value=next.mic_slot;
    if (!monitoring) showLevel(next.audio_ready?next.mic_dbfs:null, false);
    history.push(next.audio_ready?(monitoring?(micStats?.acPeak||0):next.mic_peak):0);history.shift();drawHistory();
    document.querySelectorAll("[data-trigger]").forEach(button=>button.classList.toggle("down",!!(next.keys&(1<<Number(button.dataset.trigger)))));
    renderSound(next);
    const r=next.recording;$("rec-status").textContent=["待命","準備中","錄音中","完成與保存中","錄音已停止"][r.state]||"未知";$("rec-status").classList.toggle("active",r.state===2);
    $("record-dot").classList.toggle("live",r.state===2);const seconds=r.duration_ms/1000;$("rec-duration").textContent=String(Math.floor(seconds/60)).padStart(2,"0")+":"+(seconds%60).toFixed(1).padStart(4,"0");
    $("record-error").textContent=r.error || captureError || (r.dropped_frames?"資料遺失，請重新錄音。":"");
    $("flash-status").textContent=r.flash_ready?"內建 Flash 可用":"Flash 尚未就緒";$("sd-status").textContent=r.mounting?"SD 掛載中":r.sd_ready?"microSD 已就緒":"microSD 未掛載（可免卡錄音）";
    $("device-download").textContent=r.filename?"↓ 下載 "+r.filename:"↓ 裝置尚無保存的錄音";
    $("device-address").textContent=(demo?"展示模式":next.transport==="usb"?"USB 連線 · 電腦維持原本 Wi-Fi":next.ssid+" · "+next.ip)+" · 離線控制頁面";targetHelp();
  }
  function soundLabel(){return $("sound-key").value==="common"?"共用音效":"第 "+(Number($("sound-key").value)+1)+" 顆鍵";}
  function renderSound(next=state) {
    const custom=next?.custom_sound,key=$("sound-key").value;
    const clip=key==="common"?custom:custom?.keys?.[Number(key)];
    $("sound-key-summary").textContent=!custom?.supported?"需要更新鍵盤韌體。":!custom.per_key?"目前韌體只有共用音效；更新後可分別設定四顆鍵。":key==="common"?"更換共用音效，不會覆蓋每顆鍵另設的音效。":clip?.custom?"這顆鍵有獨立音效；其他按鍵不受影響。":"這顆鍵目前使用共用音效。";
    $("sound-upload").textContent="保存到 "+soundLabel();
    if(!downloading)$("sound-status").textContent=!custom?.supported?"更新韌體後可保存自訂音效。":clip?.error||custom.error|| (custom.uploading?"正在接收或保存音效…":clip?.ready&&clip.frames?("已保存到 "+(clip.storage==="sd"?"SD："+clip.filename:"內建儲存")+" · "+(clip.frames/16000).toFixed(2)+" 秒"):"尚未保存可播放的音效。");
  }
  function drawHistory() {
    const canvas=$("level-history"),ctx=canvas.getContext("2d"),w=canvas.width,h=canvas.height;
    ctx.clearRect(0,0,w,h);ctx.strokeStyle="#333c51";ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(0,h-10);ctx.lineTo(w,h-10);ctx.stroke();
    ctx.strokeStyle="#57dec2";ctx.lineWidth=2;ctx.beginPath();history.forEach((value,i)=>{const x=i/(history.length-1)*w,y=h-10-Math.min(1,value)* (h-25);if(i)ctx.lineTo(x,y);else ctx.moveTo(x,y);});ctx.stroke();
  }
  function showLevel(dbfs, ac) {
    $("dbfs").textContent=dbfs===null?"—":Number(dbfs).toFixed(1);
    $("meter-fill").style.width=dbfs===null?"0%":Math.max(0,Math.min(100,(dbfs+90)/90*100))+"%";
    $("meter-label").textContent=ac?"去除直流後音量":"原始數位音量";
  }
  function drawWaveform(stats) {
    const canvas=$("mic-waveform"),ctx=canvas.getContext("2d"),w=canvas.width,h=canvas.height;
    ctx.clearRect(0,0,w,h);ctx.strokeStyle="#293448";ctx.lineWidth=1;ctx.beginPath();
    for(let y=0;y<=4;y++){ctx.moveTo(0,y*h/4);ctx.lineTo(w,y*h/4);}
    for(let x=0;x<=8;x++){ctx.moveTo(x*w/8,0);ctx.lineTo(x*w/8,h);}ctx.stroke();
    if(!stats)return;
    const scale=$("wave-auto").checked?Math.min(1024,Math.max(1,.8/Math.max(stats.acPeak,1/32768))):1;
    $("wave-scale").textContent=scale.toFixed(scale<10?1:0)+"×";
    $("wave-window").textContent=(stats.samples.length/16).toFixed(1)+" ms · 16 kHz";
    ctx.strokeStyle=stats.clipped?"#ffad8a":"#57dec2";ctx.lineWidth=1.4;ctx.beginPath();
    // Min/max per pixel preserves brief impulses that a point-sampled trace can miss.
    const pixels=Math.min(w,stats.samples.length);
    for(let x=0;x<pixels;x++) {
      const first=Math.floor(x*stats.samples.length/pixels),last=Math.floor((x+1)*stats.samples.length/pixels);
      let min=Infinity,max=-Infinity;
      for(let i=first;i<last;i++){min=Math.min(min,stats.samples[i]);max=Math.max(max,stats.samples[i]);}
      const y=value=>h/2-(value-stats.dc)/32768*scale*(h/2-6);
      ctx.moveTo(x*w/pixels,y(min));ctx.lineTo(x*w/pixels,y(max));
    }ctx.stroke();
  }
  function clearMicrophone(message) {
    micStats=null;drawWaveform(null);$("mic-raw").textContent="—";$("mic-dc").textContent="—";$("mic-ac-peak").textContent="—";$("wave-scale").textContent="—";
    $("monitor-note").textContent=message;
    if(monitoring)showLevel(null,true);
  }
  async function microphoneResponse() {
    if(demo) {
      const frames=Math.floor(Date.now()/1000*16000),buffer=new ArrayBuffer(2048),view=new DataView(buffer);
      const amplitude=500+400*Math.sin(Date.now()/700);
      for(let i=0;i<1024;i++)view.setInt16(i*2,Math.round(454+amplitude*Math.sin((frames-1024+i)*2*Math.PI*220/16000)),true);
      return {buffer,rate:16000,frames,ageMs:0};
    }
    const response=await fetch("/api/microphone",{cache:"no-store",signal:AbortSignal.timeout(1500)});
    if(!response.ok)throw new Error(response.status===404?"裝置韌體尚未支援波形，請更新韌體。":response.status===409?"目前有其他錄音作業，請先停止錄音。":"麥克風資料中斷，等待恢復。");
    const numeric=name=>{const value=response.headers.get(name);return value===null||value.trim()===""?NaN:Number(value);};
    return {buffer:await response.arrayBuffer(),rate:numeric("X-PCB2-Sample-Rate"),frames:numeric("X-PCB2-Frames"),ageMs:numeric("X-PCB2-Age-Ms")};
  }
  async function pollMicrophone(generation) {
    if(!monitoring||generation!==monitorGeneration)return;
    try {
      if(!document.hidden&&!downloading) {
        const packet=validateMicrophonePacket(await microphoneResponse());
        if(!monitoring||generation!==monitorGeneration)return;
        if(packet.frames!==micFrames) {
          micFrames=packet.frames;micReceived=Date.now();micStats=analyzePCM(packet.buffer);
          showLevel(micStats.acDbfs,true);drawWaveform(micStats);
          $("mic-raw").textContent=micStats.rawDbfs.toFixed(1);$("mic-dc").textContent=micStats.dc.toFixed(1);$("mic-ac-peak").textContent=micStats.peakDbfs.toFixed(1);
          $("monitor-note").textContent=micStats.clipped?"音訊已削波，請降低收音增益。":demo?"展示模式：波形為合成音訊，沒有連接 PCB 麥克風。":state?.mic_preview_legacy?"即時相容預覽中：只使用暫存串流，不保存音訊。停止監測後約 2 秒可開始錄音。":"即時監測中。波形和音量已去除直流偏移；自動放大只影響顯示，不修改收音或錄音。";
        } else if(Date.now()-micReceived>1000) clearMicrophone("麥克風沒有新樣本，等待恢復。");
      } else clearMicrophone(document.hidden?"頁面在背景，已暫停波形更新。":"下載錄音中，已暫停波形更新。");
    } catch(error) {if(monitoring&&generation===monitorGeneration){micFrames=-1;clearMicrophone(error.message);}}
    finally {if(monitoring&&generation===monitorGeneration)monitorTimer=setTimeout(()=>void pollMicrophone(generation),document.hidden?500:100);}
  }
  async function poll() {
    if(polling||downloading)return;polling=true;const current=epoch;
    try {const next=await request("/api/state");if(current===epoch){render(next);setConnection(true);}}
    catch {setConnection(false);}finally {polling=false;}
  }
  async function action(callback) {
    if(busy)return;busy=true;controls();
    try {await callback();await poll();}catch(error){toast(error.message,true);}finally {busy=false;controls();}
  }
  async function apply() {
    const config=readConfig();++epoch;await request("/api/config",config);++epoch;draft=config;edited=false;preview();
  }
  async function trigger(key) {
    if(edited)await apply();await request("/api/trigger",{key});
    const button=document.querySelector(`[data-trigger="${key}"]`);button.classList.add("test");setTimeout(()=>button.classList.remove("test"),300);
  }
  async function audioResponse(owner) {
    if(demo) {
      updateDemo();const r=simulation.recording;const frames=Math.min(1024,Math.max(0,r.frames-demoDelivered));const data=new ArrayBuffer(frames*2),view=new DataView(data);
      for(let i=0;i<frames;i++)view.setInt16(i*2,Math.sin((demoDelivered+i)*2*Math.PI*220/16000)*800,true);demoDelivered+=frames;
      return {buffer:data,state:r.state,frames:r.frames,dropped:0};
    }
    const response=await fetch("/api/audio",{cache:"no-store",headers:{"X-PCB2-Recording":owner},signal:AbortSignal.timeout(2000)});
    if(!response.ok)throw new Error("錄音串流中斷："+response.status);
    return {buffer:await response.arrayBuffer(),state:Number(response.headers.get("X-PCB2-State")),frames:Number(response.headers.get("X-PCB2-Frames")),dropped:Number(response.headers.get("X-PCB2-Dropped"))};
  }
  async function collect(session) {
    try {
      while(capture===session) {
        const packet=await audioResponse(session.owner);const count=validateAudioChunk(packet.buffer);
        if(count){if(session.frames+count>16000*600)throw new Error("錄音超過 10 分鐘上限。");session.chunks.push(packet.buffer);session.frames+=count;}
        if(packet.dropped)throw new Error(`錄音資料遺失，請保持 ${state?.transport==="usb"?"USB":"Wi-Fi"} 連線後重新錄音。`);
        if([0,4].includes(packet.state)&&!count) {
          if(packet.state===4||session.frames!==packet.frames)throw new Error("錄音未完整接收，請重新錄音。");
          if(!session.frames)throw new Error("沒有收到收音資料。");
          const blob=new Blob([wavHeader(session.frames),...session.chunks],{type:"audio/wav"});
          lastRecordingBlob=blob;
          if(downloadURL)URL.revokeObjectURL(downloadURL);downloadURL=URL.createObjectURL(blob);
          $("browser-download").href=downloadURL;$("browser-download").download="PCB2-"+Date.now()+".wav";$("browser-download").classList.remove("disabled");
          toast("錄音完成，可下載到手機或電腦。");break;
        }
        await new Promise(resolve=>setTimeout(resolve,state?.transport==="usb"?10:40));
      }
    } catch(error) {
      captureError=error.message;$("record-error").textContent=captureError;toast(captureError,true);
      try {await request("/api/record",{action:"stop"});}catch{}
    } finally {if(capture===session)capture=null;controls();await poll();}
  }
  function ownerID() {const bytes=new Uint8Array(8);crypto.getRandomValues(bytes);return [...bytes].map(n=>n.toString(16).padStart(2,"0")).join("");}
  document.querySelectorAll("[data-mode]").forEach(button=>button.addEventListener("click",()=>{if(!draft)return;draft.led_mode=Number(button.dataset.mode);changed();}));
  document.querySelectorAll("[data-color]").forEach(button=>button.addEventListener("click",()=>{$("color").value=button.dataset.color;changed();}));
  ["color","brightness","speed","volume","mic-gain","sound"].forEach(id=>$(id).addEventListener("input",changed));
  document.querySelectorAll("[data-trigger]").forEach(button=>button.addEventListener("click",()=>action(()=>trigger(Number(button.dataset.trigger)))));
  $("apply").addEventListener("click",()=>action(async()=>{await apply();toast("設定已套用，保存後會在重開機保留。");}));
  $("save").addEventListener("click",()=>action(async()=>{if(edited)await apply();await request("/api/save",{});toast("設定已保存到裝置。");}));
  $("test-sound").addEventListener("click",()=>action(()=>trigger(0)));$("reconnect").addEventListener("click",poll);
  async function selectAudio(bytes,label) {
    selectedAudio=null;$("sound-source").textContent="正在讀取音訊…";
    updateClipPreview();
    if(bytes.byteLength>20*1024*1024)throw new Error("音訊檔請小於 20MB。");
    const context=new AudioContext();
    try {
      const decoded=await context.decodeAudioData(bytes.slice(0));
      selectedAudio={channels:Array.from({length:decoded.numberOfChannels},(_,i)=>decoded.getChannelData(i)),rate:decoded.sampleRate};
      $("clip-start").value=0;$("clip-length").value=Math.min(1,decoded.duration).toFixed(2);
      $("sound-source").textContent=label+" · "+decoded.duration.toFixed(2)+" 秒；請選取最多 2 秒。";
    } finally {await context.close();}
    updateClipPreview();
    controls();
  }
  function updateClipPreview() {
    $("sound-preview").pause();$("sound-preview").removeAttribute("src");$("sound-preview").hidden=true;
    if(clipURL){URL.revokeObjectURL(clipURL);clipURL=null;}
    $("sound-clip-error").textContent="";
    if(!selectedAudio)return;
    try {
      const bytes=keySoundPCM(selectedAudio.channels,selectedAudio.rate,Number($("clip-start").value),Number($("clip-length").value),$("clip-normalize").checked);
      clipURL=URL.createObjectURL(new Blob([wavHeader(bytes.length/2),bytes],{type:"audio/wav"}));
      $("sound-preview").src=clipURL;$("sound-preview").hidden=false;
    }catch(error){$("sound-clip-error").textContent=error.message;}
  }
  ["clip-start","clip-length","clip-normalize"].forEach(id=>$(id).addEventListener("change",updateClipPreview));
  $("sound-key").addEventListener("change",()=>{renderSound();controls();});
  $("sound-test-key").addEventListener("click",()=>action(async()=>{
    if(Number($("sound").value)!==5)throw new Error("請先選擇自訂音效並儲存，再在鍵盤試聽。");
    await trigger($("sound-key").value==="common"?0:Number($("sound-key").value));
  }));
  $("sound-default").addEventListener("click",()=>action(async()=>{
    if($("sound-key").value==="common")return;
    await request("/api/sound/default",{key:Number($("sound-key").value)});toast(soundLabel()+" 已恢復使用共用音效。");
  }));
  async function wavBytes(path) {
    const response=await fetch(path,{cache:"no-store",signal:AbortSignal.timeout(300000)});
    if(!response.ok)throw new Error("無法讀取錄音；請先完成錄音並確認 SD 已掛載。");
    return response.arrayBuffer();
  }
  $("sound-file").addEventListener("change",()=>action(async()=>{
    const file=$("sound-file").files[0];if(!file)return;
    if(file.size>20*1024*1024)throw new Error("音訊檔請小於 20MB。");
    await selectAudio(await file.arrayBuffer(),file.name);
  }));
  $("sound-recording").addEventListener("click",()=>action(async()=>{
    downloading=true;controls();
    try {await selectAudio(lastRecordingBlob&&state.recording.target===0?await lastRecordingBlob.arrayBuffer():await wavBytes("/api/recording.wav"),"最新錄音");}
    finally {downloading=false;}
  }));
  $("usb-update").addEventListener("click",()=>action(async()=>{
    await request("/api/device/restart",{mode:"bootloader"});
    toast("正在切換到 USB 更新模式，裝置會暫時離線。");
  }));
  $("sound-storage").addEventListener("change",controls);
  $("sound-upload").addEventListener("click",()=>action(async()=>{
    if(!selectedAudio)throw new Error("請先選聲音。");
    const bytes=keySoundPCM(selectedAudio.channels,selectedAudio.rate,Number($("clip-start").value),Number($("clip-length").value),$("clip-normalize").checked);
    const owner=ownerID(),storage=$("sound-storage").value,key=$("sound-key").value,label=soundLabel();let began=false;
    downloading=true;controls();
    try {
      await request("/api/sound/begin",{owner,storage,frames:bytes.length/2,crc32:soundCRC32(bytes),...(key==="common"?{}:{key:Number(key)})});began=true;
      for(let offset=0;offset<bytes.length;offset+=1024){
        const chunk=bytes.subarray(offset,offset+1024);
        await request("/api/sound/chunk",{owner,offset,pcm:btoa(String.fromCharCode(...chunk)),crc32:soundCRC32(chunk)});
        $("sound-status").textContent="傳送音效 "+Math.round((offset+chunk.length)/bytes.length*100)+"%";
      }
      $("sound-status").textContent="正在保存音效…";await request("/api/sound/commit",{owner});began=false;
      $("sound").value=5;changed();await apply();await request("/api/save",{});
      toast(label+" 的音效已保存，現在按下實體按鍵就能播放。");
    } finally {
      if(began)try{await request("/api/sound/cancel",{owner});}catch{}
      downloading=false;
    }
  }));
  $("sd-files-refresh").addEventListener("click",()=>action(async()=>{
    const result=await request("/api/sd/files");
    if(!result.ok)throw new Error(result.error);
    $("sd-file").replaceChildren(new Option("選擇 SD 錄音／音效",""));
    for(const file of result.files)$("sd-file").add(new Option(file.name+" · "+Math.round(file.bytes/1024)+"KB",file.name));
    $("sd-files-note").textContent=result.truncated?"已顯示前 32 個音訊檔；完整卡片內容請用讀卡機。":result.files.length?"可下載，或選取片段保存為按鍵聲。":"SD 上還沒有板子錄下或保存的 WAV。";
  }));
  $("sd-file").addEventListener("change",controls);
  function sdPath(){return "/api/sd/recording.wav?name="+encodeURIComponent($("sd-file").value);}
  $("sd-file-sound").addEventListener("click",()=>action(async()=>{
    downloading=true;controls();
    try{await selectAudio(await wavBytes(sdPath()),$("sd-file").value);}
    finally{downloading=false;}
    $("sound-source").scrollIntoView({behavior:"smooth",block:"center"});
  }));
  $("sd-file-download").addEventListener("click",()=>action(async()=>{
    downloading=true;controls();
    try{
      const bytes=await wavBytes(sdPath()),url=URL.createObjectURL(new Blob([bytes],{type:"audio/wav"})),link=document.createElement("a");
      link.href=url;link.download=$("sd-file").value;document.body.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),60000);
      toast("SD 錄音已讀取，請查看電腦下載項目。");
    }finally{downloading=false;}
  }));
  $("mic-channel").addEventListener("change",()=>action(async()=>{
    await request("/api/microphone/channel",{slot:$("mic-channel").value});
    micFrames=-1;clearMicrophone("聲道已切換，正在取得新資料…");await poll();
  }));
  $("monitor-toggle").addEventListener("click",()=>{
    monitoring=!monitoring;++monitorGeneration;clearTimeout(monitorTimer);micFrames=-1;
    $("monitor-toggle").textContent=monitoring?"停止即時監測":"開始即時監測";$("monitor-toggle").setAttribute("aria-pressed",String(monitoring));
    clearMicrophone(monitoring?"正在取得麥克風資料…":"即時監測已停止，沒有保存錄音。");
    if(monitoring)void pollMicrophone(monitorGeneration);else showLevel(connected&&state?.audio_ready?state.mic_dbfs:null,false);
    controls();
  });
  $("wave-auto").addEventListener("change",()=>drawWaveform(micStats));
  $("preview-close").addEventListener("click",()=>action(async()=>{
    await request("/api/preview/close",{});monitoring=false;++monitorGeneration;clearTimeout(monitorTimer);clearInterval(stateTimer);
    clearMicrophone(state?.preview_wifi_managed?"預覽已結束，正在恢復原本 Wi-Fi。":"本機預覽已結束。");
    toast(state?.preview_wifi_managed?"已結束預覽，正在恢復原本 Wi-Fi。":"已結束本機預覽。");
  }));
  $("sd-retry").addEventListener("click",()=>action(async()=>{await request("/api/sd",{});toast("已要求重試 SD 掛載。");}));
  $("record-target").addEventListener("change",targetHelp);
  $("record-start").addEventListener("click",()=>action(async()=>{
    if(edited)await apply();const target=$("record-target").value,owner=ownerID();
    await request("/api/record",{action:"start",target,owner});
    captureError="";
    if(target==="browser"){
      if(downloadURL){URL.revokeObjectURL(downloadURL);downloadURL=null;}
      $("browser-download").removeAttribute("href");$("browser-download").classList.add("disabled");
      const session={owner,chunks:[],frames:0};capture=session;void collect(session);
    }toast("已開始錄音。");
  }));
  $("record-stop").addEventListener("click",()=>action(async()=>{await request("/api/record",{action:"stop"});toast("正在完成錄音。");}));
  $("device-download").addEventListener("click",()=>action(async()=>{
    downloading=true;controls();
    try {
      let blob;
      if(demo)blob=new Blob([wavHeader(16000),new Uint8Array(32000)],{type:"audio/wav"});
      else {const response=await fetch("/api/recording.wav",{signal:AbortSignal.timeout(300000)});if(!response.ok)throw new Error("無法下載裝置錄音");blob=await response.blob();}
      const url=URL.createObjectURL(blob),link=document.createElement("a");link.href=url;link.download=state.recording.filename||"PCB2.wav";document.body.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),60000);
    } finally {downloading=false;controls();}
  }));
  window.addEventListener("pagehide",()=>{monitoring=false;++monitorGeneration;clearTimeout(monitorTimer);if(capture&&!demo)void fetch("/api/record",{method:"POST",keepalive:true,headers:{"Content-Type":"application/json","X-PCB2-Control":"1"},body:JSON.stringify({action:"stop"})}).catch(()=>{});});
  loadConfig(defaults);controls();drawWaveform(null);void poll();const stateTimer=setInterval(poll,350);
}
