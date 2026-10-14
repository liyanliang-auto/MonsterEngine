"""Real Vulkan helmet integration capture. No compositing or drawn chrome.

Run: python Tests/PBRHelmet/test_native_capture.py --output specs/<TS>/native-window
Requires a Windows desktop, Pillow and the existing Release build.
"""
import argparse
import ctypes
from ctypes import wintypes as W
import hashlib
import json
import os
import re
import subprocess
import time
from pathlib import Path
from PIL import Image, ImageStat, ImageGrab

ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / 'specs/20261013-194026-nvt4/build-vs2019/bin/Release/MonsterEngine.exe'
u, g = ctypes.windll.user32, ctypes.windll.gdi32
u.SetProcessDPIAware()
u.GetDC.argtypes = [W.HWND]; u.GetDC.restype = W.HDC
u.ReleaseDC.argtypes = [W.HWND, W.HDC]
u.GetWindowRect.argtypes = [W.HWND, ctypes.POINTER(W.RECT)]
u.GetClientRect.argtypes = [W.HWND, ctypes.POINTER(W.RECT)]
u.ClientToScreen.argtypes = [W.HWND, ctypes.POINTER(W.POINT)]
u.GetWindowThreadProcessId.argtypes = [W.HWND, ctypes.POINTER(W.DWORD)]
u.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, ctypes.c_int]
u.PrintWindow.argtypes = [W.HWND, W.HDC, W.UINT]
u.SendMessageTimeoutW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM, W.UINT, W.UINT, ctypes.POINTER(ctypes.c_size_t)]
u.SendMessageTimeoutW.restype = W.LPARAM
u.GetPropW.argtypes = [W.HWND, W.LPCWSTR]; u.GetPropW.restype = ctypes.c_void_p
u.GetWindowLongPtrW.argtypes = [W.HWND, ctypes.c_int]; u.GetWindowLongPtrW.restype = ctypes.c_void_p
u.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
for name in ('CreateCompatibleDC', 'CreateCompatibleBitmap', 'SelectObject'):
    getattr(g, name).restype = W.HANDLE
g.CreateCompatibleDC.argtypes = [W.HDC]
g.CreateCompatibleBitmap.argtypes = [W.HDC, ctypes.c_int, ctypes.c_int]
g.SelectObject.argtypes = [W.HDC, W.HANDLE]
g.GetDIBits.argtypes = [W.HDC, W.HANDLE, W.UINT, W.UINT, ctypes.c_void_p, ctypes.c_void_p, W.UINT]
g.DeleteObject.argtypes = [W.HANDLE]; g.DeleteDC.argtypes = [W.HDC]

class BI(ctypes.Structure):
    _fields_ = [('size', W.DWORD), ('width', W.LONG), ('height', W.LONG), ('planes', W.WORD), ('bits', W.WORD), ('compression', W.DWORD), ('imageSize', W.DWORD), ('xp', W.LONG), ('yp', W.LONG), ('used', W.DWORD), ('important', W.DWORD)]

def title(hwnd):
    text = ctypes.create_unicode_buffer(512)
    u.GetWindowTextW(hwnd, text, len(text))
    return text.value

