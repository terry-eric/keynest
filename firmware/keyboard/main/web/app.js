"use strict";

function wavHeader(frames) {
  if (!Number.isInteger(frames) || frames < 0 || frames > 16000 * 300) throw new Error("Invalid WAV frame count");
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
if (typeof module === "object" && module.exports) {
  module.exports = { wavHeader, validateAudioChunk };
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
  const history = Array(70).fill(0), rows = [];
  const defaults = {led_mode:4,brightness:64,speed:80,volume:10,mic_gain:2,sound:2,color:[120,88,255],keycodes:[104,105,106,107],modifiers:[0,0,0,0]};
  const simulation = {config:structuredClone(defaults),dirty:false,keys:0,audio_ready:true,usb_connected:true,ssid:"PCB2-Keypad-DEMO",ip:"展示模式",mic_dbfs:-32,mic_peak:.12,recording:{sd_ready:false,flash_ready:true,mounting:false,downloading:false,state:0,target:0,last_target:1,frames:0,duration_ms:0,dropped_frames:0,limit_seconds:60,flash_limit_seconds:2.048,filename:"",error:""}};
  let demoStarted=0, demoDelivered=0;
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
  }
  async function request(path,body) {
    if (demo) return demoRequest(path,body);
    const response=await fetch(path,{method:body===undefined?"GET":"POST",cache:"no-store",signal:AbortSignal.timeout(3000),headers:body===undefined?{}:{"Content-Type":"application/json","X-PCB2-Control":"1"},body:body===undefined?undefined:JSON.stringify(body)});
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
    updateDemo();
    if (path==="/api/state") return structuredClone(simulation);
    if (path==="/api/config") {simulation.config=structuredClone(body);simulation.dirty=true;}
    if (path==="/api/save") simulation.dirty=false;
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
  }
  function targetHelp() {
    const target=$("record-target").value,r=state?.recording;
    $("target-help").textContent=target==="browser"?`不用 SD 卡，最長 ${r?.limit_seconds||60} 秒。請保持網頁開啟與 Wi-Fi 連線，完成後下載 WAV。`:target==="flash"?`先錄入 RAM，停止後寫入內建 Flash。最多 ${(r?.flash_limit_seconds||2.048).toFixed(2)} 秒；保存最新一段，重開機後可下載。`:`最長 ${r?.limit_seconds||60} 秒，建立新 WAV，不覆蓋舊檔。${r?.sd_ready?"microSD 已就緒。":"目前沒有可用的 microSD。"}`;
    controls();
  }
  function render(next) {
    state=next;
    if (!draft || !edited && JSON.stringify(draft)!==JSON.stringify(next.config)) loadConfig(next.config);
    if(!edited)$("save-state").textContent=next.dirty?"已套用・尚未保存":"設定已保存";
    $("usb-status").textContent=next.usb_connected?"USB HID 已連接":"USB 尚未連接";$("usb-status").classList.toggle("active",next.usb_connected);
    $("mic-status").textContent=next.audio_ready?"收音中":"音訊未就緒";$("mic-status").classList.toggle("active",next.audio_ready);
    $("dbfs").textContent=next.audio_ready?Number(next.mic_dbfs).toFixed(1):"—";$("meter-fill").style.width=next.audio_ready?Math.max(0,Math.min(100,(next.mic_dbfs+90)/90*100))+"%":"0%";
    history.push(next.audio_ready?next.mic_peak:0);history.shift();drawHistory();
    document.querySelectorAll("[data-trigger]").forEach(button=>button.classList.toggle("down",!!(next.keys&(1<<Number(button.dataset.trigger)))));
    const r=next.recording;$("rec-status").textContent=["待命","準備中","錄音中","完成與保存中","錄音已停止"][r.state]||"未知";$("rec-status").classList.toggle("active",r.state===2);
    $("record-dot").classList.toggle("live",r.state===2);const seconds=r.duration_ms/1000;$("rec-duration").textContent=String(Math.floor(seconds/60)).padStart(2,"0")+":"+(seconds%60).toFixed(1).padStart(4,"0");
    $("record-error").textContent=r.error || captureError || (r.dropped_frames?"資料遺失，請重新錄音。":"");
    $("flash-status").textContent=r.flash_ready?"內建 Flash 可用":"Flash 尚未就緒";$("sd-status").textContent=r.mounting?"SD 掛載中":r.sd_ready?"microSD 已就緒":"microSD 未掛載（可免卡錄音）";
    $("device-download").textContent=r.filename?"↓ 下載 "+r.filename:"↓ 裝置尚無保存的錄音";
    $("device-address").textContent=(demo?"展示模式":next.ssid+" · "+next.ip)+" · 離線控制頁面";targetHelp();
  }
  function drawHistory() {
    const canvas=$("level-history"),ctx=canvas.getContext("2d"),w=canvas.width,h=canvas.height;
    ctx.clearRect(0,0,w,h);ctx.strokeStyle="#333c51";ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(0,h-10);ctx.lineTo(w,h-10);ctx.stroke();
    ctx.strokeStyle="#57dec2";ctx.lineWidth=2;ctx.beginPath();history.forEach((value,i)=>{const x=i/(history.length-1)*w,y=h-10-Math.min(1,value)* (h-25);if(i)ctx.lineTo(x,y);else ctx.moveTo(x,y);});ctx.stroke();
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
        if(count){session.chunks.push(packet.buffer);session.frames+=count;}
        if(packet.dropped)throw new Error("錄音資料遺失，請保持 Wi-Fi 連線後重新錄音。");
        if([0,4].includes(packet.state)&&!count) {
          if(packet.state===4||session.frames!==packet.frames)throw new Error("錄音未完整接收，請重新錄音。");
          if(!session.frames)throw new Error("沒有收到收音資料。");
          const blob=new Blob([wavHeader(session.frames),...session.chunks],{type:"audio/wav"});
          if(downloadURL)URL.revokeObjectURL(downloadURL);downloadURL=URL.createObjectURL(blob);
          $("browser-download").href=downloadURL;$("browser-download").download="PCB2-"+Date.now()+".wav";$("browser-download").classList.remove("disabled");
          toast("錄音完成，可下載到手機或電腦。");break;
        }
        await new Promise(resolve=>setTimeout(resolve,40));
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
      else {const response=await fetch("/api/recording.wav",{signal:AbortSignal.timeout(60000)});if(!response.ok)throw new Error("無法下載裝置錄音");blob=await response.blob();}
      const url=URL.createObjectURL(blob),link=document.createElement("a");link.href=url;link.download=state.recording.filename||"PCB2.wav";document.body.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),60000);
    } finally {downloading=false;controls();}
  }));
  window.addEventListener("pagehide",()=>{if(capture&&!demo)void fetch("/api/record",{method:"POST",keepalive:true,headers:{"Content-Type":"application/json","X-PCB2-Control":"1"},body:JSON.stringify({action:"stop"})}).catch(()=>{});});
  loadConfig(defaults);controls();void poll();setInterval(poll,350);
}
