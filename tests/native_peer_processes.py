import pathlib,socket,subprocess,sys
exe=str(pathlib.Path(sys.argv[1]).resolve())
with socket.socket() as reserve:
    reserve.bind(('127.0.0.1',0));port=reserve.getsockname()[1]
host=subprocess.Popen([exe,'host',str(port)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
client=None
try:
    client=subprocess.Popen([exe,'client',str(port)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    for process in (host,client):
        out,err=process.communicate(timeout=12)
        if process.returncode:raise RuntimeError(err.decode()+out.decode())
        print(out.decode().strip())
finally:
    for process in (host,client):
        if process and process.poll() is None:process.kill();process.wait()
