#!/bin/bash
# $1: phrase to find

if [ "$2" == "" ]; then
	grep_flags="-nwr"
else
	grep_flags="$2"
fi

grep "$1" "$grep_flags" --color=auto --include="*.c" --include="*.h" --include="*.s" --include="*.inc" --include="*.txt" --exclude-dir=".git" --exclude-dir=".travis" --exclude-dir="build" --exclude-dir="common_syms" --exclude-dir="data/layouts" --exclude-dir="data/tilesets/*" --exclude-dir="graphics" --exclude-dir="sound" --exclude-dir="tools"
