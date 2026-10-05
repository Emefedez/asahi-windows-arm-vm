import ctypes, sys, platform
libc = ctypes.CDLL(None, use_errno=True)
PR_SET_MEM_MODEL, PR_GET_MEM_MODEL, TSO = 0x4d4d444c, 0x6d4d444c, 1
r = libc.prctl(PR_SET_MEM_MODEL, TSO, 0, 0, 0)
e = ctypes.get_errno()
g = libc.prctl(PR_GET_MEM_MODEL, 0, 0, 0, 0)
print(f"{sys.argv[1]}: kernel {platform.release()} set_tso={r} errno={e} get={g}")
