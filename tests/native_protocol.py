"""Black-box native TCP framing/validation test, without starting Elden Ring."""
import pathlib,socket,struct,subprocess,sys,time
exe=str(pathlib.Path(sys.argv[1]).resolve())
with socket.socket() as reserved:
    reserved.bind(('127.0.0.1',0));port=reserved.getsockname()[1]
host=subprocess.Popen([exe,'host',str(port)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
def connect():
    end=time.monotonic()+3
    while True:
        try:
            peer=socket.create_connection(('127.0.0.1',port),timeout=1);peer.settimeout(3);return peer
        except ConnectionRefusedError:
            if time.monotonic()>end:raise
            time.sleep(.01)
def frame(kind,quantity=0,sequence=0):
    return b'SLR3'+bytes([kind,0,0,0])+struct.pack('!QQII',202,sequence,quantity,0)+b'S'*32
def read_frame(peer):
    result=b''
    while len(result)<64:
        data=peer.recv(64-len(result))
        if not data:raise EOFError()
        result+=data
    return result
try:
    with connect() as peer:
        invalid=bytearray(frame(1));invalid[5]=1;peer.sendall(invalid)
        try:
            while True:read_frame(peer)
        except (EOFError,ConnectionResetError):pass
    with connect() as peer:
        data=frame(1)+frame(2,1)
        for start in range(0,len(data),7):peer.sendall(data[start:start+7])
        received=[]
        while 3 not in [f[4] for f in received]:received.append(read_frame(peer))
        award=next(f for f in received if f[4]==3)
        if struct.unpack_from('!I',award,24)[0]!=3:raise AssertionError('Wrong native quantity')
        # Two identical reliable messages must yield only one pending grant.
        peer.sendall(frame(3,2,1)+frame(3,2,1))
        output,error=host.communicate(timeout=5)
        if host.returncode:raise RuntimeError(error.decode()+output.decode())
    print('PASS: reserved-byte rejection, fragmented/coalesced frames, native duplicate suppression')
finally:
    if host.poll() is None:host.kill();host.wait()
