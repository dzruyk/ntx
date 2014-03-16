#!/bin/bash

sudo true
./write_hex.py |sudo nc -l -p 23 | xxd
