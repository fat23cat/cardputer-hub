const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const page = fs.readFileSync('web/index.html', 'utf8');
const source = page.slice(page.indexOf('// ---- LED model:'), page.indexOf('async function ledCanvasPage'));
let now = 0;
let timeoutId = 0; const timeouts = new Map();
class Element {
  constructor(props = {}) { this.style = {}; this.events = {}; this.listenerOptions = {}; this.attrs = {}; this.hidden = !!props.hidden; this.checked = !!props.checked; Object.assign(this, props); this.classList = { toggle() {} }; }
  setAttribute(k,v) { this.attrs[k] = v; }
  addEventListener(k,v,options) { this.events[k] = v; this.listenerOptions[k] = options; }
  getBoundingClientRect() { return { left: 0, top: 0, width: 400, height: 400 }; }
  setPointerCapture(id) { this.capture = id; }
  hasPointerCapture(id) { return this.capture === id; }
}
const requests = [];
const shared = { open: true, revision: 0, fingers: 0, settings: { mode: 'rainbow', fade: 'medium', ink: 0, hold: false } };
let handler = async () => ({ ...shared });
const context = vm.createContext({ console, Float32Array, Uint32Array, Map, Set, Math, Object, Array, String,
  AbortController, crypto: { getRandomValues: (a) => { a[0] = 123; return a; } }, performance: { now: () => now },
  setTimeout: (fn,ms) => { timeouts.set(++timeoutId,{fn,ms}); return timeoutId; }, clearTimeout(id) { timeouts.delete(id); }, setInterval: () => 1, clearInterval() {}, requestAnimationFrame: () => 1, cancelAnimationFrame() {},
  localStorage: { getItem: () => null, setItem() {} }, state: { signedIn: true }, routeSerial: 1,
  window: { addEventListener() {}, removeEventListener() {} }, document: { addEventListener() {}, removeEventListener() {} },
  h: (tag, props) => new Element(props), show() {},
  api: async (path, options) => { requests.push({ path, options }); return handler(path,options); }
});
vm.runInContext(source + '\nglobalThis.Model=LedCanvasModel;globalThis.Queue=LedStrokeQueue;globalThis.Page=LedCanvasPage;', context);
const { Model, Queue, Page } = context;
const tick = () => new Promise((resolve) => setImmediate(resolve));
function event(id, type='touch', size=10, x=175, y=175) { return { pointerId:id, pointerType:type, width:size,height:size,clientX:x,clientY:y,button:0,buttons:1 }; }
function deferred() { let resolve,reject; const promise=new Promise((a,b)=>{resolve=a;reject=b;});return { promise,resolve,reject }; }
(async () => {
  const m=new Model(),e=m.eng;
  e.mode='solid';e.ink=6;m._stroke(e,0,3.5,3.5,0,true,.4);
  assert.equal(e.rgb[27*3],255);assert.ok(e.rgb[28*3]>0&&e.rgb[28*3]<255);
  delete e.ptrs[0];m._tick(e,1,1000);assert.ok(Math.abs(e.rgb[27*3]-94)<1);
  e.hold=true;const before=e.rgb[27*3];m._tick(e,1,2000);assert.equal(e.rgb[27*3],before);
  const mm=new Model();mm.eng.mode='mix';mm._stroke(mm.eng,0,3.5,3.5,0,true,.4);mm._stroke(mm.eng,1,3.5,3.5,0,true,.4);
  assert.equal(mm.eng.rgb[81],255);assert.equal(mm.eng.rgb[82],255);
  for(const mode of ['rainbow','kaleidoscope']) {
    const local=new Model();local.eng.mode=mode;local.eng.hold=true;
    local._stroke(local.eng,0,.5,.5,0,true,.4);
    const green=local.eng.rgb[1];
    local._stroke(local.eng,1,3.5,3.5,10,true,.4);
    local._tick(local.eng,.033,43);
    assert.equal(local.eng.rgb[1],green,'A stationary finger retains its own hue');
  }
  const q=new Queue();q.add([0,0,0,6,0,1]);for(let i=1;i<150;++i)q.add([0,i,0,6,i*2,0]);q.add([0,150,0,6,300,2]);
  assert.equal(q.pending.length,64);const batch=q.take();assert.equal(batch[0][5],1);assert.equal(batch.at(-1)[5],2);
  assert.ok(batch.every(p=>p[4]>=0&&p[4]<=255));assert.equal(batch.reduce((s,p)=>s+p[4],0),300);
  // A slow request may collect many taps; admission preserves all starts and ends within 64.
  for(let i=0;i<100;++i){if(!q.canStart(0))break;q.add([0,1,1,6,i,1]);q.add([0,1,1,6,i,2]);}
  assert.ok(q.pending.length<=64);assert.ok(q.pending.every(p=>p[5]!==0));
  const p=new Page(1);await tick();assert.equal(p.open,true);assert.equal(p.closed.hidden,true);
  let prevented=0;
  for(const type of ['touchstart','touchmove','touchend','gesturestart','gesturechange','gestureend']) {
    assert.equal(typeof p.panelWrap.events[type],'function','Drawing owns '+type);
    assert.equal(p.panelWrap.listenerOptions[type].passive,false,'Gesture cancellation must not be passive');
    p.panelWrap.events[type]({cancelable:true,preventDefault(){++prevented;}});
    p.panelWrap.events[type]({cancelable:false,preventDefault(){throw Error('Noncancelable event');}});
  }
  assert.equal(prevented,6,'Pinch and double-tap defaults are suppressed inside the drawing panel');

  for(let i=0;i<6;++i)p.down(event(i));assert.equal(p.pointers.size,5);
  for(const [i,sample] of p.queue.pending.entries()) {
    assert.equal(sample.length,8,'Each point carries its preview color phase');
    assert.equal(sample[6],(i+1)*400);assert.equal(sample[7],i%3);
  }
  p.endAll();await tick();
  assert.equal(requests.at(-1).options.body.p[0].length,8,'The sender preserves phase metadata');
  assert.equal(p.radius(event(9)),1.05);assert.ok(Math.abs(p.radius(event(10,'touch',20))-.3)<1e-8);
  assert.equal(p.radius(event(11,'mouse',99)),.4);assert.equal(p.radius(event(12,'pen',99)),.4);
  p.hover=false;p.move({...event(20,'mouse'),buttons:0});assert.equal(p.pointers.size,0);
  p.hover=true;p.move({...event(20,'mouse'),buttons:0});assert.equal(p.pointers.size,1);p.endAll();await tick();
  const inFlight=deferred();handler=()=>inFlight.promise;
  now=1000;p.down(event(30));const sent=p.flush();const count=requests.length;
  now=1010;p.move(event(30,'touch',20,250));await p.flush();assert.equal(requests.length,count);
  inFlight.reject({status:0,body:{error:'network'}});await sent;assert.ok(p.retry);assert.equal(p.online,false);
  const retrySeq=p.retry.seq;handler=async()=>({...shared});now=1600;await p.flush();
  assert.equal(requests.at(-1).options.body.seq,retrySeq);assert.equal(p.retry,null);assert.equal(p.online,true);
  await p.flush();assert.ok(requests.at(-1).options.body.seq>retrySeq);
  // Clear waits for the stroke response, then empties the preview and blocks new input meanwhile.
  const beforeClear=deferred();handler=(path)=>path.endsWith('/strokes')?beforeClear.promise:Promise.resolve({...shared});
  now=2000;p.move(event(30));const stroke=p.flush();const clearing=p.clear();
  p.down(event(40));assert.equal(p.pointers.size,0);
  beforeClear.resolve({...shared});await stroke;
  for (const [id,timer] of timeouts) if (timer.ms===20) { timeouts.delete(id);timer.fn(); }
  await clearing;assert.equal(requests.at(-1).path,'/api/led/clear');
  assert.ok(Array.from(p.model.eng.rgb).every(v=>v===0));p.dispose();
  // A failed stroke must not recreate a retry that survives Clear.
  handler=async()=>({...shared});
  const clearRace=new Page(1);await tick();
  const failedStroke=deferred(),clearReply=deferred();
  handler=(path)=>path.endsWith('/strokes')?failedStroke.promise:
    path.endsWith('/clear')?clearReply.promise:Promise.resolve({...shared});
  now=3000;clearRace.down(event(60));const raceSend=clearRace.flush();
  const raceClear=clearRace.clear();
  failedStroke.reject({status:0,body:{error:'network'}});await raceSend;
  const strokesBefore=requests.filter(r=>r.path.endsWith('/strokes')).length;
  now=4000;await clearRace.flush();
  assert.equal(requests.filter(r=>r.path.endsWith('/strokes')).length,strokesBefore,
    'No retry may start between the failed stroke and Clear');
  for(const [id,timer] of timeouts)if(timer.ms===20){timeouts.delete(id);timer.fn();}
  await tick();
  assert.equal(requests.at(-1).path,'/api/led/clear');
  assert.equal(clearRace.retry,null,'Clear must discard retries created while waiting');
  await clearRace.flush();
  assert.equal(requests.filter(r=>r.path.endsWith('/strokes')).length,strokesBefore,
    'No stroke may start while the Clear request is pending');
  clearReply.resolve({...shared});await raceClear;
  await clearRace.flush();
  assert.equal(requests.filter(r=>r.path.endsWith('/strokes')).length,strokesBefore,
    'A successful Clear must not replay a pre-clear stroke');
  await clearRace.poll();
  const newStroke=deferred();handler=(path)=>path.endsWith('/strokes')?newStroke.promise:Promise.resolve({...shared});
  now=5000;clearRace.down(event(61));const afterClear=clearRace.flush();
  assert.equal(requests.at(-1).options.body.p[0][4],0,'A fresh post-clear stroke starts a new sample timeline');
  assert.equal(requests.at(-1).options.body.p[0][5],1);
  newStroke.resolve({...shared});await afterClear;clearRace.dispose();
  handler=async()=>({...shared});
  // Closed status discards queued/retried input and wakes on the next poll.
  const closed=new Page(1);await tick();closed.apply({open:false,revision:0,settings:shared.settings});
  assert.equal(closed.open,false);closed.down(event(50));assert.equal(closed.pointers.size,0);
  closed.apply(shared);assert.equal(closed.open,true);closed.dispose();
  assert.equal(closed.current(),false);
  console.log('LED browser model, queue, pointers, retry and lifecycle checks passed');
})();
