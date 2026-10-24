"""Deterministic golden ZIP corpus. Generates no files outside tests/fixtures."""
import io, zipfile, struct, zlib
from pathlib import Path
p=Path(__file__).parent/'fixtures';p.mkdir(exist_ok=True)
with zipfile.ZipFile(p/'standard.zip','w') as z:
 z.writestr('folder/',b'')
 z.writestr('folder/small.txt','Hello from HYPER EXTRACT!\n'.encode(),compress_type=zipfile.ZIP_DEFLATED)
 z.writestr('stored.bin',bytes(range(256)),compress_type=zipfile.ZIP_STORED)
 z.writestr('empty.txt',b'',compress_type=zipfile.ZIP_DEFLATED)
 z.writestr('한글/파일.txt','내용 테스트'.encode(),compress_type=zipfile.ZIP_DEFLATED)
with zipfile.ZipFile(p/'slip.zip','w') as z:
 z.writestr('../outside.txt','bad')
# Valid data descriptor: ZipFile treats an unseekable stream as streaming output.
class Unseekable(io.BytesIO):
 def seek(self,*args): raise io.UnsupportedOperation('no seek')
 def tell(self):raise io.UnsupportedOperation('no tell')
stream=Unseekable()
with zipfile.ZipFile(stream,'w') as z: z.writestr('descriptor.txt',b'abc'*100,compress_type=zipfile.ZIP_DEFLATED)
(p/'descriptor.zip').write_bytes(stream.getvalue())
with zipfile.ZipFile(p/'zip64_local.zip','w',allowZip64=True) as z:
 with z.open('forcezip64.txt','w',force_zip64=True) as f: f.write(b'hello zip64 local')
# Hand-authored CP949 name, ASCII content, method 0 and no UTF-8 flag.
name='테스트.txt'.encode('cp949');payload=b'legacy cp949\n';crc=zlib.crc32(payload)
loc=struct.pack('<IHHHHHIIIHH',0x04034B50,20,0,0,0,0,crc,len(payload),len(payload),len(name),0)+name+payload
cd=struct.pack('<IHHHHHHIIIHHHHHII',0x02014B50,20,20,0,0,0,0,crc,len(payload),len(payload),len(name),0,0,0,0,0,0)+name
eocd=struct.pack('<IHHHHIIH',0x06054B50,0,0,1,1,len(cd),len(loc),0)
(p/'cp949.zip').write_bytes(loc+cd+eocd)
# Unsupported methods (even if data happens to be stored): parser must FAIL closed.
b=bytearray((p/'cp949.zip').read_bytes());c=len(loc)
struct.pack_into('<H',b,8,99);struct.pack_into('<H',b,c+10,99)
(p/'unsupported.zip').write_bytes(b)
# truncated EOCD for error paths
(p/'broken.zip').write_bytes(b'PK\x03\x04oops')
print('fixtures:',*[x.name for x in p.glob('*.zip')])

# ZIP64 EOCD + locator, with sentinel ordinary EOCD values.
# Central and local records remain small; only footer is promoted to ZIP64.
zip64eocd = struct.pack('<IQHHIIQQQQ',0x06064B50,44,45,45,0,0,1,1,len(cd),len(loc))
zip64loc = struct.pack('<IIQI',0x07064B50,0,len(loc)+len(cd),1)
zip64classic = struct.pack('<IHHHHIIH',0x06054B50,0,0,65535,65535,0xFFFFFFFF,0xFFFFFFFF,0)
(p/'zip64_eocd.zip').write_bytes(loc+cd+zip64eocd+zip64loc+zip64classic)
