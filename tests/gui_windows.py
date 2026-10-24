"""Automate only this project's own native window; no desktop/global input.
Launch hidden, inspect actual controls, extract a golden ZIP, cancel a streaming ZIP,
and capture the application's PrintWindow rendering for visual verification.
"""
import ctypes as c,ctypes.wintypes as w,pathlib,subprocess,time,os,json,sys,zipfile,hashlib,struct,tempfile
from PIL import Image
# PrintWindow paints physical pixels; avoid DPI-virtualized bitmap dimensions.
_dpi_user=c.WinDLL('user32',use_last_error=True)
_dpi_user.SetProcessDpiAwarenessContext.argtypes=[c.c_void_p]
_dpi_user.SetProcessDpiAwarenessContext(c.c_void_p(-4))
exe=pathlib.Path(sys.argv[1]).resolve();project=pathlib.Path(__file__).resolve().parents[1]
base=pathlib.Path(tempfile.mkdtemp(prefix='hx-gui-',dir=sys.argv[2])).resolve();env=os.environ.copy();env['HX_DATA_HOME']=str(base/'data')
startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
source=base/'gui.zip'
with zipfile.ZipFile(source,'w',compression=zipfile.ZIP_DEFLATED) as z:z.writestr('한글/결과.txt','GUI 검증 내용'.encode())
process=subprocess.Popen([str(exe),str(source)],env=env,startupinfo=startup)
u=c.WinDLL('user32',use_last_error=True);g=c.WinDLL('gdi32',use_last_error=True);k=c.WinDLL('kernel32',use_last_error=True)
u.GetDlgItem.argtypes=[w.HWND,c.c_int];u.GetDlgItem.restype=w.HWND;u.SendMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];u.SendMessageW.restype=w.LPARAM
u.SetWindowTextW.argtypes=[w.HWND,w.LPCWSTR];u.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,c.c_int];u.IsWindowEnabled.argtypes=[w.HWND]
u.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
u.PrintWindow.argtypes=[w.HWND,w.HDC,w.UINT];u.GetWindowDC.argtypes=[w.HWND];u.GetWindowDC.restype=w.HDC
g.CreateCompatibleDC.argtypes=[w.HDC];g.CreateCompatibleDC.restype=w.HDC;g.CreateCompatibleBitmap.argtypes=[w.HDC,c.c_int,c.c_int];g.CreateCompatibleBitmap.restype=w.HBITMAP
g.SelectObject.argtypes=[w.HDC,w.HANDLE];g.SelectObject.restype=w.HANDLE;g.DeleteObject.argtypes=[w.HANDLE];g.DeleteDC.argtypes=[w.HDC]
u.ReleaseDC.argtypes=[w.HWND,w.HDC]
callback=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM);window=None;results=[]
@callback
def find(h,l):
 global window
 pid=w.DWORD();u.GetWindowThreadProcessId(h,c.byref(pid))
 name=c.create_unicode_buffer(128);u.GetClassNameW(h,name,128)
 if pid.value==process.pid and name.value=='HyperExtractWindow':window=h;return False
 return True
def wait_for(predicate,seconds=60):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  if predicate():return True
  if process.poll() is not None:return False
  time.sleep(.03)
 return False
def record(name,value):results.append(dict(name=name,passed=bool(value)));print(('PASS ' if value else 'FAIL ')+name,flush=True)
def text(h):
 buf=c.create_unicode_buffer(2048);u.SendMessageW(h,0x0d,len(buf),c.addressof(buf));return buf.value
def set_text(h,value):
 buf=c.create_unicode_buffer(str(value));u.SendMessageW(h,0x0c,0,c.addressof(buf))
def start(path,out):
 set_text(u.GetDlgItem(window,101),path);set_text(u.GetDlgItem(window,103),out);u.SendMessageW(window,0x111,105,0)
