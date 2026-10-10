"""Real C++ HTTP/WebSocket API and session persistence integration test."""
import base64, http.client, json, os, pathlib, socket, struct, subprocess, tempfile, time
ROOT=pathlib.Path(__file__).resolve().parents[1]
PORT=8091
class API:
    def call(self, method, path, data=None, expected=200, origin=None):
        conn=http.client.HTTPConnection('127.0.0.1',PORT,timeout=15)
        headers={'Content-Type':'application/json'}
        if origin: headers['Origin']=origin
        conn.request(method,path,None if data is None else json.dumps(data),headers)
        response=conn.getresponse();body=json.loads(response.read());conn.close()
        assert response.status==expected,(response.status,body)
        return body
    def wait(self,id):
        deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            state=self.call('GET','/api/sessions/'+id)
            if not state['running']: return state
            time.sleep(.03)
        raise AssertionError('continue timeout')
def exact(sock,n):
    data=b''
    while len(data)<n:
        chunk=sock.recv(n-len(data))
        if not chunk: raise RuntimeError('WebSocket closed')
        data+=chunk
    return data
def frame(sock):
    a,b=exact(sock,2);size=b&127
    if size==126: size=struct.unpack('!H',exact(sock,2))[0]
    elif size==127: size=struct.unpack('!Q',exact(sock,8))[0]
    assert not b&128 and a&15==1
    return json.loads(exact(sock,size))
def ws():
    s=socket.create_connection(('127.0.0.1',PORT),timeout=10)
    key=base64.b64encode(os.urandom(16)).decode()
    s.sendall((f'GET /ws HTTP/1.1\r\nHost: 127.0.0.1:{PORT}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n').encode())
    header=b''
    while not header.endswith(b'\r\n\r\n'): header+=exact(s,1)
    assert b'101 Switching Protocols' in header
    assert frame(s)['type']=='connected'
    return s
def start(directory):
    p=subprocess.Popen([str(ROOT/'build/ttd-server'),str(PORT),directory],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    api=API()
    for _ in range(100):
        if p.poll() is not None: raise AssertionError(p.stderr.read())
        try: api.call('GET','/api/health');return p
        except OSError: time.sleep(.05)
    raise AssertionError('server startup timeout')

with tempfile.TemporaryDirectory() as directory:
    p=start(directory);api=API();web=ws()
    try:
        s=api.call('POST','/api/sessions',{'demo':'crash'},201);id=s['id'];prefix='/api/sessions/'+id
        assert frame(web)['session']['pid']==s['pid']
        api.call('POST',prefix+'/control',{'action':'step'})
        api.call('POST',prefix+'/control',{'action':'continue'})
        fault=api.wait(id);assert fault['status']=='crashed'
        events=api.call('GET',prefix+'/events')
        history=api.call('GET',prefix+'/state?step=39');assert not history['execution_restored']
        api.call('POST',prefix+'/control',{'action':'step'}) # deliver SIGSEGV, original terminates
        proofs=[]
        for step in [1,39,fault['cursor']-1,fault['cursor']]:
            restored=api.call('POST',prefix+'/control',{'action':'restore','step':step})
            assert restored['live']['registers']==events[step]['registers']
            assert restored['live']['memory']==events[step]['memory']
            assert restored['restoration']['verified'] and restored['pid']!=s['pid']
            proofs.append(restored['restoration'])
        api.call('POST',prefix+'/control',{'action':'rewind'},409)
        api.call('POST',prefix+'/control',{'action':'restore','step':9999},409)
        api.call('POST','/api/sessions',{'demo':'/bin/ls'},400)
        api.call('POST','/api/sessions',{'demo':'clean'},403,origin='https://untrusted.example')
        loop=api.call('POST','/api/sessions',{'demo':'loop'},201);lid=loop['id']
        api.call('POST',f'/api/sessions/{lid}/control',{'action':'continue'})
        time.sleep(.08)
        paused=api.call('POST',f'/api/sessions/{lid}/control',{'action':'pause'})
        time.sleep(.1);assert api.call('GET',f'/api/sessions/{lid}')['cursor']==paused['cursor']
        api.call('DELETE',f'/api/sessions/{lid}')
        rejected=api.call('POST','/api/sessions',{'demo':'forbidden'},201)
        api.call('POST',f'/api/sessions/{rejected["id"]}/control',{'action':'continue'})
        rejection=api.wait(rejected['id']);assert rejection['status']=='rejected' and 'unsupported syscall' in rejection['error']
        web.close();p.terminate();assert p.wait(timeout=10)==0
        p=start(directory)
        loaded=api.call('GET',prefix);assert loaded['status']=='loaded' and loaded['pid']==-1
        replay=api.call('POST',prefix+'/control',{'action':'restore','step':39})
        assert replay['live']['registers']==events[39]['registers'] and replay['live']['memory']==events[39]['memory']
        print(json.dumps({'result':'PASS','http':'real C++ ptrace session controls and inspectors','websocket':'live session frame received',
                          'crash_step':fault['cursor'],'restorations':proofs,'persisted_restart_restore':39,
                          'negative_checks':['unsupported action','invalid target','unknown executable','cross-origin request','unsupported syscall'],
                          'pause':'cursor unchanged after pause'},indent=2))
    finally:
        if p.poll() is None:p.terminate();p.wait(timeout=10)
