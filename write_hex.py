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
    a = sys.stdin.readline().rstrip("\n").rstrip("\r")
    sys.stdout.write(a.decode("hex"))
