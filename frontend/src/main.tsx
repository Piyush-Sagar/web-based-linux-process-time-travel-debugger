import React, {useEffect, useRef, useState} from 'react';
import {createRoot} from 'react-dom/client';
import './style.css';

type Call = {number:number; arguments:string[]; result?:string; output:string; write_hex:string; actual_output_hex?:string};
type Event = {step:number; kind:string; signal:number; status:string; registers?:Record<string,string>; memory?:{address:string;hex:string;label:string}; syscall:Call|null; exit_code?:number};
type Proof = {checkpoint_step:number;target_step:number;executed_instructions:number;verified:boolean};
type Session = {id:string;demo:string;pid:number;status:string;mode:string;cursor:number;recorded_steps:number;checkpoint_steps:number[];live:Event|null;running:boolean;error:string;restoration?:Proof};
type Demo = {id:string;description:string};
async function request<T>(path:string, init:RequestInit={}):Promise<T> {
  const response=await fetch('/api'+path,{...init,headers:{'Content-Type':'application/json',...init.headers}});
  const body=await response.json(); if(!response.ok)throw new Error(body.error ?? `HTTP ${response.status}`);return body;
}
const syscallNames:Record<number,string>={1:'write',60:'exit',228:'clock_gettime',231:'exit_group',318:'getrandom'};
function Icon({name}:{name:'play'|'step'|'pause'|'rewind'|'terminal'|'close'|'arrow'}) {
  const paths={play:'M8 5l11 7-11 7V5Z',step:'M5 5l10 7L5 19V5Zm14 0v14',pause:'M8 5v14M16 5v14',rewind:'M9 5 2 12l7 7V5Zm13 0-9 7 9 7V5Z',terminal:'m4 6 6 6-6 6m9 0h7',close:'m6 6 12 12M18 6 6 18',arrow:'m8 5 7 7-7 7'};
  return <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.7" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true"><path d={paths[name]}/></svg>;
}
function describe(e:Event):string {
  if(e.signal)return `${e.signal===11?'SIGSEGV':`Signal ${e.signal}`} · ${e.kind==='exit'?'process terminated':'fault captured'}`;
  if(e.syscall)return `${syscallNames[e.syscall.number] ?? `syscall ${e.syscall.number}`} ${e.syscall.result?`→ ${e.syscall.result}`:''}`;
  if(e.kind==='exit')return `Process exited · code ${e.exit_code}`;
  return e.kind==='launch'?'Initial instruction stop':'Instruction completed';
}
function word(hex:string):bigint {return BigInt('0x'+(hex.match(/../g)??[]).reverse().join(''));}
function text(hex:string):string {return (hex.match(/../g)??[]).map(b=>String.fromCharCode(parseInt(b,16))).join('');}

