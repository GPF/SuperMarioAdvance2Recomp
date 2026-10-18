#!/usr/bin/env node
// Gate 8D client: breakpoint at the frozen B16 window start, validate the guest state, arm 1,000,000 instructions, capture.
// ONE attempt: any failed check stops before arming; nothing is retried. Every RPC goes to rpc.jsonl.
import fs from 'node:fs'; import crypto from 'node:crypto';
const WebSocket=(await import('/home/gpf/code/nulldc3/devtools/node_modules/ws/index.js')).default;
const OUT=process.argv[2], URL=process.argv[3], TRACE=process.argv[4], CONSOLE=process.argv[5];
const START=0x8C01FEA2, G_CPU=0x8C145EAC, CYC=0x8C145E98, VBL=0x8C145FD8, IRQ=0x8C1BAB10, DEPTH=0x8C24EE40, ARMED=0x8C1A988D, COMPLETE=0x8C1A988C, RETIRED=0x8C1AC898, INTERP_RET=0x8C1A9774, NATIVE=0x8C1B0B44;
const LIMIT=1000000, STOP_TIMEOUT_MS=240000, CAPTURE_WAIT_MS=240000;
const rpcLog=fs.createWriteStream(OUT+'/rpc.jsonl');
const hex=(n)=>'0x'+(n>>>0).toString(16).toUpperCase().padStart(8,'0');
const ws=new WebSocket(URL); let id=0; const pend=new Map(); let exec={state:'?',stopId:-1}; let ticks=0; const tickPc=new Map();
const num=(v)=>typeof v==='number'?v:parseInt(String(v).replace(/^0x/i,''),16);
ws.on('message',(raw)=>{const m=JSON.parse(raw.toString());
  if(m.id!==undefined&&pend.has(m.id)){rpcLog.write(JSON.stringify({dir:'res',t:Date.now(),m})+'\n');pend.get(m.id)(m);pend.delete(m.id);return;}
  if(m.method==='event.tick'){ticks++;const p=m.params;if(p.executionState){exec=p.executionState;for(const w of p.watches??[])if(w.expression==='dc.sh4.cpu.pc')tickPc.set(exec.stopId,num(w.value));}}});