def done():return u.IsWindowEnabled(u.GetDlgItem(window,105))!=0
try:
 wait_for(lambda:(u.EnumWindows(find,0) and False) or window is not None,10);record('native window created',window is not None)
 if window is None:raise RuntimeError('GUI startup failed')
 record('native controls initialized',wait_for(lambda:u.GetDlgItem(window,109) and u.GetDlgItem(window,111),10))
 labels=[text(u.GetDlgItem(window,i)) for i in [102,104,105,106,107,108,109]]
 print('Control labels:',repr(labels),flush=True)
 record('Korean controls',labels==['파일 선택…','위치 선택…','압축 풀기','빠른 튜닝','자체 검사','결과 폴더 열기','취소'])
 record('filename open routed through real HDROP handler',wait_for(lambda:text(u.GetDlgItem(window,101))==str(source),10))
 out=base/'result';start(source,out);record('GUI extraction completes',wait_for(done,30) and '완료' in text(u.GetDlgItem(window,110)))
 record('GUI output bytes correct',(out/'한글'/'결과.txt').read_bytes()=='GUI 검증 내용'.encode())
 # Capture actual window rendering (not a mockup).
 u.ShowWindow.argtypes=[w.HWND,c.c_int];u.UpdateWindow.argtypes=[w.HWND]
 u.ShowWindow(window,4);u.UpdateWindow(window);time.sleep(.25)
 client=w.RECT();u.GetClientRect.argtypes=[w.HWND,c.POINTER(w.RECT)];u.GetClientRect(window,c.byref(client));bounds=[]
 u.MapWindowPoints.argtypes=[w.HWND,w.HWND,c.c_void_p,w.UINT]
 @callback
 def child_bounds(child,l):
  rect=w.RECT();u.GetWindowRect(child,c.byref(rect));u.MapWindowPoints(None,window,c.byref(rect),2)
  bounds.append(rect.left>=0 and rect.top>=0 and rect.right<=client.right and rect.bottom<=client.bottom);return True
 u.EnumChildWindows.argtypes=[w.HWND,callback,w.LPARAM];u.EnumChildWindows(window,child_bounds,0)
 record('all controls fit actual DPI client area',bounds and all(bounds))
 rect=w.RECT();u.GetWindowRect.argtypes=[w.HWND,c.POINTER(w.RECT)];u.GetWindowRect(window,c.byref(rect));width=rect.right-rect.left;height=rect.bottom-rect.top
 dc=u.GetWindowDC(window);memory=g.CreateCompatibleDC(dc);bmp=g.CreateCompatibleBitmap(dc,width,height);old=g.SelectObject(memory,bmp)
 painted=u.PrintWindow(window,memory,2)
 g.SelectObject(memory,old)
 class Header(c.Structure):_fields_=[('size',w.DWORD),('width',w.LONG),('height',w.LONG),('planes',w.WORD),('bits',w.WORD),('compression',w.DWORD),('image_size',w.DWORD),('x',w.LONG),('y',w.LONG),('used',w.DWORD),('important',w.DWORD)]
 header=Header(c.sizeof(Header),width,-height,1,32,0,0,0,0,0,0);pixels=c.create_string_buffer(width*height*4)
 g.GetDIBits.argtypes=[w.HDC,w.HBITMAP,w.UINT,w.UINT,c.c_void_p,c.c_void_p,w.UINT];rows=g.GetDIBits(memory,bmp,0,height,pixels,c.byref(header),0)
 rendered=Image.frombuffer('RGB',(width,height),pixels,'raw','BGRX',0,1) if rows else None
 visible=bool(rendered and any(lo!=hi for lo,hi in rendered.getextrema()))
 print('Render evidence:',width,height,'PrintWindow',painted,'rows',rows,'extrema',rendered.getextrema() if rendered else None,flush=True)
 if rendered:rendered.save(project/'logs'/'gui-preview.png')
 record('actual nonblank GUI rendering captured',visible)
 g.SelectObject(memory,old);g.DeleteObject(bmp);g.DeleteDC(memory);u.ReleaseDC(window,dc)
 if len(sys.argv)>3:
  cancelled=base/'cancelled';start(pathlib.Path(sys.argv[3]).resolve(),cancelled);time.sleep(.15);u.SendMessageW(window,0x111,109,0)
  record('GUI streaming cancellation',wait_for(done,60) and '취소' in text(u.GetDlgItem(window,110)) and not cancelled.exists())
 u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];u.PostMessageW(window,0x10,0,0);process.wait(timeout=10);record('GUI exits cleanly',process.returncode==0)
except Exception as error:record(repr(error),False)
finally:
 if process.poll() is None:process.terminate();process.wait()
 (project/'logs'/'gui-results.json').write_text(json.dumps(results,indent=2,ensure_ascii=False),encoding='utf-8')
raise SystemExit(any(not r['passed'] for r in results))