function App() {
  const [demos,setDemos]=useState<Demo[]>([]),[sessions,setSessions]=useState<Session[]>([]);
  const [id,setId]=useState(''),[demo,setDemo]=useState('crash'),[events,setEvents]=useState<Event[]>([]);
  const [view,setView]=useState<'live'|'history'>('live'),[selected,setSelected]=useState(0),[historical,setHistorical]=useState<Event|null>(null);
  const [busy,setBusy]=useState(false),[error,setError]=useState(''),[connected,setConnected]=useState(false),[proof,setProof]=useState<Proof|null>(null);
  const [filter,setFilter]=useState('all'),[eventPage,setEventPage]=useState(0);
  const [dismissed,setDismissed]=useState('');
  const currentId=useRef(id);currentId.current=id;
  const session=sessions.find(s=>s.id===id);
  function upsert(s:Session) {setSessions(prev=>{const exists=prev.some(x=>x.id===s.id);return exists?prev.map(x=>x.id===s.id?s:x):[...prev,s];});}
  useEffect(()=>{
    Promise.all([request<Demo[]>('/demos'),request<Session[]>('/sessions')]).then(([d,s])=>{setDemos(d);setSessions(s);if(s.length){setId(s[0].id);if(!s[0].live)setView('history');}}).catch(e=>setError(e.message));
    let stopped=false;let socket:WebSocket;let retry:ReturnType<typeof setTimeout>;
    function connect(){
      socket=new WebSocket(`${location.protocol==='https:'?'wss':'ws'}://${location.host}/ws`);
      socket.onopen=()=>setConnected(true);
      socket.onmessage=message=>{const update=JSON.parse(message.data);if(update.type==='session')upsert(update.session);};
      socket.onerror=()=>setConnected(false);
      socket.onclose=()=>{if(!stopped){setConnected(false);retry=setTimeout(connect,1500);}};
    }connect();
    return ()=>{stopped=true;clearTimeout(retry);socket?.close();};
  },[]);
  useEffect(()=>{
    if(!id)return;const controller=new AbortController();
    request<Event[]>(`/sessions/${id}/events`,{signal:controller.signal}).then(setEvents).catch(e=>{if(e.name!=='AbortError')setError(e.message);});
    return ()=>controller.abort();
  },[id,session?.recorded_steps]);
  useEffect(()=>{
    if(view!=='history'||!id)return;setHistorical(null);const controller=new AbortController();
    request<{event:Event}>(`/sessions/${id}/state?step=${selected}`,{signal:controller.signal}).then(r=>setHistorical(r.event)).catch(e=>{if(e.name!=='AbortError')setError(e.message);});
    return ()=>controller.abort();
  },[id,selected,view]);
  // Reconcile after reconnection; controls remain usable if WS is unavailable.
  useEffect(()=>{const timer=setInterval(()=>{if(currentId.current)request<Session>(`/sessions/${currentId.current}`).then(upsert).catch(()=>{});},2000);return()=>clearInterval(timer);},[]);
  const event=view==='history'?historical:session?.live;
  const cursor=view==='history'?selected:session?.cursor??0;
  const max=Math.max(0,(session?.recorded_steps??1)-1);
  const previous=events[Math.max(0,cursor-1)];
  function browse(step:number) {setView('history');setSelected(Math.max(0,Math.min(max,step)));setProof(null);}
  async function perform(action:string,step?:number) {
    if(!id)return;setBusy(true);setError('');setDismissed('');
    try {const s=await request<Session>(`/sessions/${id}/control`,{method:'POST',body:JSON.stringify({action,step})});upsert(s);
      if(action==='restore'){setProof(s.restoration??null);setView('live');setSelected(s.cursor);}else if(action==='step'||action==='continue'){setProof(null);setView('live');}
    }catch(e){setError((e as Error).message);try{upsert(await request<Session>(`/sessions/${id}`));}catch{ /* retain explicit error */ }}finally{setBusy(false);}
  }
  async function launch() {
    setBusy(true);setError('');
    try{const s=await request<Session>('/sessions',{method:'POST',body:JSON.stringify({demo})});upsert(s);setId(s.id);setEvents([]);setSelected(0);setView('live');setProof(null);setEventPage(0);}
    catch(e){setError((e as Error).message);}finally{setBusy(false);}
  }
  async function remove() {
    if(!id)return;setBusy(true);try{await request(`/sessions/${id}`,{method:'DELETE'});const remaining=sessions.filter(s=>s.id!==id);setSessions(remaining);setId(remaining[0]?.id??'');setSelected(0);setEvents([]);setProof(null);setView(remaining[0]?.live?'live':'history');}catch(e){setError((e as Error).message);}finally{setBusy(false);}
  }
  const visible=events.filter(e=>filter==='all'||(filter==='events'?e.kind!=='instruction':e.signal!==0));
  const pages=Math.max(1,Math.ceil(visible.length/12));const page=Math.min(eventPage,pages-1);
  const output=events.filter(e=>e.syscall?.number===1).map(e=>text(e.syscall!.actual_output_hex??'')).join('');
  const values=events.filter(e=>e.memory).map(e=>({step:e.step,value:Number(word(e.memory!.hex.slice(0,16)))}));
  const peak=Math.max(1,...values.map(x=>x.value));const points=values.map(p=>`${20+(p.step/Math.max(1,max))*940},${100-(p.value/peak)*66}`).join(' ');
  const actionable=!!session && !session.running && !busy;
  return <div className="app">
    <a className="skip" href="#workspace">Skip to workspace</a>
    <header className="topbar"><div className="brand"><span className="logo"><Icon name="rewind"/></span><span>chronoscope<span className="version"> / v0.1</span></span></div><div className="connection"><span className={`dot ${connected?'online':''}`}/>{connected?'Live connection':'Reconnecting…'}<span className="tag">LOCAL · LINUX x86-64</span></div></header>
    <div className="shell">
      <aside className="sidebar"><div className="overline">PROCESS WORKSPACE</div><h1>Time-travel<br/>debugger</h1><p className="intro">Record a failure.<br/>Return to the state before it.</p>
        <div className="launch-box"><label htmlFor="demo">Demo executable</label><select id="demo" value={demo} onChange={e=>setDemo(e.target.value)}>{demos.map(d=><option value={d.id} key={d.id}>{d.id}.cpp</option>)}</select><p>{demos.find(d=>d.id===demo)?.description??'Connecting to Linux backend…'}</p><button className="primary full" onClick={launch} disabled={busy||demos.length===0}><Icon name="terminal"/>{busy?'Working…':'Launch process'}</button></div>
        <div className="section-heading"><h2>Sessions</h2><span>{sessions.length} / 8</span></div>
        <div className="sessions">{sessions.length===0?<p className="empty-small">Launch a demo to begin recording.</p>:sessions.map(s=><button key={s.id} className={`session ${s.id===id?'active':''}`} aria-pressed={s.id===id} onClick={()=>{setId(s.id);setSelected(0);setView(s.live?'live':'history');setProof(null);setEventPage(0);}}><span className={`dot ${s.status==='crashed'?'danger':s.pid>0?'online':''}`}/><span><strong>{s.demo}.cpp</strong><small>{s.id} · {s.status} · {s.recorded_steps} states</small></span><Icon name="arrow"/></button>)}</div>
        {id&&<button className="remove" onClick={remove} disabled={busy}><Icon name="close"/>Remove selected session</button>}
        <div className="scope-note"><span className="overline">SUPPORTED SCOPE</span><p>Bundled single-threaded demos.<br/>2,000 instructions per trace.<br/>Checkpoints every 20 steps.</p><p>Files, networking, threads, and arbitrary executables are rejected.</p></div>
      </aside>
      <main id="workspace" tabIndex={-1}>
        <div className="workspace-heading"><div><div className="overline">EXECUTION ANALYSIS</div><h2>{session?`${session.demo}.cpp`:'Your next failure, explained.'}</h2><p>{session?`${session.id} / ${session.pid>0?`PID ${session.pid}`:'No live process'} / ${session.running?'running':session.status}`:'Launch the crash demo to inspect and replay real Linux execution.'}</p></div><div className={`status-pill ${session?.status==='crashed'?'crash':''}`}>{session?.running?'Recording':session?.status??'Ready to trace'}</div></div>
        {(error||(session?.error&&dismissed!==`${id}:${session.error}`))&&<div className="error" role="alert"><strong>Operation rejected</strong><span>{error||session?.error}</span><button onClick={()=>{setError('');setDismissed(`${id}:${session?.error}`);}} aria-label="Dismiss error"><Icon name="close"/></button></div>}
        {!session?<section className="welcome"><span className="large-icon"><Icon name="rewind"/></span><h3>Execution has a history.</h3><p>The crash demo records random input, time, and a short computation before a segmentation fault. Inspect a saved state, then restore a checkpoint and execute forward to it.</p><button className="primary" onClick={launch} disabled={busy||!demos.length}>Launch {demo}.cpp <Icon name="play"/></button><div className="welcome-steps"><span><b>01</b> Record</span><span><b>02</b> Inspect</span><span><b>03</b> Restore & replay</span></div></section>:<>
          <div className="toolbar"><div className="controls"><button onClick={()=>perform('continue')} disabled={!actionable||session.status!=='paused'}><Icon name="play"/>Continue</button><button onClick={()=>perform('step')} disabled={!actionable||!['paused','crashed'].includes(session.status)}><Icon name="step"/>{session.status==='crashed'?'Deliver signal':'Step'}</button><button onClick={()=>perform('pause')} disabled={busy||!session.running}><Icon name="pause"/>Pause</button></div><button className="subtle" onClick={()=>perform('terminate')} disabled={!actionable||session.pid<0}>Terminate</button></div>
          <section className="panel timeline"><div className="panel-heading"><h3>Execution timeline</h3><span>{session.recorded_steps} states <span className="separator">/</span> {session.checkpoint_steps.length} checkpoints</span></div><div className="timeline-meta"><span>watch_state[0] <span className="muted">· recorded values</span></span><div className="legend"><span><i className="legend-line"/>Memory value</span><span><i className="legend-checkpoint"/>Checkpoint</span></div></div>
            <svg className="chart" viewBox="0 0 980 120" preserveAspectRatio="none" role="img" aria-label={`Recorded watch_state[0] over steps 0 through ${max}`}><line x1="20" x2="960" y1="100" y2="100" className="grid"/><line x1="20" x2="960" y1="60" y2="60" className="grid"/>{session.checkpoint_steps.map(s=><line key={s} x1={20+s/Math.max(1,max)*940} x2={20+s/Math.max(1,max)*940} y1="25" y2="100" className="checkpoint-line"/>)}<polyline points={points} fill="none" className="value-line"/><line x1={20+cursor/Math.max(1,max)*940} x2={20+cursor/Math.max(1,max)*940} y1="10" y2="110" className="cursor-line"/></svg>
            <label className="sr-only" htmlFor="timeline">Historical step</label><input id="timeline" type="range" min="0" max={max} value={cursor} onChange={e=>browse(Number(e.target.value))} disabled={busy}/><div className="timeline-axis"><span>0 · start</span><span>step {cursor}</span><span>{max} · latest</span></div>
            <div className="navigation"><div className="step-picker"><button onClick={()=>browse(cursor-1)} disabled={cursor===0||busy} aria-label="Previous historical step">←</button><label htmlFor="target">Step</label><input id="target" type="number" min="0" max={max} value={cursor} onChange={e=>browse(Number(e.target.value))}/><span>/ {max}</span><button onClick={()=>browse(cursor+1)} disabled={cursor>=max||busy} aria-label="Next historical step">→</button></div><button className="primary" onClick={()=>perform('restore',cursor)} disabled={!actionable||!event?.registers}><Icon name="rewind"/>Restore & replay to step</button></div>
          </section>
          <div className={`view-banner ${view==='history'?'historical':'live'}`} role="status"><div><strong data-testid="view-mode">{view==='history'?'Historical inspection':session.pid<0?'No live execution':session.mode==='replay'?'Live restored execution':'Live recording state'}</strong><span>{view==='history'?'Viewing saved data. Execution has not been restored.':session.pid<0?'The process has ended. Restore an earlier recorded state to resume.':session.mode==='replay'?'A checkpoint was restored into a live process. Instructions were replayed and compared.':'State captured from the stopped Linux process.'}</span></div>{view==='history'&&<button onClick={()=>{setView('live');setProof(null);}} disabled={!session.live}>Return to live state</button>}{proof&&session.pid>0&&<span className="proof" data-testid="replay-proof">Verified · checkpoint {proof.checkpoint_step} → step {proof.target_step} · {proof.executed_instructions} instructions</span>}</div>
          {event?.signal!==0&&event?.signal&&<div className="fault-banner"><strong>{describe(event)}</strong><span>Restore an earlier step to inspect execution before the fault.</span></div>}
          <div className="inspectors"><section className="panel"><div className="panel-heading"><h3>Registers</h3><span>x86-64 · step {cursor}</span></div>{event?.registers?<div className="registers" data-testid="registers">{Object.entries(event.registers).map(([key,value])=><div key={key} className={previous?.registers?.[key]!==value?'changed':''}><span>{key.toUpperCase()}</span><code data-register={key}>{value}</code></div>)}</div>:<p className="panel-empty">{view==='history'&&!historical?'Loading saved state…':'No registers available for this terminated process. Restore an earlier step.'}</p>}<div className="panel-foot">Highlighted values changed from the previous recorded step.</div></section>
            <section className="panel"><div className="panel-heading"><h3>Selected memory</h3><span>64 bytes</span></div>{event?.memory?<><div className="memory-title"><code>{event.memory.address}</code><span>{event.memory.label}</span></div><table className="memory"><caption className="sr-only">Selected memory as eight unsigned 64-bit little-endian words</caption><thead><tr><th>Offset</th><th>Bytes · little-endian</th><th>u64 value</th></tr></thead><tbody>{Array.from({length:8},(_,i)=>{const value=event.memory!.hex.slice(i*16,i*16+16);return <tr key={i} className={previous?.memory?.hex.slice(i*16,i*16+16)!==value?'changed':''}><th>+{(i*8).toString(16).padStart(2,'0')}</th><td><code data-memory={i}>{value.match(/../g)?.join(' ')}</code></td><td><code>{word(value).toString()}</code></td></tr>;})}</tbody></table></>:<p className="panel-empty">No selected memory at this state.</p>}<div className="output"><h4>Captured stdout / stderr</h4><pre>{output||'No output recorded.'}</pre><p>Original output is captured. Replay suppresses external writes.</p></div></section>
          </div>
          <section className="panel event-panel"><div className="panel-heading"><h3>Recorded events</h3><label className="filter">Show<select aria-label="Event filter" value={filter} onChange={e=>{setFilter(e.target.value);setEventPage(0);}}><option value="all">All instructions</option><option value="events">Syscalls & events</option><option value="signals">Signals</option></select></label></div><div className="event-list">{visible.slice(page*12,(page+1)*12).map(e=><button key={e.step} className={`event-row ${e.step===cursor?'selected':''} ${e.signal?'signal':''}`} onClick={()=>browse(e.step)} aria-pressed={view==='history'&&e.step===cursor}><code>#{e.step.toString().padStart(4,'0')}</code><span className="event-kind">{e.kind}</span><span>{describe(e)}</span><code className="event-rip">{e.registers?.rip??'—'}</code><Icon name="arrow"/></button>)}{visible.length===0&&<p className="panel-empty">No events match this filter.</p>}</div><div className="pagination"><span>{visible.length} matching events · page {page+1} of {pages}</span><div><button disabled={page===0} onClick={()=>setEventPage(page-1)}>Previous</button><button disabled={page>=pages-1} onClick={()=>setEventPage(page+1)}>Next</button></div></div></section>
        </>}
        <footer>Custom C++ ptrace engine <span>·</span> Local demo scope <span>·</span> {connected?'Real backend data connected':'Connection unavailable — retained data may be stale'}</footer>
      </main>
    </div>
  </div>;
}
createRoot(document.getElementById('root')!).render(<React.StrictMode><App/></React.StrictMode>);
