"""Build-time identity allowlist for the four controlled demo images."""
import json,pathlib,sys
def digest(data):
    h=14695981039346656037
    for b in data:h=((h^b)*1099511628211)&0xffffffffffffffff
    return f'0x{h:016x}'
images={pathlib.Path(p).name.removeprefix('demo_'):digest(pathlib.Path(p).read_bytes()) for p in sys.argv[2:]}
pathlib.Path(sys.argv[1]).write_text(json.dumps(images,indent=2)+'\n')
