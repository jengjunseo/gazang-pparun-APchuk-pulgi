#!/usr/bin/env bash
exec "${HX_ZIG:?Set HX_ZIG}" cc -target x86_64-windows-gnu -O2 "$@"
