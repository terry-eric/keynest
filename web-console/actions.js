"use strict";
function keynestSingle(value) {
  const match=/^(key|mod):(\d+)$/.exec(value||'');
  if(!match)throw new Error('請選擇一個按鍵。');
  const n=Number(match[2]);
  if(match[1]==='mod' && n>=1 && n<=128 && !(n&(n-1)))return {code:0,modifiers:n};
  if(match[1]==='key' && (n===0||n>=4&&n<=115))return {code:n,modifiers:0};
  throw new Error('這不是可用的單鍵。');
}
function keynestSingleValue({code,modifiers}) {
  if(!modifiers)return `key:${code}`;
  return !code && modifiers<=128 && !(modifiers&(modifiers-1))?`mod:${modifiers}`:null;
}
function keynestConfig(config,overrides) {
  const result={...config,keycodes:config.keycodes.slice(),modifiers:config.modifiers.slice()};
  overrides.forEach((mapping,i)=>{if(mapping){result.keycodes[i]=mapping.code;result.modifiers[i]=mapping.modifiers;}});
  return result;
}
function keynestLaunch(target,name='',wait=800) {
  let value=target.trim();
  if(value.startsWith('"')&&value.endsWith('"'))value=value.slice(1,-1);
  if(!value || /[^\x20-\x7e]/.test(value))throw new Error('請使用英文程式名稱或英文 .exe 路徑，例如 notepad、calc。');
  const alias=/^[a-zA-Z0-9_.-]+$/.test(value),path=/^[a-zA-Z]:\\[^<>:"|?*\r\n]+\.exe$/i.test(value);
  if(!alias&&!path)throw new Error('請填程式名稱，或完整 .exe 路徑；此欄不接受額外參數或多行指令。');
  if(!Number.isInteger(wait)||wait<300||wait>5000)throw new Error('等待時間請填 300～5000 毫秒。');
  const text=path?`"${value}"`:value;
  if(new TextEncoder().encode(text).length>512)throw new Error('程式路徑太長，最多 512 bytes。');
  return {name,steps:[{type:'keys',keys:['META','R']},{type:'wait',ms:wait},{type:'keys',keys:['CTRL','A']},{type:'text',text},{type:'wait',ms:150},{type:'keys',keys:['ENTER']}]};
}
function keynestLaunchDetails(program) {
  const steps=program.steps;
  if(program.text_mode && program.text_mode!=='ascii'||steps.length!==6)return null;
  const keys=(n,value)=>steps[n]?.type==='keys'&&JSON.stringify(steps[n].keys)===JSON.stringify(value);
  if(!keys(0,['META','R'])||steps[1]?.type!=='wait'||!keys(2,['CTRL','A'])||steps[3]?.type!=='text'||steps[4]?.type!=='wait'||steps[4].ms!==150||!keys(5,['ENTER']))return null;
  try{const rebuilt=keynestLaunch(steps[3].text,program.name,steps[1].ms);if(JSON.stringify(rebuilt.steps)!==JSON.stringify(steps))return null;}
  catch{return null;}
  return {target:steps[3].text.replace(/^"|"$/g,''),wait:steps[1].ms};
}
function keynestExample(id,mac=false) {
  const modifier=mac?'META':'CTRL';
  const examples={
    copyAll:{name:'全選並複製',steps:[{type:'keys',keys:[modifier,'A']},{type:'wait',ms:100},{type:'keys',keys:[modifier,'C']}]},
    pasteSend:{name:'貼上並送出',steps:[{type:'keys',keys:[modifier,'V']},{type:'wait',ms:200},{type:'keys',keys:['ENTER']}]},
    englishReply:{name:'常用英文回覆',steps:[{type:'text',text:'Thanks! I will check and get back to you.'}]}
  };
  if(!examples[id])throw new Error('找不到這個範例。');
  return structuredClone(examples[id]);
}
function keynestCatalog(mac=false) {
  const mod=mac?8:1, name=mac?'⌘':'Ctrl';
  const raw=(id,title,description,code,modifiers=0,category='shortcut')=>({id,title,description,category,mapping:{code,modifiers},kind:category==='single'?'single':'shortcut',preview:category==='single'?title:description.split(' · ')[0]});
  const entries=[
    raw('ctrl','Ctrl','按住維持 Ctrl；可搭配另一顆實體鍵。',0,1,'single'),
    raw('shift','Shift','按住維持 Shift；可選取文字或輸入大寫。',0,2,'single'),
    raw('alt','Alt','按住維持 Alt。',0,4,'single'),
    raw('meta',mac?'Command':'Win','按住維持系統修飾鍵。',0,8,'single'),
    raw('a','A','一般字母 A，按住可持續輸入。',4,0,'single'),
    raw('enter','Enter','確認、換行；部分程式會送出訊息。',40,0,'single'),
    raw('space','空白鍵','播放／暫停影片，或輸入空白。',44,0,'single'),
    raw('escape','Esc','關閉選單或取消目前操作。',41,0,'single'),
    ...[['copy','複製','C',6],['paste','貼上','V',25],['cut','剪下','X',27],['selectAll','全選','A',4],['undo','復原','Z',29],['find','搜尋','F',9],['save','儲存檔案','S',22]].map(([id,title,key,code])=>raw(id,title,`${name}＋${key} · 在目前程式執行${title}。`,code,mod)),
    raw('switchWindow','切換視窗',mac?'⌘＋Tab · 切到另一個應用程式。':'Alt＋Tab · 切到另一個視窗。',43,mac?8:4),
    raw('screenshot','擷取畫面','Windows · Win＋Shift＋S，選取截圖範圍。',22,10),
    raw('desktop','顯示桌面','Windows · Win＋D，切換顯示桌面。',7,8),
    ...[['copyAll','全選並複製','先全選，再複製；適合整理文字。',`${name}＋A → 等待 → ${name}＋C`],['pasteSend','貼上並送出','貼上後按 Enter；聊天軟體可能直接送出。',`${name}＋V → 等待 → Enter`],['englishReply','常用英文回覆','輸入固定英文回覆，可到按鍵設定修改文字。','Thanks! I will check and get back to you.']].map(([id,title,description,preview])=>({id,title,description,preview,category:'macro',kind:id==='englishReply'?'text':'macro',program:keynestExample(id,mac)})),
    ...[['notepad','記事本'],['calc','小算盤'],['explorer','檔案總管']].map(([target,title])=>({id:'open-'+target,title:'開啟'+title,description:'Windows · 程式須已安裝，使用美式英文輸入。',preview:`Win＋R → ${target} → Enter`,category:'application',kind:'application',program:keynestLaunch(target,'開啟'+title)})),
    ...[['play','播放／暫停','PLAYPAUSE'],['mute','靜音切換','MUTE'],['volumeUp','增加音量','VOLUMEUP'],['volumeDown','降低音量','VOLUMEDOWN']].map(([id,title,key])=>({id,title,description:'控制電腦的媒體與音量。',preview:key,category:'media',kind:'chord',program:{name:title,steps:[{type:'keys',keys:[key]}]}}))
  ];
  return entries;
}
function keynestActionProgram(entry) {
  return entry.mapping?{name:entry.title,steps:[{type:'hid',code:entry.mapping.code,modifiers:entry.mapping.modifiers}]}:structuredClone(entry.program);
}
function keynestWrap(program,mapping,kind,gestures) {
  const result=structuredClone(program);
  if(gestures?.hold || gestures?.double){
    if(['single','shortcut'].includes(kind))result.steps=[{type:'hid',code:mapping.code,modifiers:mapping.modifiers}];
    result.gestures=structuredClone(gestures);
  }
  return result;
}
const KeyNestActions={single:keynestSingle,singleValue:keynestSingleValue,config:keynestConfig,launch:keynestLaunch,launchDetails:keynestLaunchDetails,example:keynestExample,catalog:keynestCatalog,program:keynestActionProgram,wrap:keynestWrap};
if(typeof module==='object'&&module.exports)module.exports=KeyNestActions;
else window.KeyNestActions=KeyNestActions;
