"use strict";
const PCB2MacroKeys = new Set([..."ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789`-=[]\\;',./", ...Array.from({length:24},(_,i)=>`F${i+1}`), "CTRL","SHIFT","ALT","META","RCTRL","RSHIFT","RALT","RMETA","ENTER","ESC","BACKSPACE","TAB","SPACE","CAPSLOCK","INSERT","HOME","PAGEUP","DELETE","END","PAGEDOWN","LEFT","RIGHT","UP","DOWN","PRINTSCREEN","SCROLLLOCK","PAUSE","NUMLOCK","VOLUMEUP","VOLUMEDOWN","MUTE","PLAYPAUSE","NEXT","PREVIOUS"]);
function pcb2Chord(value) {
  const parts = value.toUpperCase().split("+").map(key=>key.trim()), keys = parts.filter(Boolean);
  if (parts.some(key=>!key) || !keys.length || keys.length > 16 || new Set(keys).size !== keys.length || keys.some(key=>!PCB2MacroKeys.has(key))) throw new Error("組合請用 + 分隔，例如 CTRL + SHIFT + A；最多 16 個不同按鍵。");
  return keys;
}
function pcb2ValidateProgram(program) {
  const fields = (value,names)=>value && Object.keys(value).length===names.length && names.every(name=>Object.hasOwn(value,name));
  if (!fields(program,["name","steps"]) || typeof program.name !== "string" || new TextEncoder().encode(program.name).length > 64 || !Array.isArray(program.steps) || program.steps.length > 16) throw new Error("名稱最多 64 bytes，巨集最多 16 步。");
  const validString = value => typeof value === "string" && !/[\u0000-\u0008\u000b-\u001f\u007f]/u.test(value) && !/[\ud800-\udfff]/u.test(value);
  if (!validString(program.name)) throw new Error("動作名稱含有無效字元。");
  let delay = 0;
  for (const step of program.steps) {
    if (!step || typeof step !== "object") throw new Error("無效的巨集步驟。");
    if (step.type === "keys" && fields(step,["type","keys"])) {
      if (!Array.isArray(step.keys) || step.keys.length < 1 || step.keys.length > 16 || new Set(step.keys).size !== step.keys.length || step.keys.some(key=>!PCB2MacroKeys.has(key))) throw new Error("請確認組合鍵，最多 16 個不同按鍵。");
    } else if (step.type === "text" && fields(step,["type","text"])) {
      if (!validString(step.text) || !step.text || new TextEncoder().encode(step.text).length > 512) throw new Error("請輸入文字；每段最多 512 bytes（約 170 個中文字）。");
    } else if (step.type === "wait" && fields(step,["type","ms"])) {
      if (!Number.isInteger(step.ms) || step.ms < 0 || step.ms > 5000) throw new Error("每次等待須為 0～5000 毫秒。"); delay += step.ms;
    } else throw new Error("不支援的巨集步驟。");
  }
  if (delay > 30000 || new TextEncoder().encode(JSON.stringify(program)).length > 1200) throw new Error("整個巨集最多 1200 bytes，等待合計最多 30 秒；請縮短文字或步驟。");
  return program;
}
function pcb2Shortcut(event) {
  const codes = {Enter:40,Escape:41,Backspace:42,Tab:43,Space:44,CapsLock:57,PrintScreen:70,ScrollLock:71,Pause:72,Insert:73,Home:74,PageUp:75,Delete:76,End:77,PageDown:78,ArrowRight:79,ArrowLeft:80,ArrowDown:81,ArrowUp:82,Minus:45,Equal:46,BracketLeft:47,BracketRight:48,Backslash:49,Semicolon:51,Quote:52,Backquote:53,Comma:54,Period:55,Slash:56,NumLock:83,NumpadDivide:84,NumpadMultiply:85,NumpadSubtract:86,NumpadAdd:87,NumpadEnter:88,NumpadDecimal:99};
  let code = codes[event.code];
  if (/^Key[A-Z]$/.test(event.code)) code = event.code.charCodeAt(3)-65+4;
  if (/^Digit[0-9]$/.test(event.code)) code = event.code === "Digit0" ? 39 : Number(event.code.slice(5))+29;
  if (/^Numpad[0-9]$/.test(event.code)) code = event.code === "Numpad0" ? 98 : Number(event.code.slice(6))+88;
  const fn = /^F([0-9]+)$/.exec(event.code); if (fn && Number(fn[1]) <= 24) code = Number(fn[1]) <= 12 ? Number(fn[1])+57 : Number(fn[1])+91;
  if (!code || event.isComposing || event.repeat) return null;
  return {code, modifiers:[event.ctrlKey,event.shiftKey,event.altKey,event.metaKey]};
}
if (typeof module === "object" && module.exports) module.exports = {pcb2Chord, pcb2ValidateProgram, pcb2Shortcut};
else {
  const transport = window.PCB2Transport, originalFetch = transport.fetch.bind(transport);
  let stateHandler = null;
  transport.fetch = async (...args) => {
    const response = await originalFetch(...args);
    if (args[0] === "/api/state" && response.ok) stateHandler?.(await response.clone().json());
    return response;
  };
  document.addEventListener("DOMContentLoaded", () => {
    const $ = id=>document.getElementById(id), demo = new URLSearchParams(location.search).get("demo") === "1", mac = /Mac|iPhone|iPad/.test(navigator.platform);
    const rows = [...$("mappings").children], keys = [...document.querySelectorAll("[data-edit-key]")], tabs = [...document.querySelectorAll("[data-page]")];
    const extraNames=new Map([[45,"−"],[46,"="],[47,"["],[48,"]"],[49,"\\"],[51,";"],[52,"'"],[53,"`"],[54,","],[55,"."],[56,"/"],[70,"Print Screen"],[71,"Scroll Lock"],[72,"Pause"],[83,"Num Lock"],[84,"數字鍵盤 /"],[85,"數字鍵盤 *"],[86,"數字鍵盤 −"],[87,"數字鍵盤 +"],[88,"數字鍵盤 Enter"],[99,"數字鍵盤 ."]]);
    for(let i=0;i<10;i++)extraNames.set(i?88+i:98,`數字鍵盤 ${i}`);
    rows.forEach(row=>{const select=row.querySelector("select");for(const [code,name] of extraNames)if(![...select.options].some(option=>Number(option.value)===code))select.add(new Option(name,code));});
    let selected = 0, latest = null, loaded = demo, loading = false, loadFailed = false, saving = false, savedClick = false, capture = null, captureKey = 0;
    let programs = Array.from({length:4},()=>({name:"",steps:[]})), kinds = Array(4).fill("shortcut"); const changed = new Set();
    const ready = ()=>demo || $("connection").classList.contains("online");
    const supported = ()=>demo || latest?.macros?.version === 1;
    function toast(message, error = false) { $("toast").textContent=message; $("toast").classList.toggle("error",error); $("toast").hidden=false; setTimeout(()=>$("toast").hidden=true,5000); }
    function page(name, focus = false) {
      tabs.forEach(tab=>{const active=tab.dataset.page===name; tab.setAttribute("aria-selected",String(active));tab.tabIndex=active?0:-1; $(tab.getAttribute("aria-controls")).hidden=!active;if(active&&focus)tab.focus();});
      $("page-subtitle").textContent={keys:"點選一顆鍵，設定它要幫你做什麼。",effects:"調整喜歡的燈光與聲音，再儲存到鍵盤。",audio:"先看收音反應，再開始錄音；不需要 SD 卡。"}[name];
    }
    tabs.forEach((tab,i)=>{
      tab.addEventListener("click",()=>page(tab.dataset.page));
      tab.addEventListener("keydown",event=>{let at;if(event.key==="ArrowRight")at=(i+1)%3;else if(event.key==="ArrowLeft")at=(i+2)%3;else if(event.key==="Home")at=0;else if(event.key==="End")at=2;else return;event.preventDefault();page(tabs[at].dataset.page,true);});
    });
    function labelPrograms() {
      programs.forEach((p,i)=>{
        const active = kinds[i]!=="shortcut";
        if (active) { $("key-label-"+i).textContent=p.name||{text:"文字",chord:"組合鍵",macro:"巨集"}[kinds[i]]; $("key-mod-"+i).textContent=kinds[i]==="macro"?`${p.steps.length} 個步驟`:kinds[i]==="text"?"輸入一段文字":"同時送出多鍵"; }
        else { const select=rows[i].querySelector("select"); if(select.selectedOptions.length)$("key-label-"+i).textContent=select.selectedOptions[0].textContent; }
        keys[i].setAttribute("aria-label",`設定第 ${i+1} 顆鍵：${$("key-label-"+i).textContent}`);
      });
    }
    function status() {
      const online=ready(); $("key-fields").disabled=!online||saving||!loaded&&supported();
      document.querySelectorAll("[data-requires-device]").forEach(field=>field.disabled=!online);
      const base=$("save-state").textContent;
      $("save-summary").textContent=!online?"接上鍵盤即可開始設定":saving?"正在儲存，請保持連線…":changed.size?`${changed.size} 顆按鍵有未儲存的動作`:base.includes("尚未套用")?"設定已修改，準備好就儲存":base.includes("尚未保存")?"目前已生效，重啟後還不會保留":"設定已儲存";
      $("save-help").textContent=changed.size?"文字與巨集會儲存到鍵盤；執行時請保持桌面工具開啟。":"儲存後，重新插拔鍵盤也會保留設定；暫時套用只在這次連接生效。";
      $("apply").hidden=changed.size>0;
      document.querySelector(".save-dock").dataset.macroDirty=String(changed.size>0);
      $("save").disabled=$("save").disabled||saving;
      $("companion-note").hidden=kinds[selected]==="shortcut";
      $("macro-support").textContent=!supported()?"這塊板子需要更新巨集韌體":latest?.companion_error?"桌面工具需要檢查":latest?.companion_ready?"桌面工具已就緒":"文字與巨集需要桌面工具";
      $("macro-support-detail").textContent=!supported()?"目前韌體仍可設定普通快捷鍵；更新後才能把文字與多步巨集儲存到板子。":latest?.companion_error?latest.companion_error:latest?.companion_ready?"切到目標程式，再按實體鍵即可執行。忙碌時不會堆積下一次巨集。":"儲存後請啟動桌面工具，並使用它開啟的本機控制台；工具會管理 USB 連線。";
      $("audio-running").hidden=$("monitor-toggle").getAttribute("aria-pressed")!=="true"&&!$("record-dot").classList.contains("live");
      labelPrograms();
    }
    function mark() { changed.add(selected); status(); }
    function editor() {
      keys.forEach((key,i)=>key.setAttribute("aria-pressed",String(i===selected)));
      rows.forEach((row,i)=>row.hidden=i!==selected);
      document.querySelectorAll("[data-test-key]").forEach(button=>button.hidden=Number(button.dataset.testKey)!==selected);
      $("editor-number").textContent=String(selected+1).padStart(2,"0"); $("editor-title").textContent=`設定第 ${selected+1} 顆鍵`;
      $("action-kind").value=kinds[selected]; $("editor-kind").textContent={shortcut:"快捷鍵",chord:"組合鍵",text:"文字",macro:"巨集"}[kinds[selected]];
      $("shortcut-editor").hidden=kinds[selected]!=="shortcut"; $("program-editor").hidden=kinds[selected]==="shortcut";
      $("macro-name").value=programs[selected].name; $("add-steps").hidden=kinds[selected]!=="macro";
      renderSteps(); status();
    }
    keys.forEach((key,i)=>key.addEventListener("click",()=>{selected=i;editor();}));
    $("action-kind").addEventListener("change",()=>{
      const kind=$("action-kind").value; kinds[selected]=kind;
      if(kind==="shortcut")programs[selected]={name:"",steps:[]};
      else if(kind==="text"&&!(programs[selected].steps.length===1&&programs[selected].steps[0].type==="text"))programs[selected].steps=[{type:"text",text:""}];
      else if(kind==="chord"&&!(programs[selected].steps.length===1&&programs[selected].steps[0].type==="keys"))programs[selected].steps=[{type:"keys",keys:["CTRL","C"]}];
      else if(kind==="macro"&&!programs[selected].steps.length)programs[selected].steps=[{type:"keys",keys:["CTRL","C"]},{type:"wait",ms:200}];
      mark();editor();
    });
    $("macro-name").addEventListener("input",()=>{programs[selected].name=$("macro-name").value;mark();});
    function counts() { $("macro-count").textContent=`${programs[selected].steps.length} / 16 步 · 已使用 ${Math.ceil(new TextEncoder().encode(JSON.stringify(programs[selected])).length/12)}% 內容容量`; }
    function renderSteps() {
      $("macro-steps").replaceChildren(); $("macro-error").textContent="";
      programs[selected].steps.forEach((step,index)=>{
        const card=document.createElement("div");card.className="step-card";
        const head=document.createElement("div");head.className="step-head";const title=document.createElement("strong");title.textContent=`${index+1}. ${ {keys:"按鍵組合",text:"輸入文字",wait:"等待"}[step.type]}`;head.append(title);
        if(kinds[selected]==="macro"){
          const tools=document.createElement("div");tools.className="step-tools";
          for(const [text,label,move] of [["↑","上移",-1],["↓","下移",1],["×","刪除",0]]){const button=document.createElement("button");button.textContent=text;button.setAttribute("aria-label",`${label}第 ${index+1} 步`);button.disabled=move!==0&&(index+move<0||index+move>=programs[selected].steps.length);button.addEventListener("click",()=>{const steps=programs[selected].steps;if(move)[steps[index],steps[index+move]]=[steps[index+move],steps[index]];else steps.splice(index,1);mark();renderSteps();});tools.append(button);}head.append(tools);
        }
        card.append(head);const input=document.createElement(step.type==="text"?"textarea":"input"); input.setAttribute("aria-label",`第 ${index+1} 步${{keys:"組合鍵",text:"文字",wait:"等待毫秒"}[step.type]}`);
        if(step.type==="text"){input.value=step.text;input.placeholder="輸入中文、英文或常用回覆…";}
        else if(step.type==="keys"){input.type="text";input.value=step.keys.join(" + ");input.placeholder="例如 CTRL + SHIFT + A + B";}
        else {input.type="number";input.min=0;input.max=5000;input.step=50;input.value=step.ms;}
        input.addEventListener("input",()=>{try{if(step.type==="keys")step.keys=pcb2Chord(input.value);else if(step.type==="text")step.text=input.value;else step.ms=Number(input.value); input.setCustomValidity("");$("macro-error").textContent="";}catch(error){if(step.type==="keys")step.keys=[];input.setCustomValidity(error.message);$("macro-error").textContent=error.message;}mark();counts();});
        card.append(input);const help=document.createElement("p");help.className="caption";help.textContent=step.type==="keys"?"用 + 分隔。修飾鍵：CTRL、SHIFT、ALT、META（⌘／Win），也可使用 F1–F24、方向鍵與媒體鍵。":step.type==="text"?"支援中文與換行；每段最多 512 bytes。":"單位為毫秒，1000 = 1 秒。";card.append(help);$("macro-steps").append(card);
      }); counts();
    }
    document.querySelectorAll("[data-add-step]").forEach(button=>button.addEventListener("click",()=>{if(programs[selected].steps.length>=16){toast("最多 16 個步驟。",true);return;}const type=button.dataset.addStep;programs[selected].steps.push(type==="keys"?{type,keys:["ENTER"]}:type==="text"?{type,text:""}:{type,ms:200});mark();renderSteps();}));
    function setShortcut(shortcut,index) {
      const row=rows[index],select=row.querySelector("select");if(![...select.options].some(option=>Number(option.value)===shortcut.code))select.add(new Option(`HID 0x${shortcut.code.toString(16)}`,shortcut.code));
      select.value=String(shortcut.code);[...row.querySelectorAll("input")].forEach((input,bit)=>input.checked=shortcut.modifiers[bit]);select.dispatchEvent(new Event("change",{bubbles:true}));
    }
    document.querySelectorAll("[data-preset]").forEach(button=>button.addEventListener("click",()=>{const presets={copy:6,paste:25,undo:29,space:44};setShortcut({code:presets[button.dataset.preset],modifiers:button.dataset.preset==="space"?[false,false,false,false]:[!mac,false,false,mac]},selected);}));
    $("capture-shortcut").addEventListener("click",()=>{capture=null;captureKey=selected;$("capture-value").textContent="等待你的按鍵…";$("capture-confirm").disabled=true;$("shortcut-dialog").showModal();});
    $("shortcut-dialog").addEventListener("keydown",event=>{if(event.key==="Escape"||event.target===$("capture-confirm"))return;event.preventDefault();event.stopPropagation();const result=pcb2Shortcut(event);if(!result)return;capture=result;$("capture-value").textContent=[...result.modifiers.map((value,i)=>value?["Ctrl","Shift","Alt",mac?"⌘":"Win"][i]:"").filter(Boolean),event.code.replace(/^Key|^Digit/,"")].join(" + ");$("capture-confirm").disabled=false;});
    $("capture-confirm").addEventListener("click",()=>{if(capture)setShortcut(capture,captureKey);$("shortcut-dialog").close();});
    async function request(path,body) {
      const response=await transport.fetch(path,{method:body?"POST":"GET",headers:body?{"Content-Type":"application/json","X-PCB2-Control":"1"}:{},body:body?JSON.stringify(body):undefined,signal:AbortSignal.timeout(5000)});
      const result=await response.json();if(!response.ok)throw new Error(result.error||"鍵盤沒有接受這次設定。");return result;
    }
    stateHandler=next=>{
      latest=next;
      if(supported()&&!loaded&&!loading&&!loadFailed){loading=true;void Promise.all([0,1,2,3].map(i=>request(`/api/macro/${i}`))).then(values=>{values.forEach(pcb2ValidateProgram);programs=values;kinds=values.map(p=>!p.steps.length?"shortcut":p.steps.length===1&&p.steps[0].type==="text"?"text":p.steps.length===1&&p.steps[0].type==="keys"?"chord":"macro");loaded=true;editor();}).catch(error=>{loadFailed=true;toast("讀取巨集失敗，請重新連接："+error.message,true);}).finally(()=>loading=false);}
      if(!supported())loaded=true;
    };
    $("save").addEventListener("click",async event=>{
      if(savedClick||!changed.size)return;
      event.stopImmediatePropagation(); if(saving)return;
      try {
        if(!supported())throw new Error("目前韌體不支援文字與巨集，請先更新板子。");
        if(!$("key-fields").reportValidity())return;
        for(const i of changed)pcb2ValidateProgram(programs[i]);
        saving=true;status();
        if(!demo)for(const i of [...changed]){await request("/api/macro",{key:i,program:programs[i]});changed.delete(i);}
        else changed.clear();
        saving=false;$("save").disabled=false;savedClick=true;$("save").click();savedClick=false;status();
      } catch(error){saving=false;$("save").disabled=!ready();$("macro-error").textContent=error.message;toast(error.message,true);status();}
    },true);
    $("sound-recording").addEventListener("click",()=>{document.querySelector(".custom-sound").open=true;});
    $("sd-file-sound").addEventListener("click",()=>{page("effects");document.querySelector(".custom-sound").open=true;});
    $("reconnect").addEventListener("click",()=>{if(!changed.size){loaded=false;loadFailed=false;}});
    for(const id of ["serial-choose","serial-connect","serial-disconnect"]){$(id).addEventListener("click",event=>{if(changed.size){event.stopImmediatePropagation();toast("先儲存按鍵動作，再切換或中斷鍵盤。",true);}else if(id!=="serial-disconnect"){loaded=false;loadFailed=false;}},true);}
    new MutationObserver(()=>status()).observe($("save-state"),{childList:true,characterData:true,subtree:true});
    new MutationObserver(()=>{if(!ready()&&!changed.size){loaded=false;loadFailed=false;}status();}).observe($("connection"),{attributes:true,attributeFilter:["class"]});
    new MutationObserver(()=>status()).observe($("monitor-toggle"),{attributes:true,attributeFilter:["aria-pressed"]});
    new MutationObserver(()=>status()).observe($("record-dot"),{attributes:true,attributeFilter:["class"]});
    // Maintained app updates board labels when loading/saving settings; restore macro summaries.
    new MutationObserver(()=>{if(programs.some(p=>p.steps.length))labelPrograms();}).observe($("board"),{attributes:true,attributeFilter:["class"]});
    document.querySelectorAll("[data-test-key]").forEach(button=>new MutationObserver(()=>{const key=keys[Number(button.dataset.testKey)];key.classList.toggle("down",button.classList.contains("down"));key.classList.toggle("test",button.classList.contains("test"));}).observe(button,{attributes:true,attributeFilter:["class"]}));
    editor();
  });
}
