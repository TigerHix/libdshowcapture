import os
import platform
import sys
from ctypes import *

def resolve(name):
    f = os.path.join(os.path.dirname(__file__), name)
    return f

lib = None

def init():
    dll_path = resolve(os.path.join("vs", "2013", "x64", "Release", "dshowcapture.dll"))
    lib = cdll.LoadLibrary(dll_path)
    cam = 0
    width = 1280
    height = 720
    fps = 30
    if len(sys.argv) > 1:
        cam = sys.argv[1]
    if len(sys.argv) > 2:
        width = sys.argv[2]
    if len(sys.argv) > 3:
        height = sys.argv[3]
    if len(sys.argv) > 4:
        fps = sys.argv[4]
    lib.lib_test(int(cam), int(width), int(height), int(fps))

init()