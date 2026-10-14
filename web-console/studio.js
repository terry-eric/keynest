"use strict";
const PCB2MacroKeys = new Set([..."ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789`-=[]\\;',./", ...Array.from({length:24},(_,i)=>`F${i+1}`), "CTRL","SHIFT","ALT","META","RCTRL","RSHIFT","RALT","RMETA","ENTER","ESC","BACKSPACE","TAB","SPACE","CAPSLOCK","INSERT","HOME","PAGEUP","DELETE","END","PAGEDOWN","LEFT","RIGHT","UP","DOWN","PRINTSCREEN","SCROLLLOCK","PAUSE","NUMLOCK","VOLUMEUP","VOLUMEDOWN","MUTE","PLAYPAUSE","NEXT","PREVIOUS"]);
function pcb2Chord(value) {
  const parts = value.toUpperCase().split("+").map(key=>key.trim()), keys = parts.filter(Boolean);
  if (parts.some(key=>!key) || !keys.length || keys.length > 16 || new Set(keys).size !== keys.length || keys.some(key=>!PCB2MacroKeys.has(key))) throw new Error("組合請用 + 分隔，例如 CTRL + SHIFT + A；最多 16 個不同按鍵。");
  return keys;
}
function pcb2ValidateProgram(program) {
  const fields = (value,names)=>value && Object.keys(value).length===names.length && names.every(name=>Object.hasOwn(value,name));
  if ((!fields(program,["name","steps"]) && !fields(program,["name","steps","text_mode"])) || typeof program.name !== "string" || new TextEncoder().encode(program.name).length > 64 || !Array.isArray(program.steps) || program.steps.length > 16) throw new Error("名稱最多 64 bytes，巨集最多 16 步。");
  if(program.text_mode!==undefined && !["ascii","linux","macos"].includes(program.text_mode))throw new Error("請選擇支援的文字輸入方式。");
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
function pcb2CapturedKey(event) {
  if(event.isComposing || event.repeat)return null;
  if(/^Key[A-Z]$/.test(event.code))return event.code.slice(3);
  if(/^Digit[0-9]$/.test(event.code))return event.code.slice(5);
  if(/^F(?:[1-9]|1[0-9]|2[0-4])$/.test(event.code))return event.code;
  const names={ControlLeft:"CTRL",ControlRight:"RCTRL",ShiftLeft:"SHIFT",ShiftRight:"RSHIFT",AltLeft:"ALT",AltRight:"RALT",MetaLeft:"META",MetaRight:"RMETA",Enter:"ENTER",Escape:"ESC",Backspace:"BACKSPACE",Tab:"TAB",Space:"SPACE",CapsLock:"CAPSLOCK",Insert:"INSERT",Home:"HOME",PageUp:"PAGEUP",Delete:"DELETE",End:"END",PageDown:"PAGEDOWN",ArrowLeft:"LEFT",ArrowRight:"RIGHT",ArrowUp:"UP",ArrowDown:"DOWN",Minus:"-",Equal:"=",BracketLeft:"[",BracketRight:"]",Backslash:"\\",Semicolon:";",Quote:"'",Backquote:"`",Comma:",",Period:".",Slash:"/",PrintScreen:"PRINTSCREEN",ScrollLock:"SCROLLLOCK",Pause:"PAUSE",NumLock:"NUMLOCK",AudioVolumeUp:"VOLUMEUP",AudioVolumeDown:"VOLUMEDOWN",AudioVolumeMute:"MUTE",MediaPlayPause:"PLAYPAUSE",MediaTrackNext:"NEXT",MediaTrackPrevious:"PREVIOUS"};
  return names[event.code]||null;
}
function pcb2AppendCaptured(keys,event) {
  const key=pcb2CapturedKey(event);if(!key)return keys.slice();
  const result=keys.slice();
  // Recover modifiers already held before the dialog received focus.
  for(const [flag,left,right] of [["ctrlKey","CTRL","RCTRL"],["shiftKey","SHIFT","RSHIFT"],["altKey","ALT","RALT"],["metaKey","META","RMETA"]])
    if(event[flag] && key!==left && key!==right && !result.includes(left) && !result.includes(right))result.push(left);
  if(!result.includes(key))result.push(key);
  if(result.length>16)throw new Error("最多擷取 16 個不同按鍵，請移除一顆後再加入。");
  return result;
}
function pcb2ActionLabel(keys,mac=false){
  if(keys.length===2&&(mac?["META","RMETA"]:["CTRL","RCTRL"]).includes(keys[0])){const preset={C:"複製",V:"貼上",Z:"復原"}[keys[1]];if(preset)return preset;}
  const names={CTRL:"Ctrl",RCTRL:"右 Ctrl",SHIFT:"Shift",RSHIFT:"右 Shift",ALT:"Alt",RALT:"右 Alt",META:mac?"⌘":"Win",RMETA:mac?"右 ⌘":"右 Win",SPACE:"空白",ENTER:"Enter",ESC:"Esc",LEFT:"←",RIGHT:"→",UP:"↑",DOWN:"↓"};
  return keys.map(key=>names[key]||key).join(" + ");
}
function pcb2MotionEnabled(preference,reduced){return preference==='full'||preference!=='reduced'&&!reduced;}
function pcb2TextIssue(program){
  const text=program.steps.filter(step=>step.type==='text').map(step=>step.text).join('');
  if(/[^\x00-\x7f]/u.test(text)&&(!program.text_mode||program.text_mode==='ascii'))return '這段文字包含中文或其他 Unicode 字元。請先選擇並設定對應的系統輸入方式；Windows 暫無通用的免工具中文模式。';
  if(program.text_mode==='macos'&&[...text].some(c=>c.codePointAt(0)>0xffff))return 'macOS 模式暫不支援 emoji 等 U+FFFF 以上字元，請移除後再儲存。';
  return '';
}
if (typeof module === "object" && module.exports) module.exports = {pcb2Chord, pcb2ValidateProgram, pcb2Shortcut, pcb2CapturedKey, pcb2AppendCaptured, pcb2ActionLabel, pcb2MotionEnabled, pcb2TextIssue};
else {
  const transport = window.PCB2Transport, originalFetch = transport.fetch.bind(transport);
  let stateHandler = null, operationHandler = null;
  transport.fetch = async (...args) => {
    const mutation=["/api/config","/api/save","/api/macro"].includes(args[0]);
    if(mutation)operationHandler?.(1);
    try{const response = await originalFetch(...args);if (args[0] === "/api/state" && response.ok) stateHandler?.(await response.clone().json());return response;}
    finally{if(mutation)operationHandler?.(-1);}
  };
  document.addEventListener("DOMContentLoaded", () => {
    const $ = id=>document.getElementById(id), demo = new URLSearchParams(location.search).get("demo") === "1", mac = /Mac|iPhone|iPad/.test(navigator.platform);
    const rows = [...$("mappings").children], keys = [...document.querySelectorAll("[data-edit-key]")], tabs = [...document.querySelectorAll("[data-page]")];
    const choices=[...document.querySelectorAll('[data-action-kind]')];
    keys.forEach((key,i)=>key.querySelector('.key-name').textContent=`第 ${i+1} 顆`);
    const navMedia=window.matchMedia('(max-width:900px)');const navOrientation=()=>document.querySelector('.studio-tabs').setAttribute('aria-orientation',navMedia.matches?'horizontal':'vertical');navMedia.addEventListener('change',navOrientation);navOrientation();
    const extraNames=new Map([[45,"−"],[46,"="],[47,"["],[48,"]"],[49,"\\"],[51,";"],[52,"'"],[53,"`"],[54,","],[55,"."],[56,"/"],[70,"Print Screen"],[71,"Scroll Lock"],[72,"Pause"],[83,"Num Lock"],[84,"數字鍵盤 /"],[85,"數字鍵盤 *"],[86,"數字鍵盤 −"],[87,"數字鍵盤 +"],[88,"數字鍵盤 Enter"],[99,"數字鍵盤 ."]]);
    for(let i=0;i<10;i++)extraNames.set(i?88+i:98,`數字鍵盤 ${i}`);
    rows.forEach(row=>{const select=row.querySelector("select");for(const [code,name] of extraNames)if(![...select.options].some(option=>Number(option.value)===code))select.add(new Option(name,code));});
    let selected = 0, latest = null, loaded = demo, loading = false, loadFailed = false, saving = false, savedClick = false, capture = null, captureKey = 0, captureStep = null, capturedKeys = [], pendingOperations = 0;
    const motion=setupMotion(), animate=target=>motion.reveal('capture-'+target.id,target,{y:12,scale:.95,duration:.36});
    let previousOnline=null,previousWriting=false,savedTimer;
    function setupMotion(){
      const gsap=window.gsap, media=window.matchMedia('(prefers-reduced-motion: reduce)'), contexts=new Map();
      let preference=null,enabled=false;
      try{preference=localStorage.getItem('pcb2-motion');}catch{}
      const visible=targets=>(Array.isArray(targets)?targets:[targets]).filter(node=>node?.isConnected&&node.getClientRects().length);
      function reset(slot){contexts.get(slot)?.revert();contexts.delete(slot);}
      function clear(){contexts.forEach(context=>context.revert());contexts.clear();}
      function play(slot,build){reset(slot);if(enabled&&gsap)contexts.set(slot,gsap.context(build,document.body));}
      function reveal(slot,targets,{y=22,scale=1,duration=.46,stagger=.06}={}){
        const nodes=visible(targets);if(!nodes.length){reset(slot);return;}
        play(slot,()=>gsap.fromTo(nodes,{y,scale,opacity:.18},{y:0,scale:1,opacity:1,duration,stagger,ease:'power3.out',clearProps:'transform,opacity'}));
      }
      function key(target){
        play(target,()=>{
          gsap.fromTo(target,{scale:.92},{scale:1,duration:.42,ease:'back.out(2)',clearProps:'transform'});
          gsap.fromTo(target,{boxShadow:'0 0 0 0 rgba(49,94,209,.36)'},{boxShadow:'0 0 0 18px rgba(49,94,209,0)',duration:.52,ease:'power2.out',clearProps:'boxShadow'});
        });
      }
      function page(target){reset('entrance');reset('editor');reveal('page',[...target.querySelectorAll('.key-overview,.editor-panel')].concat(target.id==='page-keys'?[]:[...target.children]),{y:28,duration:.52,stagger:.08});}
      function entrance(){reveal('entrance',[document.querySelector('.brand'),...tabs,document.querySelector('.heading'),document.querySelector('.usb-panel'),document.querySelector('.setup-progress'),document.querySelector('.key-overview'),document.querySelector('.editor-panel')],{y:20,duration:.58,stagger:.045});}
      function update(){
        clear();enabled=!!gsap&&pcb2MotionEnabled(preference,media.matches);
        document.body.dataset.motion=enabled?'full':'reduced';
        const button=$('motion-toggle');button.disabled=!gsap;button.setAttribute('aria-pressed',String(enabled));
        button.textContent=gsap?enabled?'動畫開啟':'動畫關閉':'動畫未載入';
        button.title=enabled?'關閉介面動畫':'開啟介面動畫';
      }
      $('motion-toggle').addEventListener('click',()=>{preference=enabled?'reduced':'full';try{localStorage.setItem('pcb2-motion',preference);}catch{}update();if(enabled)entrance();});
      media.addEventListener('change',update);
      window.addEventListener('storage',event=>{if(event.key==='pcb2-motion'){preference=event.newValue;update();}});
      window.addEventListener('pagehide',clear);
      window.addEventListener('pageshow',event=>{if(event.persisted){update();entrance();}});
      document.querySelectorAll('details').forEach(details=>details.addEventListener('toggle',()=>{if(details.open)reveal(details,[...details.children].slice(1),{y:10,duration:.34,stagger:.03});else reset(details);}));
      $('connection-manage').addEventListener('click',()=>{if(document.querySelector('.usb-panel').dataset.expanded==='true')reveal('connection-options',[$('usb-connect-description'),$('serial-note'),document.querySelector('.usb-more')],{y:10,duration:.34});else reset('connection-options');});
      update();return{reset,reveal,key,page,entrance,saved(){
        play('saved',()=>gsap.fromTo(document.querySelector('.save-dock'),{boxShadow:'0 0 0 0 rgba(32,123,89,.3)'},{boxShadow:'0 0 0 14px rgba(32,123,89,0)',duration:.65,ease:'power2.out',clearProps:'boxShadow'}));
      }};
    }
    let programs = Array.from({length:4},()=>({name:"",steps:[]})), kinds = Array(4).fill("shortcut"); const changed = new Set();
    const ready = ()=>demo || $("connection").classList.contains("online");
    const supported = ()=>demo || latest?.macros?.version === 1;
    function toast(message, error = false) { $("toast").textContent=message; $("toast").classList.toggle("error",error); $("toast").hidden=false; setTimeout(()=>$("toast").hidden=true,5000); }
    function page(name, focus = false) {
      const previous=tabs.find(tab=>tab.getAttribute('aria-selected')==='true')?.dataset.page;
      tabs.forEach(tab=>{const active=tab.dataset.page===name; tab.setAttribute("aria-selected",String(active));tab.tabIndex=active?0:-1; $(tab.getAttribute("aria-controls")).hidden=!active;if(active&&focus)tab.focus();});
      $("page-subtitle").textContent={keys:"點選一顆鍵，設定它要幫你做什麼。",effects:"調整喜歡的燈光與聲音，再儲存到鍵盤。",audio:"先看收音反應，再開始錄音；不需要 SD 卡。"}[name];
      $("page-title").textContent={keys:"設定你的鍵盤",effects:"調整燈光與音效",audio:"收音與錄音"}[name];
      if(previous!==name)motion.page($('page-'+name));
    }
    tabs.forEach((tab,i)=>{
      tab.addEventListener("click",()=>page(tab.dataset.page));
      tab.addEventListener("keydown",event=>{let at;if(event.key==="ArrowRight"||event.key==="ArrowDown")at=(i+1)%3;else if(event.key==="ArrowLeft"||event.key==="ArrowUp")at=(i+2)%3;else if(event.key==="Home")at=0;else if(event.key==="End")at=2;else return;event.preventDefault();page(tabs[at].dataset.page,true);});
    });
    function labelPrograms() {
      programs.forEach((p,i)=>{
        const active = kinds[i]!=="shortcut";
        keys[i].dataset.macro=String(active);
        if (active) { $("key-label-"+i).textContent=p.name||(kinds[i]==="chord"?pcb2ActionLabel(p.steps[0]?.keys||[],mac):{text:"輸入文字",macro:"連續動作"}[kinds[i]]); $("key-mod-"+i).textContent=kinds[i]==="macro"?`${p.steps.length} 個步驟`:kinds[i]==="text"?"輸入一段文字":"多鍵組合"; }
        else { const select=rows[i].querySelector("select");if(select.selectedOptions.length){const code=Number(select.value),flags=[...rows[i].querySelectorAll('input')].map(input=>input.checked),modifierNames=['CTRL','SHIFT','ALT','META'],label=select.selectedOptions[0].textContent,parts=modifierNames.filter((_,bit)=>flags[bit]);parts.push(label.toUpperCase());$("key-label-"+i).textContent=pcb2ActionLabel(parts,mac);if(i===selected)$("shortcut-preview").textContent=parts.map(key=>key==='META'?(mac?'⌘':'Win'):key==='CTRL'?'Ctrl':key==='SHIFT'?'Shift':key==='ALT'?'Alt':key).join(' + ');}}
        keys[i].setAttribute("aria-label",`設定第 ${i+1} 顆鍵：${$("key-label-"+i).textContent}`);
        keys[i].title=keys[i].getAttribute("aria-label");
      });
    }
    function status() {
      const online=ready(),writing=saving||pendingOperations>0; $("key-fields").disabled=!online||writing||!loaded&&supported();
      document.body.dataset.connected=String(online);
      const dock=document.querySelector('.save-dock');dock.dataset.writing=String(writing);
      if(writing&&!previousWriting)dock.dataset.saved='false';previousWriting=writing;
      if(previousOnline!==null&&previousOnline!==online){motion.reveal('connection',document.querySelector('.usb-panel'),{y:0,scale:.98,duration:.4});if(online)motion.page($('page-'+tabs.find(tab=>tab.getAttribute('aria-selected')==='true').dataset.page));}
      previousOnline=online;
      $("connection-placeholder").hidden=online;
      choices.forEach(choice=>{const active=choice.dataset.actionKind===kinds[selected];choice.setAttribute('aria-checked',String(active));choice.tabIndex=active?0:-1;choice.disabled=$("key-fields").disabled;});
      keys.forEach(key=>key.disabled=$("key-fields").disabled);
      document.querySelectorAll("[data-requires-device]").forEach(field=>field.disabled=!online||writing);
      const base=$("save-state").textContent;
      const dirty=changed.size>0||base.includes('尚未套用')||base.includes('尚未保存');
      $('offline-draft').hidden=online||!dirty;
      $('macro-connection-status').hidden=!online||kinds[selected]==='shortcut';
      if(!online){$('page-title').textContent='設定你的 PCB2';$('page-subtitle').textContent='連接鍵盤，開始設定你的四顆按鍵。';}
      else{const active=tabs.find(tab=>tab.getAttribute('aria-selected')==='true').dataset.page;$('page-title').textContent={keys:'設定你的鍵盤',effects:'調整燈光與音效',audio:'收音與錄音'}[active];$('page-subtitle').textContent={keys:'選一顆鍵，把常用操作放進去。',effects:'調整喜歡的燈光與聲音，再儲存到鍵盤。',audio:'看收音反應，選擇保存位置，再開始錄音。'}[active];}
      $("flow-connect").classList.toggle('current',!online);$("flow-connect").classList.toggle('complete',online);
      $("flow-edit").classList.toggle('current',online);$("flow-save").classList.toggle('pending',online&&dirty);
      $("key-selection-summary").textContent=online?`正在編輯第 ${selected+1} 顆鍵${dirty?'，有尚未儲存的設定':''}`:'正在等待你的鍵盤';
      $("save-summary").textContent=!online?"接上鍵盤即可開始設定":writing?"正在儲存，請保持連線…":changed.size?`${changed.size} 顆按鍵有未儲存的動作`:base.includes("尚未套用")?"設定已修改，準備好就儲存":base.includes("尚未保存")?"目前已生效，重啟後還不會保留":"設定已儲存";
      $("save-help").textContent=changed.size?"動作會儲存到鍵盤並由板子執行，儲存後可關閉網站。":"儲存後，重新插拔鍵盤也會保留設定；暫時套用只在這次連接生效。";
      $("apply").hidden=changed.size>0;
      document.querySelector(".save-dock").dataset.macroDirty=String(changed.size>0);
      $("save").disabled=$("save").disabled||writing;
      const onboard=latest?.macros?.execution==="device",hasText=programs[selected].steps.some(step=>step.type==="text");
      $("text-mode-group").hidden=!hasText;
      $("companion-note").hidden=demo||kinds[selected]==="shortcut";
      $("companion-note").dataset.ready=String(onboard);
      $("macro-support").textContent=onboard?"由鍵盤自行執行":"請先更新板子韌體";
      $("macro-support-detail").textContent=onboard?"儲存後，切到目標程式並按實體鍵。網站可以關閉，無須安裝背景工具。":"目前的舊韌體仍需要背景工具。更新為 v10 後，快捷鍵、多鍵組合與巨集就能直接執行。";
      const activity=latest?.macros,results={running:"執行中",completed:"已送出",interrupted:"其他按鍵中止動作",disconnected:"連線中斷",timeout:"執行逾時",usb_timeout:"USB 未接收輸出",unicode_mode_required:"中文需要先選擇文字輸入方式",macos_bmp_only:"macOS 模式暫不支援 emoji 等擴充字元"};
      if(onboard&&activity.result&&!["idle","running","completed"].includes(activity.result))$("macro-support-detail").textContent=results[activity.result]||"上次動作未完成，請查看執行狀態。";
      $("macro-activity").hidden=!online||!onboard;
      if(onboard)$("macro-activity").textContent=`已送出 ${activity.completed||0} 次 · 未完成 ${activity.failed||0} 次 · 忙碌略過 ${activity.dropped||0} 次`+(results[activity.result]?` · 第 ${activity.last_key} 顆鍵：${results[activity.result]}`:"");
      if(online&&!onboard&&kinds[selected]!=="shortcut")$("save-help").textContent="設定可保留在板子；更新 v10 韌體後才能免工具執行。";
      $("audio-running").hidden=$("monitor-toggle").getAttribute("aria-pressed")!=="true"&&!$("record-dot").classList.contains("live");
      labelPrograms();
    }
    operationHandler=delta=>{pendingOperations=Math.max(0,pendingOperations+delta);status();};
    function mark() { changed.add(selected); status(); }
    function editor(withMotion=false) {
      keys.forEach((key,i)=>key.setAttribute("aria-pressed",String(i===selected)));
      rows.forEach((row,i)=>row.hidden=i!==selected);
      document.querySelectorAll("[data-test-key]").forEach(button=>button.hidden=Number(button.dataset.testKey)!==selected);
      $("editor-number").textContent=String(selected+1).padStart(2,"0"); $("editor-title").textContent=`設定第 ${selected+1} 顆鍵`;
      $("action-kind").value=kinds[selected]; $("editor-kind").textContent={shortcut:"快捷鍵",chord:"多鍵組合",text:"輸入文字",macro:"連續動作"}[kinds[selected]];
      $("shortcut-editor").hidden=kinds[selected]!=="shortcut"; $("program-editor").hidden=kinds[selected]==="shortcut";
      $("macro-name").value=programs[selected].name; $("text-mode").value=programs[selected].text_mode||"ascii"; textModeHelp(); $("add-steps").hidden=kinds[selected]!=="macro";
      renderSteps(); status();
      if(withMotion){motion.reset('entrance');motion.reset('page');motion.reveal('editor',[document.querySelector('.editor-panel .panel-head'),$('key-fields')],{y:14,duration:.36,stagger:.04});}
    }
    keys.forEach((key,i)=>key.addEventListener("click",()=>{if(selected!==i){selected=i;editor(true);}motion.key(key);}));
    $("action-kind").addEventListener("change",()=>{
      const kind=$("action-kind").value; kinds[selected]=kind;
      if(kind==="shortcut")programs[selected]={name:"",steps:[]};
      else if(kind==="text"&&!(programs[selected].steps.length===1&&programs[selected].steps[0].type==="text"))programs[selected].steps=[{type:"text",text:""}];
      else if(kind==="chord"&&!(programs[selected].steps.length===1&&programs[selected].steps[0].type==="keys"))programs[selected].steps=[{type:"keys",keys:["CTRL","C"]}];
      else if(kind==="macro"&&!programs[selected].steps.length)programs[selected].steps=[{type:"keys",keys:["CTRL","C"]},{type:"wait",ms:200}];
      mark();editor(true);
    });
    choices.forEach((choice,i)=>{
      choice.addEventListener('click',()=>{if(choice.dataset.actionKind===$("action-kind").value)return;$("action-kind").value=choice.dataset.actionKind;$("action-kind").dispatchEvent(new Event('change',{bubbles:true}));});
      choice.addEventListener('keydown',event=>{let next;if(event.key==='ArrowRight'||event.key==='ArrowDown')next=(i+1)%choices.length;else if(event.key==='ArrowLeft'||event.key==='ArrowUp')next=(i+choices.length-1)%choices.length;else return;event.preventDefault();choices[next].focus();choices[next].click();});
    });
    $("macro-name").addEventListener("input",()=>{programs[selected].name=$("macro-name").value;mark();});
    function textModeHelp(){
      $("text-mode-help").textContent={ascii:"免設定輸入英文、數字與符號。請使用美式英文鍵盤配置；中文原文可保存，執行前需選擇下方系統模式。Windows 暫無適用所有軟體的免工具中文輸入方式。",linux:"需使用支援 Ctrl + Shift + U 的輸入環境（例如 Ubuntu／GNOME）。請先在目標程式手動確認可輸入 Unicode；部分應用程式不支援。",macos:"需先在 macOS 新增並切換到 Unicode Hex Input 輸入來源。暫支援基本平面中文字；emoji 等 U+FFFF 以上字元會整段拒絕執行。"}[$("text-mode").value];
    }
    $("text-mode").addEventListener("change",()=>{programs[selected].text_mode=$("text-mode").value;textModeHelp();mark();});
    function counts() { $("macro-count").textContent=`${programs[selected].steps.length} / 16 步 · 已使用 ${Math.ceil(new TextEncoder().encode(JSON.stringify(programs[selected])).length/12)}% 內容容量`; }
    function renderSteps() {
      motion.reset('steps');
      $("macro-steps").replaceChildren(); $("macro-error").textContent="";
      programs[selected].steps.forEach((step,index)=>{
        const card=document.createElement("div");card.className="step-card";
        const head=document.createElement("div");head.className="step-head";const title=document.createElement("strong");title.textContent=`${index+1}. ${ {keys:"按鍵組合",text:"輸入文字",wait:"等待"}[step.type]}`;head.append(title);
        if(kinds[selected]==="macro"){
          const tools=document.createElement("div");tools.className="step-tools";
          for(const [text,label,move] of [["↑","上移",-1],["↓","下移",1],["×","刪除",0]]){const button=document.createElement("button");button.textContent=text;button.setAttribute("aria-label",`${label}第 ${index+1} 步`);button.disabled=move!==0&&(index+move<0||index+move>=programs[selected].steps.length);button.addEventListener("click",()=>{const steps=programs[selected].steps;if(move)[steps[index],steps[index+move]]=[steps[index+move],steps[index]];else steps.splice(index,1);mark();renderSteps();motion.reveal('steps',[...$('macro-steps').children],{y:10,duration:.3,stagger:.025});});tools.append(button);}head.append(tools);
        }
        card.append(head);const input=document.createElement(step.type==="text"?"textarea":"input"); input.setAttribute("aria-label",`第 ${index+1} 步${{keys:"組合鍵",text:"文字",wait:"等待毫秒"}[step.type]}`);
        if(step.type==="text"){input.value=step.text;input.placeholder="輸入中文、英文或常用回覆…";}
        else if(step.type==="keys"){input.type="text";input.value=step.keys.join(" + ");input.placeholder="例如 CTRL + SHIFT + A + B";}
        else {input.type="number";input.min=0;input.max=5000;input.step=50;input.value=step.ms;}
        input.addEventListener("input",()=>{try{if(step.type==="keys")step.keys=pcb2Chord(input.value);else if(step.type==="text")step.text=input.value;else step.ms=Number(input.value); input.setCustomValidity("");$("macro-error").textContent="";}catch(error){if(step.type==="keys")step.keys=[];input.setCustomValidity(error.message);$("macro-error").textContent=error.message;}mark();counts();});
        card.append(input);
        if(step.type==="keys"){const button=document.createElement("button");button.type="button";button.className="button capture-chord";button.textContent="直接按鍵，幫我組合";button.setAttribute("aria-label",`擷取第 ${index+1} 步的組合鍵`);button.addEventListener("click",()=>openCapture(index));card.append(button);}
        const help=document.createElement("p");help.className="caption";help.textContent=step.type==="keys"?"點「直接按鍵」後，按下要加入的鍵，不必手動輸入名稱；也可在上方修改。":step.type==="text"?"每段最多 512 bytes。中文需在「文字輸入方式」選擇適合的系統，並完成系統設定。":"單位為毫秒，1000 = 1 秒。";card.append(help);$("macro-steps").append(card);
      }); counts();
    }
    document.querySelectorAll("[data-add-step]").forEach(button=>button.addEventListener("click",()=>{if(programs[selected].steps.length>=16){toast("最多 16 個步驟。",true);return;}const type=button.dataset.addStep;programs[selected].steps.push(type==="keys"?{type,keys:["ENTER"]}:type==="text"?{type,text:""}:{type,ms:200});mark();renderSteps();motion.reveal('steps',$('macro-steps').lastElementChild,{y:18,duration:.4});}));
    function setShortcut(shortcut,index) {
      const row=rows[index],select=row.querySelector("select");if(![...select.options].some(option=>Number(option.value)===shortcut.code))select.add(new Option(`HID 0x${shortcut.code.toString(16)}`,shortcut.code));
      select.value=String(shortcut.code);[...row.querySelectorAll("input")].forEach((input,bit)=>input.checked=shortcut.modifiers[bit]);select.dispatchEvent(new Event("change",{bubbles:true}));
    }
    document.querySelectorAll("[data-preset]").forEach(button=>button.addEventListener("click",()=>{const presets={copy:6,paste:25,undo:29,space:44};setShortcut({code:presets[button.dataset.preset],modifiers:button.dataset.preset==="space"?[false,false,false,false]:[!mac,false,false,mac]},selected);motion.key(keys[selected]);motion.reveal('shortcut-preview',$('shortcut-preview'),{y:6,scale:.97,duration:.3});}));
    function renderCapture(){
      $("capture-value").textContent=captureStep===null?capture?.label||"等待你的按鍵…":capturedKeys.length?capturedKeys.join(" + "):"按下要加入的鍵…";
      $("capture-confirm").disabled=captureStep===null?!capture:!capturedKeys.length;
      $("capture-chips").replaceChildren();
      if(captureStep!==null)capturedKeys.forEach((key,index)=>{const chip=document.createElement("button");chip.type="button";chip.className="captured-key";chip.textContent=key+" ×";chip.setAttribute("aria-label",`移除 ${key}`);chip.addEventListener("click",()=>{capturedKeys.splice(index,1);renderCapture();$("capture-pad").focus();});$("capture-chips").append(chip);});
    }
    function openCapture(step=null){
      capture=null;captureKey=selected;captureStep=step;capturedKeys=[];
      $("capture-title").textContent=step===null?"按下你想用的快捷鍵":"按鍵，我來幫你組合";
      $("capture-instructions").textContent=step===null?"按住 Ctrl、Shift、Alt 或 ⌘／Win，再按一個鍵。":"按下要一起送出的鍵。每顆只加入一次，放開後仍保留。";
      $("capture-confirm").textContent=step===null?"使用這個快捷鍵":"使用這組按鍵";
      $("capture-reset").hidden=step===null;$("capture-pad").hidden=false;
      $("capture-pad").textContent=step===null?"在這裡按下快捷鍵，會自動幫你組合":"在這裡按鍵，會自動加入組合";
      $("capture-specials").hidden=step===null;
      $("capture-note").textContent=step===null?"按 Esc 取消；Esc 本身可在選單設定。":"按 Tab 切換操作，Esc 取消；要加入這兩顆鍵，可用上方按鈕。完成後再儲存到鍵盤。";
      renderCapture();$("shortcut-dialog").showModal();$("capture-pad").focus();animate($("shortcut-dialog"));
    }
    $("capture-shortcut").addEventListener("click",()=>openCapture());
    $("shortcut-dialog").addEventListener("keydown",event=>{
      if(captureStep!==null){if(event.target!==$("capture-pad")||event.key==="Escape"||event.key==="Tab"&&!event.ctrlKey&&!event.altKey&&!event.metaKey)return;event.preventDefault();event.stopPropagation();try{const next=pcb2AppendCaptured(capturedKeys,event);if(next.join()!==capturedKeys.join()){capturedKeys=next;renderCapture();animate($("capture-value"));}else if(!pcb2CapturedKey(event)&&!event.repeat&&!event.isComposing)$("capture-note").textContent="這顆鍵目前不支援多鍵動作，請換其他按鍵。";}catch(error){$("capture-note").textContent=error.message;}return;}
      if(event.key==="Escape"||event.key==="Tab"&&!event.ctrlKey&&!event.altKey&&!event.metaKey||event.target===$("capture-confirm")||event.target.closest("form"))return;
      event.preventDefault();event.stopPropagation();const result=pcb2Shortcut(event);if(!result)return;
      capture={...result,label:[...result.modifiers.map((value,i)=>value?["Ctrl","Shift","Alt",mac?"⌘":"Win"][i]:"").filter(Boolean),event.code.replace(/^Key|^Digit/,"")].join(" + ")};renderCapture();animate($("capture-value"));
    });
    for(const [id,code] of [["capture-tab","Tab"],["capture-esc","Escape"]])$(id).addEventListener("click",()=>{try{capturedKeys=pcb2AppendCaptured(capturedKeys,{code});renderCapture();$("capture-pad").focus();}catch(error){$("capture-note").textContent=error.message;}});
    $("capture-reset").addEventListener("click",()=>{capturedKeys=[];renderCapture();$("capture-pad").focus();});
    $("capture-confirm").addEventListener("click",()=>{if(captureStep===null){if(capture)setShortcut(capture,captureKey);}else if(capturedKeys.length){programs[captureKey].steps[captureStep].keys=capturedKeys.slice();changed.add(captureKey);renderSteps();status();}$("shortcut-dialog").close();motion.key(keys[captureKey]);});
    async function request(path,body) {
      const response=await transport.fetch(path,{method:body?"POST":"GET",headers:body?{"Content-Type":"application/json","X-PCB2-Control":"1"}:{},body:body?JSON.stringify(body):undefined,signal:AbortSignal.timeout(5000)});
      const result=await response.json();if(!response.ok)throw new Error(result.error||"鍵盤沒有接受這次設定。");return result;
    }
    stateHandler=next=>{
      latest=next;
      if(supported()&&!loaded&&!loading&&!loadFailed){loading=true;void Promise.all([0,1,2,3].map(i=>request(`/api/macro/${i}`))).then(values=>{values.forEach(pcb2ValidateProgram);programs=values;kinds=values.map(p=>!p.steps.length?"shortcut":p.steps.length===1&&p.steps[0].type==="text"?"text":p.steps.length===1&&p.steps[0].type==="keys"?"chord":"macro");loaded=true;editor();}).catch(error=>{loadFailed=true;toast("讀取巨集失敗，請重新連接："+error.message,true);}).finally(()=>loading=false);}
      if(!supported())loaded=true;
      status();
    };
    $("save").addEventListener("click",async event=>{
      if(savedClick||!changed.size)return;
      event.stopImmediatePropagation(); if(saving)return;
      try {
        if(!supported())throw new Error("目前韌體不支援文字與巨集，請先更新板子。");
        if(!$("key-fields").reportValidity())return;
        for(const i of changed){pcb2ValidateProgram(programs[i]);if(latest?.macros?.execution==='device'){const issue=pcb2TextIssue(programs[i]);if(issue)throw new Error(issue);}}
        saving=true;status();
        if(!demo)for(const i of [...changed]){await request("/api/macro",{key:i,program:programs[i]});changed.delete(i);}
        else changed.clear();
        saving=false;$("save").disabled=false;savedClick=true;$("save").click();savedClick=false;status();
      } catch(error){saving=false;$("save").disabled=!ready();$("macro-error").textContent=error.message;toast(error.message,true);status();}
    },true);
    $("sound-recording").addEventListener("click",()=>{document.querySelector(".custom-sound").open=true;});
    $("sd-file-sound").addEventListener("click",()=>{page("effects");document.querySelector(".custom-sound").open=true;});
    $("reconnect").addEventListener("click",()=>{if(!changed.size){loaded=false;loadFailed=false;}});
    for(const id of ["serial-choose","serial-connect","serial-disconnect"]){$(id).addEventListener("click",event=>{
      const resuming=!ready()&&['serial-choose','serial-connect'].includes(id);
      if(changed.size&&!resuming||pendingOperations||saving){
        event.preventDefault();event.stopImmediatePropagation();toast("先完成擷取並儲存目前的設定，再切換或中斷鍵盤。",true);
      }else if(id!=="serial-disconnect"&&!changed.size){loaded=false;loadFailed=false;}
    },true);}
    new MutationObserver(()=>status()).observe($("save-state"),{childList:true,characterData:true,subtree:true});
    new MutationObserver(()=>{if(!ready()&&!changed.size){loaded=false;loadFailed=false;}status();}).observe($("connection"),{attributes:true,attributeFilter:["class"]});
    new MutationObserver(()=>status()).observe($("monitor-toggle"),{attributes:true,attributeFilter:["aria-pressed"]});
    new MutationObserver(()=>status()).observe($("record-dot"),{attributes:true,attributeFilter:["class"]});
    function offlineDownload(){const source=$('browser-download'),link=$('offline-download'),available=!!source.getAttribute('href')&&!source.classList.contains('disabled');link.hidden=!available||ready();if(available){link.href=source.href;link.download=source.download;}}
    new MutationObserver(offlineDownload).observe($('browser-download'),{attributes:true,attributeFilter:['href','class','download']});
    new MutationObserver(offlineDownload).observe(document.body,{attributes:true,attributeFilter:['data-connected']});
    new MutationObserver(()=>{
      if($('toast').hidden)return;
      motion.reveal('toast',$('toast'),{y:18,duration:.36});
      if(!$('toast').classList.contains('error')&&/設定已保存/.test($('toast').textContent)){
        const dock=document.querySelector('.save-dock');dock.dataset.saved='true';clearTimeout(savedTimer);savedTimer=setTimeout(()=>dock.dataset.saved='false',1300);motion.saved();
      }
    }).observe($('toast'),{childList:true,attributes:true,attributeFilter:['hidden','class']});
    // Maintained app updates board labels when loading/saving settings; restore macro summaries.
    new MutationObserver(()=>{if(programs.some(p=>p.steps.length))labelPrograms();}).observe($("board"),{attributes:true,attributeFilter:["class"]});
    document.querySelectorAll("[data-test-key]").forEach(button=>new MutationObserver(()=>{const key=keys[Number(button.dataset.testKey)];key.classList.toggle("down",button.classList.contains("down"));key.classList.toggle("test",button.classList.contains("test"));}).observe(button,{attributes:true,attributeFilter:["class"]}));
    editor();
    motion.entrance();
  });
}
