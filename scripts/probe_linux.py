"""Real Linux ptrace/ASLR preflight. No root privileges required."""
import ctypes, os, signal, sys
if sys.platform!='linux' or os.uname().machine!='x86_64':
    sys.exit('Backend requires x86-64 Linux; run this in Ubuntu WSL2.')
libc=ctypes.CDLL(None,use_errno=True)
pid=os.fork()
if pid==0:
    if libc.personality(0x0040000)==-1 or libc.ptrace(0,0,0,0)==-1:
        os._exit(125)
    os.execve('/bin/true',['/bin/true'],{})
_,status=os.waitpid(pid,0)
if not os.WIFSTOPPED(status):
    sys.exit('FAIL: personality(ADDR_NO_RANDOMIZE), PTRACE_TRACEME, or exec blocked')
assert os.WSTOPSIG(status)==signal.SIGTRAP
assert libc.ptrace(9,pid,0,0)==0 # PTRACE_SINGLESTEP
_,status=os.waitpid(pid,0)
assert os.WIFSTOPPED(status) and os.WSTOPSIG(status)==signal.SIGTRAP
os.kill(pid,signal.SIGKILL);os.waitpid(pid,0)
print('PASS: Linux',os.uname().release,'x86-64, ASLR personality, TRACEME, exec-stop, single-step')