def capture(hwnd, path):
    path.with_suffix('.window-proc.json').write_text(json.dumps(dict(hwnd=int(hwnd), current_proc=u.GetWindowLongPtrW(hwnd, -4), owner=u.GetPropW(hwnd, 'MonsterEngine.PBR.PrintOwner'), original_proc=u.GetPropW(hwnd, 'MonsterEngine.PBR.PrintProc'))))
    rect, client, point = W.RECT(), W.RECT(), W.POINT(0, 0)
    assert u.GetWindowRect(hwnd, ctypes.byref(rect))
    assert u.GetClientRect(hwnd, ctypes.byref(client))
    assert u.ClientToScreen(hwnd, ctypes.byref(point))
    width, height = rect.right - rect.left, rect.bottom - rect.top
    assert width > client.right and height > client.bottom, 'Native window chrome absent'
    hdc = u.GetDC(hwnd); dc = g.CreateCompatibleDC(hdc)
    bmp = g.CreateCompatibleBitmap(hdc, width, height); old = g.SelectObject(dc, bmp)
    try:
        printed = u.PrintWindow(hwnd, dc, 0)
        assert printed, 'PrintWindow failed'
        info = BI(ctypes.sizeof(BI), width, -height, 1, 32, 0, width * height * 4, 0, 0, 0, 0)
        bits = ctypes.create_string_buffer(width * height * 4)
        lines = g.GetDIBits(dc, bmp, 0, height, bits, ctypes.byref(info), 0)
        assert lines == height, 'Incomplete native bitmap'
        frame = Image.frombuffer('RGB', (width, height), bits.raw, 'raw', 'BGRX', 0, 1)
        x, y = point.x - rect.left, point.y - rect.top
        content = frame.crop((x, y, x + client.right, y + client.bottom))
        method = 'Raw Win32 PrintWindow(hwnd, HDC, 0), entire GetWindowRect'
        if max(ImageStat.Stat(content).stddev) <= 15:
            # Some inaccessible desktops bypass application WM_PRINT in PrintWindow.
            # Ask the actual native procedure to print the entire window itself.
            message_result = ctypes.c_size_t()
            assert u.SendMessageTimeoutW(hwnd, 0x0317, dc, 0x000E, 3, 5000, ctypes.byref(message_result)), 'Native WM_PRINT timed out'
            lines = g.GetDIBits(dc, bmp, 0, height, bits, ctypes.byref(info), 0)
            assert lines == height, 'Incomplete WM_PRINT bitmap'
            frame = Image.frombuffer('RGB', (width, height), bits.raw, 'raw', 'BGRX', 0, 1)
            method = 'Raw native SendMessageTimeout(WM_PRINT, PRF_NONCLIENT|PRF_CLIENT|PRF_ERASEBKGND), entire GetWindowRect'
        frame.save(path)
        try:
            ImageGrab.grab(bbox=(rect.left, rect.top, rect.right, rect.bottom), all_screens=True).save(path.with_name(path.stem + '-desktop.png'))
        except OSError as error:
            path.with_suffix('.desktop-error.txt').write_text(str(error))
        content = frame.crop((x, y, x + client.right, y + client.bottom))
        # Reject blank/black GPU captures without constraining artistic lighting.
        assert max(ImageStat.Stat(content).stddev) > 15, 'GPU content blank'
        center = content.crop((client.right // 4, client.bottom // 4, 3 * client.right // 4, 3 * client.bottom // 4)).convert('L')
        stats = ImageStat.Stat(center)
        assert stats.mean[0] > 25 and stats.stddev[0] > 15, 'Helmet region unreadable'
        return dict(size=frame.size, client_offset=[x, y], client_size=[client.right, client.bottom], print_success=printed, bitmap_lines=lines, center_luma_mean=stats.mean[0], center_luma_stddev=stats.stddev[0], sha256=hashlib.sha256(path.read_bytes()).hexdigest(), method=method + '; actual native GPU client and OS chrome; no external compositing')
    finally:
        g.SelectObject(dc, old); g.DeleteObject(bmp); g.DeleteDC(dc); u.ReleaseDC(hwnd, hdc)

def run(out, alias):
    name = alias.lstrip('-')
    hwnds = []
    CB = ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    with (out / (name + '-runtime.log')).open('w') as log:
        env = os.environ.copy(); env['MONSTER_SPLAT_DIAG'] = '1'
        p = subprocess.Popen([str(EXE), alias], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
        @CB
        def cb(hwnd, _):
            pid = W.DWORD(); u.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value == p.pid and u.IsWindowVisible(hwnd): hwnds.append(hwnd)
            return True
        hwnd = None
        try:
            deadline = time.monotonic() + 45
            while time.monotonic() < deadline and p.poll() is None:
                hwnds.clear(); u.EnumWindows(cb, 0)
                if hwnds:
                    hwnd = hwnds[0]
                    text = title(hwnd)
                    match = re.search(r'FPS\s*[:=]\s*([\d.]+)', text)
                    if match and float(match.group(1)) > 0 and 'Vulkan' in text and 'renderPBR' in text:
                        time.sleep(2)
                        break
                time.sleep(.1)
            else:
                raise AssertionError('No stabilized native helmet HWND with measured FPS')
            text = title(hwnd)
            assert float(re.search(r'FPS\s*[:=]\s*([\d.]+)', text).group(1)) > 0
            evidence = capture(hwnd, out / (name + '-native.png'))
            time.sleep(.5)
            capture(hwnd, out / (name + '-repeat.png'))
            u.PostMessageW(hwnd, 0x0010, 0, 0)
            code = p.wait(timeout=90)
            assert code == 0, 'Actual render process failed: ' + str(code)
            evidence.update(title=text, exit_code=code, command=[str(EXE), alias])
            (out / (name + '-capture.json')).write_text(json.dumps(evidence, indent=2))
        finally:
            if p.poll() is None:
                if hwnd: u.PostMessageW(hwnd, 0x0010, 0, 0)
                try: p.wait(timeout=10)
                except subprocess.TimeoutExpired: p.kill(); p.wait()
    runtime = (out / (name + '-runtime.log')).read_text(errors='replace')
    assert 'PBR helmet rendering initialized successfully' in runtime, 'Assets did not initialize'
    assert not re.search(r'Validation Error|VUID-', runtime), 'Vulkan validation issue'
    print(name + ': PASS native capture, FPS, texture initialization, clean exit')

def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--output', required=True)
    args = parser.parse_args(); out = Path(args.output); out.mkdir(parents=True, exist_ok=True)
    for value in ('0', '-1', '1000001', 'invalid'):
        result = subprocess.run([str(EXE), '--renderPBR', '--cube-frames', value], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        (out / ('invalid-' + value.replace('-', 'minus') + '.log')).write_bytes(result.stdout)
        assert result.returncode == 2, 'Invalid frame limit accepted: ' + value
    print('Invalid frame limits: PASS 4/4')
    for alias in ('--renderPBR', '--pbr-scene'): run(out, alias)

if __name__ == '__main__': main()