const call=(method,params={})=>new Promise((res,rej)=>{const my=++id;pend.set(my,res);const req={jsonrpc:'2.0',id:my,method,params};rpcLog.write(JSON.stringify({dir:'req',t:Date.now(),req})+'\n');ws.send(JSON.stringify(req));setTimeout(()=>{if(pend.has(my)){pend.delete(my);rej(new Error('timeout '+method));}},20000);});
const ok=(m,w)=>{if(m.error||m.result?.error)throw new Error(w+': '+JSON.stringify(m.error??m.result.error));return m.result;};
const until=async(c,ms,w)=>{const t=Date.now();while(!c()){if(Date.now()-t>ms)throw new Error('timeout waiting for '+w);await new Promise(r=>setTimeout(r,25));}};
const log=(...a)=>console.log(new Date().toISOString().slice(11,23),...a);
const slice=async(addr,len)=>Buffer.from(ok(await call('state.getMemorySlice',{target:'sh4',address:addr,length:len}),'slice '+hex(addr)).data);
const u32=(b,o)=>b.readUInt32LE(o), u64=(b,o)=>Number(b.readBigUInt64LE(o));
async function guest(){const cpu=await slice(G_CPU,0xb4);return{R15:u32(cpu,0x3c),CPSR:u32(cpu,0x40),R:[...Array(16).keys()].map(i=>u32(cpu,i*4)),cycles:u64(await slice(CYC,8),0),vblank:u64(await slice(VBL,8),0),irq_entries:u64(await slice(IRQ,8),0),irq_iret_depth:u32(await slice(DEPTH,4),0),h5_armed:(await slice(ARMED,1))[0],h5_complete:(await slice(COMPLETE,1))[0],h5_retired:u32(await slice(RETIRED,4),0),h5_interpreter_retired:u32(await slice(INTERP_RET,4),0),native_calls_total:u64(await slice(NATIVE,8),0)};}
const result={verdict:'?'};
await new Promise((res,rej)=>{ws.on('open',res);ws.on('error',rej);});
try{
  const d=ok(await call('debugger.describe'),'describe'); log('protocol',d.protocol,(d.capabilities??[]).join(','));
  ok(await call('state.watch',{expressions:['dc.sh4.cpu.pc']}),'watch');
  const cat={muted:false,soloed:false}; ok(await call('breakpoints.setCategoryStates',{categories:{events:cat,sh4:cat,arm7:cat,dsp:cat}}),'categories');
  await until(()=>ticks>0,5000,'first tick'); if(exec.state!=='paused'){ok(await call('control.pause'),'pause');await until(()=>exec.state==='paused',10000,'pause');}
  log('start state',JSON.stringify(exec),'trace.status',JSON.stringify(ok(await call('trace.status'),'status0')));
  const bp=ok(await call('breakpoints.add',{event:'dc.sh4.cpu.pc',address:START,kind:'code'}),'bp start'); result.breakpoint=bp; log('breakpoint',JSON.stringify(bp));
  const prev=exec.stopId; ok(await call('control.runUntil'),'runUntil');
  await until(()=>exec.state==='paused'&&exec.stopId!==prev,STOP_TIMEOUT_MS,'the start breakpoint'); const sid=exec.stopId; await until(()=>tickPc.has(sid),5000,'tick pc');
  const stop={stopId:sid,reason:exec.reason,bpId:exec.breakpointId,shpc:hex(tickPc.get(sid))}; result.stop=stop; log('stopped',JSON.stringify(stop));
  const bytes=(await slice(START,2)).toString('hex'); const g=await guest(); const consoleTxt=fs.readFileSync(CONSOLE,'utf8');
  let immHash='absent'; try{immHash=crypto.createHash('sha256').update(fs.readFileSync(OUT+'/media/sma2_h5_immediate.bin')).digest('hex');}catch{}
  const bpHits=(consoleTxt.match(/CHECKPOINT_OK restored frame=200/g)||[]).length;
  result.start_state={bytes,guest:g,checkpoint_ok_lines:bpHits,immediate_file_sha256:immHash,pc:hex(tickPc.get(sid))};
  fs.writeFileSync(OUT+'/start_state.json',JSON.stringify(result.start_state,null,1)); log('start state',JSON.stringify(result.start_state));
  const checks={
    stopped_by_breakpoint_at_start: stop.reason==='breakpoint'&&tickPc.get(sid)===START,
    opcode_bytes_50e7: bytes==='50e7',
    checkpoint_immediate_file_identical: immHash==='50282ea4be8bea13b9063a38438dcde0b0b330bf0f3b929edf525bb112af32e6', console_ok_lines_not_more_than_one: bpHits<=1,
    h5_armed: g.h5_armed===1, h5_not_complete: g.h5_complete===0, retired_zero: g.h5_retired===0,
    R15: g.R15===0x0809B278, CPSR: g.CPSR===0x6000003F, cycles: g.cycles===56424200, vblank: g.vblank===201, irq_entries: g.irq_entries===192,
    native_zero: g.native_calls_total===0, interpreter_retired_one: g.h5_interpreter_retired===1 };
  result.start_checks=checks; for(const [k,v] of Object.entries(checks)) log(v?'PASS':'FAIL','start check',k);
  const st0=ok(await call('trace.status'),'status pre-arm'); if(st0.state!=='off')checks.recorder_off=false;
  if(!Object.values(checks).every(Boolean))throw new Error('start-state validation failed: NOT arming (see start_checks)');
  const arm=ok(await call('trace.arm',{target:'sh4',path:TRACE,instructionLimit:LIMIT}),'trace.arm'); const st1=ok(await call('trace.status'),'status armed');
  log('armed',JSON.stringify(st1)); if(st1.state!=='armed'||st1.recorded!==0)throw new Error('not armed'); result.armed_status=st1;
  ok(await call('breakpoints.remove',{id:stop.bpId}),'remove bp');
  const tc=Date.now(); ok(await call('control.runUntil'),'runUntil capture'); let st;
  for(;;){st=ok(await call('trace.status'),'status');if(st.limitReached||st.state==='incomplete')break;if(Date.now()-tc>CAPTURE_WAIT_MS)throw new Error('limit not reached');await new Promise(r=>setTimeout(r,50));}
  log('limit reached',JSON.stringify(st)); result.limit_status=st; result.capture_ms=Date.now()-tc;
  if(exec.state!=='paused'){ok(await call('control.pause'),'pause final');await until(()=>exec.state==='paused',10000,'pause final');}
  const tw=Date.now(); do{st=ok(await call('trace.status'),'status final');if(st.written)break;await new Promise(r=>setTimeout(r,100));}while(Date.now()-tw<5000);
  result.final_status=st; result.guest_after_pause=await guest(); log('final',JSON.stringify(st));
  result.verdict=(st.written&&st.state==='complete')?'CAPTURE_OK':'CAPTURE_NOT_COMPLETE';
}catch(e){result.verdict='ERROR';result.error=e.message;console.error('FAILED:',e.message);}
fs.writeFileSync(OUT+'/client_result.json',JSON.stringify(result,null,1)); rpcLog.end();
try{ws.close();}catch{} setTimeout(()=>process.exit(result.verdict==='CAPTURE_OK'?0:1),300);
