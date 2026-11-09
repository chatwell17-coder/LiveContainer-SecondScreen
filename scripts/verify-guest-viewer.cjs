// Runs the actual embedded viewer script against a minimal DOM and HTTP stub.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync('TweakLoader/LCGuestDisplayProbe.h', 'utf8');
const html = JSON.parse(source.match(/NSString \*html = \[NSString stringWithFormat:@(.*), framePath\];/)[1]);
assert.equal((html.match(/%@/g) || []).length, 1);
assert.equal((html.replace('%@','').match(/%/g) || []).length, 0, 'Escape NSString formatting directives');
const nodes = new Map(), requests = [], docEvents = {}, winEvents = {};
function node(id) {
    if (!nodes.has(id)) nodes.set(id, {value:'',checked:true,events:{},
        addEventListener(name,fn){this.events[name]=fn;},
        setPointerCapture(){},getBoundingClientRect(){return {left:10,top:20,width:200,height:400};},
        click(){return this.onclick();}});
    return nodes.get(id);
}
const context = {URLSearchParams,Promise,AbortSignal,Date,Math,Error,
    URL:{createObjectURL(){return 'blob:test';},revokeObjectURL(){}},
    performance:{now:()=>100},location:{pathname:'/token'},
    document:{getElementById:node,addEventListener:(n,f)=>docEvents[n]=f},
    window:{addEventListener:(n,f)=>winEvents[n]=f},setTimeout(){},
    fetch:async (url,options)=>{if(options.method==='POST')requests.push(new URL(url,'http://guest'));
        return {ok:true,blob:async()=>({})};}};
vm.runInNewContext(html.match(/<script>(.*?)<\/script>/s)[1].replace('%@','/token/frame.jpg'),context);
const drain = async()=>{for(let i=0;i<30;i++)await Promise.resolve();};
(async()=>{
    node('text').value='SPACE + PLUS'; await node('run').onclick();
    assert.equal(requests.at(-1).searchParams.get('text'),'SPACE + PLUS\n');
    await node('back').onclick(); assert.equal(requests.at(-1).searchParams.get('key'),'Backspace');
    const f=node('f'), event={button:0,pointerId:7,clientX:110,clientY:220,preventDefault(){}};
    f.events.pointerdown(event); f.events.pointermove({...event,clientX:210}); f.events.pointerup(event); await drain();
    assert.deepEqual(requests.slice(-3).map(u=>u.searchParams.get('phase')),['began','moved','ended']);
    assert.equal(requests.at(-3).searchParams.get('x'),'0.5');
    assert.equal(requests.at(-2).searchParams.get('x'),'1');
    f.events.pointerdown(event); winEvents.blur(); await drain();
    assert.equal(requests.at(-1).searchParams.get('phase'),'cancelled');
    node('touch').checked=false; f.events.pointerdown(event); await drain();
    assert.equal(requests.at(-1).searchParams.get('kind'),'click');
    await node('test').onclick(); assert.equal(requests.at(-1).searchParams.get('kind'),'test');
    console.log('PASS: text encoding, Backspace, touch sequence, coordinates, blur cancellation, basic controls, test panel');
})().catch(e=>{console.error(e);process.exitCode=1;});
