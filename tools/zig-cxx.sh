#!/usr/bin/env bash
exec "${HX_ZIG:?Set HX_ZIG}" c++ -target x86_64-windows-gnu -O2 "$@"
