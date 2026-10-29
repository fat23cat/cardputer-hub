const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const {spawnSync} = require('node:child_process');
const html = fs.readFileSync('web/index.html','utf8');
const source = html.slice(html.indexOf('// ---- LED model:'),html.indexOf('const LED_MODES'));
const context = vm.createContext({Float32Array,Math});
vm.runInContext(source+'\nglobalThis.Model=LedCanvasModel;',context);
const modes = ['rainbow','embers','velocity','mix','ripple','kaleidoscope','solid'];
const commands = [], expected = [], labels = [];
let now=0;
function colors(model) {
  const result=[];
  for(let i=0;i<64;++i) {
    const rgb=Array.from(model.eng.rgb.slice(i*3,i*3+3));
    if(model.eng.heat[i]>0) {
      const heat=model._ramp(model.K.ember,model.eng.heat[i]);
      for(let j=0;j<3;++j)rgb[j]=Math.max(rgb[j],heat[j]);
    }
    result.push(...rgb.map(Math.round));
  }
  return result;
}
for(let mode=0;mode<modes.length;++mode) {
 for(const [radius,glow,extended] of [[6,true,false],[0,false,true],[0,true,true],[6,false,true],[6,true,true],[17,false,true],[17,true,true]]) {
  const model=new context.Model();model.eng.mode=modes[mode];model.eng.hold=true;model.eng.ink=3;
  function record(command,label) { commands.push(command);expected.push(colors(model));labels.push(`${modes[mode]} radius=${radius} glow=${glow}: ${label}`); }
  record(`0 ${mode}`,'clear');
  const path=[[0,128,128,1],[0,832,128,0],[0,1408,704,0],[0,1408,704,2],[1,128,1920,1],[1,448,1920,0]];
  for(const [pointer,x,y,flags] of path) {
    now+=40;
    model._stroke(model.eng,pointer,x/256,y/256,now,!!(flags&1),radius/16,glow);
    const p=model.eng.ptrs[pointer];
    const hue=['rainbow','kaleidoscope'].includes(modes[mode])?p.hue:model.eng.hue;
    const phase=Math.round(((hue%1+1)%1)*10000)%10000,ink=p.stroke%3;
    if(flags&2)delete model.eng.ptrs[pointer];
    record(`${extended?3:1} ${mode} ${pointer} ${x} ${y} ${radius} ${flags} ${now} ${phase} ${ink}${extended?` ${glow?1:0}`:''}`,'point');
    now+=16;model._tick(model.eng,.016,now);record(`2 ${mode} 16`,'advance');
  }
 }
}
const result=spawnSync(process.argv[2],[],{input:commands.join('\n')+'\n',encoding:'utf8'});
assert.equal(result.status,0,result.stderr);
const actual=result.stdout.trim().split('\n').map(line=>line.trim().split(/\s+/).map(Number));
assert.equal(actual.length,expected.length);
for(let frame=0;frame<expected.length;++frame)for(let channel=0;channel<192;++channel)
  assert.ok(Math.abs(actual[frame][channel]-expected[frame][channel])<=1,
    `${labels[frame]} channel ${channel}: firmware ${actual[frame][channel]}, browser ${expected[frame][channel]}`);
console.log('Firmware/browser color parity passed for all seven modes');
