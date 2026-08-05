#!/bin/bash
git submodule update --init --recursive
tools/emsdk/emsdk install latest
tools/emsdk/emsdk activate latest