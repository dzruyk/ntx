#!/usr/bin/python

import sys
import os

class Unbuffered:
    def __init__(self, stream):
        self.stream = stream
    def write(self, data):
        self.stream.write(data)
        self.stream.flush()
    def __getattr__(self, attr):
        return getattr(self.stream.attr)

while True:
    a = sys.stdin.readline()
    if a == "":
        break
    a = a.rstrip("\n").rstrip("\r")
    if len(a) % 2 != 0:
        sys.stderr.write("error: not odd len!")
        continue
    sys.stdout.write(a.decode("hex"))
    sys.stdout.flush()
