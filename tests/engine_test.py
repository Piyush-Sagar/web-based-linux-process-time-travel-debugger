"""Exercise live ptrace, persisted checkpoint restoration, and negative paths."""
import json, os, subprocess, sys, tempfile, time
binary = sys.argv[1] if len(sys.argv)>1 else 'build/ttd'

class Client:
    def __init__(self):
        self.p = subprocess.Popen([binary],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
    def call(self, action, ok=True, **kwargs):
        self.p.stdin.write(json.dumps(dict(action=action,**kwargs))+'\n'); self.p.stdin.flush()
        response=json.loads(self.p.stdout.readline())
        assert response['ok']==ok, response
        return response['result'] if ok else response
    def close(self):
        self.p.stdin.close(); assert self.p.wait(timeout=5)==0

with tempfile.TemporaryDirectory() as directory:
    c=Client()
    initial=c.call('launch',demo='crash'); original_pid=initial['pid']
    assert initial['cursor']==0 and initial['status']=='paused'
    one=c.call('step'); assert one['cursor']==1
    assert c.call('pause')['cursor']==1
    fault=c.call('continue'); assert fault['status']=='crashed' and fault['live']['signal']==11
    assert bytes.fromhex(fault['observed_output_hex'])==b'recorded once\n'
    events=c.call('events'); crash_step=fault['cursor']
    syscalls=[e for e in events if e['kind']=='syscall']
    assert {e['syscall']['number'] for e in syscalls}=={1,228,318}
    assert any(e['syscall']['output'] for e in syscalls)
    assert len(events)>60 and len(fault['checkpoint_steps'])>2
    assert c.call('history',step=10)['execution_restored'] is False
    ended=c.call('step'); assert ended['status']=='terminated' and ended['live']['signal']==11
    path=os.path.join(directory,'crash.json'); c.call('save',path=path)
    # Targets both cross nondeterministic syscalls and start at later checkpoints.
    targets=sorted(set([1,19,39,crash_step-1,crash_step]))
    proof=[]
    for target in targets:
        restored=c.call('restore',step=target)
        assert restored['pid']>0 and restored['pid']!=original_pid
        assert restored['mode']=='replay' and restored['restoration']['verified']
        assert restored['observed_output_hex']==''
        assert restored['live']['registers']==events[target]['registers']
        assert restored['live']['memory']==events[target]['memory']
        proof.append(dict(target=target,**restored['restoration'],pid=restored['pid']))
    # Restore step 0 then execute all inputs again from recorded results.
    restored=c.call('restore',step=0)
    fault_again=c.call('continue')
    assert fault_again['status']=='crashed'
    assert fault_again['observed_output_hex']==''
    assert fault_again['live']['registers']==events[crash_step]['registers']
    assert fault_again['live']['memory']==events[crash_step]['memory']
    c.call('restore',step=crash_step+1,ok=False)
    c.close()
    # New tracer, persisted trace, actual restoration after original tracer exits.
    fresh=Client();fresh.call('load',path=path)
    persisted=fresh.call('restore',step=39)
    assert persisted['live']['registers']==events[39]['registers']
    assert persisted['live']['memory']==events[39]['memory']
    fresh.call('launch',demo='clean');clean=fresh.call('continue')
    assert clean['status']=='exited' and clean['live']['exit_code']==0
    fresh.call('launch',demo='forbidden')
    rejected=fresh.call('continue',ok=False)
    assert 'unsupported syscall 39' in rejected['error'] and rejected['state']['status']=='rejected'
    fresh.call('launch',demo='loop')
    async_run=fresh.call('continue',**{'async':True});assert async_run['running']
    time.sleep(.08);paused=fresh.call('pause');assert not paused['running'] and paused['cursor']>0
    time.sleep(.08);assert fresh.call('state')['cursor']==paused['cursor']
    bounded=fresh.call('continue');assert bounded['cursor']==2000
    fresh.call('step',ok=False)
    fresh.call('launch',demo='/bin/ls',ok=False)
    corrupt=json.load(open(path));corrupt['trace']['events'][1]['signal']=99
    with open(os.path.join(directory,'corrupt.json'),'w') as f: json.dump(corrupt,f)
    error=fresh.call('load',path=os.path.join(directory,'corrupt.json'),ok=False)
    assert 'checksum' in error['error']
    # Deliberately re-sign inconsistent data: replay must detect real divergence,
    # rather than overwrite ordinary instruction state to match the saved log.
    def rewrite(envelope, name):
        payload=json.dumps(envelope['trace'],sort_keys=True,separators=(',',':'),ensure_ascii=False).encode()
        h=14695981039346656037
        for b in payload:h=((h^b)*1099511628211)&0xffffffffffffffff
        envelope['checksum']=f'0x{h:016x}'
        destination=os.path.join(directory,name)
        with open(destination,'w') as f:json.dump(envelope,f)
        return destination
    divergent=json.load(open(path));memory=divergent['trace']['events'][9]['memory']
    memory['hex']='ff'+memory['hex'][2:]
    fresh.call('load',path=rewrite(divergent,'divergent.json'))
    divergence=fresh.call('restore',step=19,ok=False)
    assert 'memory divergence at 9' in divergence['error']
    assert divergence['state']['pid']==-1 and divergence['state']['live'] is None
    bad_mapping=json.load(open(path));bad_mapping['trace']['checkpoints'][0]['regions'][0]['start']+=8
    fresh.call('load',path=rewrite(bad_mapping,'mapping.json'))
    mapping=fresh.call('restore',step=19,ok=False)
    assert 'mapping layout mismatch' in mapping['error'] and mapping['state']['pid']==-1
    identity=json.load(open(path));identity['trace']['executable_hash']='modified'
    fresh.call('load',path=rewrite(identity,'identity.json'),ok=False)
    fresh.close()
    print(json.dumps(dict(result='PASS',linux=os.uname().release,crash_step=crash_step,
                         original_pid=original_pid,checkpoints=fault['checkpoint_steps'],
                         restored_targets=proof,persisted_restore=39,
                         nondeterministic_replay='all captured inputs matched',
                         side_effects='original pipe output recorded once; zero pipe bytes emitted by replay',
                         integrity='checksum, replay divergence, mapping layout, and executable identity failures rejected',
                         rejection='getpid blocked before execution'),indent=2))
