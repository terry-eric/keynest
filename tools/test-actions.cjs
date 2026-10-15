const assert=require('node:assert/strict');
const actions=require('../web-console/actions.js');
const {pcb2ValidateProgram}=require('../web-console/studio.js');
for(let bit=0;bit<8;bit++){
  const mapping=actions.single(`mod:${1<<bit}`);
  assert.deepEqual(mapping,{code:0,modifiers:1<<bit});
  assert.equal(actions.singleValue(mapping),`mod:${1<<bit}`);
}
assert.deepEqual(actions.single('key:4'),{code:4,modifiers:0});
assert.equal(actions.singleValue({code:4,modifiers:1}),null);
for(const value of ['mod:0','mod:3','mod:256','key:3','key:116','key:NaN','mod:1junk'])assert.throws(()=>actions.single(value));
const config={keycodes:[104,105,106,107],modifiers:[128,0,2,0],volume:20};
const mapped=actions.config(config,[actions.single('mod:1'),actions.single('key:4'),null,null]);
assert.deepEqual(mapped.keycodes,[0,4,106,107]);assert.deepEqual(mapped.modifiers,[1,0,2,0]);
assert.equal(mapped.volume,20);assert.equal(config.modifiers[0],128);
for(const target of ['notepad','calc','explorer','C:\\Program Files\\My App\\app.exe','"C:\\Program Files\\My App\\app.exe"']){
  const p=actions.launch(target,'啟動程式');pcb2ValidateProgram(p);
  assert.deepEqual(p.steps[0].keys,['META','R']);assert.deepEqual(p.steps[2].keys,['CTRL','A']);
  assert.deepEqual(p.steps.at(-1).keys,['ENTER']);assert(actions.launchDetails(p));
  assert.deepEqual(actions.launch(actions.launchDetails(p).target,p.name,actions.launchDetails(p).wait),p);
}
assert.equal(actions.launch('C:\\Program Files\\My App\\app.exe').steps[3].text,'"C:\\Program Files\\My App\\app.exe"');
for(const value of ['', 'calc\nnotepad', 'cmd /c something','calc & notepad','記事本','C:\\Users\\中文\\app.exe','C:\\bad"name.exe'])assert.throws(()=>actions.launch(value));
assert.throws(()=>actions.launch('calc','',100));assert.throws(()=>actions.launch('calc','',5001));
const ordinary=actions.launch('calc');ordinary.steps[4].ms=200;assert.equal(actions.launchDetails(ordinary),null);
for(const id of ['copyAll','pasteSend','englishReply'])pcb2ValidateProgram(actions.example(id));
assert.deepEqual(actions.example('copyAll',true).steps[0].keys,['META','A']);
const example=actions.example('copyAll');example.steps[0].keys.push('X');assert.equal(actions.example('copyAll').steps[0].keys.length,2);
console.log('PASS: independent single/modifier keys, exact config payloads, quoted Windows paths, launch round-trip, invalid input and editable macro examples');
const catalog=actions.catalog();
assert.equal(new Set(catalog.map(e=>e.id)).size,catalog.length);
for(const entry of catalog){pcb2ValidateProgram(actions.program(entry));if(entry.mapping)assert(entry.kind==='single'||entry.kind==='shortcut');}
const gestures={hold_ms:600,double_ms:300,hold:actions.program(catalog.find(e=>e.id==='selectAll')),double:actions.launch('notepad','記事本')};
const wrapped=actions.wrap({name:'',steps:[]},{code:6,modifiers:1},'shortcut',gestures);pcb2ValidateProgram(wrapped);
assert.deepEqual(wrapped.steps,[{type:'hid',code:6,modifiers:1}]);
assert(!actions.wrap({name:'',steps:[]},{code:0,modifiers:1},'single',{...gestures,hold:null,double:null}).gestures);
for(const patch of [{hold_ms:299},{double_ms:601},{hold_ms:NaN},{hold:{name:'',steps:[]}},{hold:{name:'',steps:[],gestures}}])assert.throws(()=>pcb2ValidateProgram({...wrapped,gestures:{...gestures,...patch}}));
assert.throws(()=>pcb2ValidateProgram({name:'',steps:[{type:'hid',code:256,modifiers:0}]}));
wrapped.gestures.hold.name='changed';assert.notEqual(gestures.hold.name,'changed');
console.log(`PASS: ${catalog.length} catalog actions, strict gesture payloads, independent drafts and raw single-key fallback`);
