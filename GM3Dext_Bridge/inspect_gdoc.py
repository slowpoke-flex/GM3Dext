import ctypes
from ctypes import wintypes

kernel32 = ctypes.windll.kernel32

GENERIC_READ = 0x80000000
FILE_SHARE_READ = 1
FILE_SHARE_WRITE = 2
OPEN_EXISTING = 3
FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000
FILE_FLAG_BACKUP_SEMANTICS = 0x02000000
FSCTL_GET_REPARSE_POINT = 0x900a8

path = r"\\?\G:\Мой диск\NON_VERTEX_ENGINE_CONCEPT.md.gdoc"

h_file = kernel32.CreateFileW(
    path,
    GENERIC_READ,
    FILE_SHARE_READ | FILE_SHARE_WRITE,
    None,
    OPEN_EXISTING,
    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
    None
)

if h_file != -1:
    out_buf = ctypes.create_string_buffer(16384)
    bytes_returned = wintypes.DWORD()
    res = kernel32.DeviceIoControl(
        h_file,
        FSCTL_GET_REPARSE_POINT,
        None, 0,
        out_buf, 16384,
        ctypes.byref(bytes_returned),
        None
    )
    if res:
        print("DeviceIoControl success, bytes returned:", bytes_returned.value)
        data = out_buf.raw[:bytes_returned.value]
        print("Reparse Tag:", hex(int.from_bytes(data[:4], 'little')))
        # Print printable ASCII/UTF-8 strings found in reparse data
        import re
        strings = re.findall(rb'[\x20-\x7e]{4,}', data)
        print("Strings found in reparse data:")
        for s in strings:
            print("  ", s.decode('ascii', errors='ignore'))
    else:
        print("DeviceIoControl failed:", kernel32.GetLastError())
    kernel32.CloseHandle(h_file)
