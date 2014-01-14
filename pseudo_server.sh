#!/bin/bash

sudo true
./write_hex.py |sudo nc -l 127.0.0.1 -p 23 | xxd
