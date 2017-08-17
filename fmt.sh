#!/bin/sh

clang-format-4.0 \
    -style=file \
    -i \
    sllq.c \
    sllq.h \
    sllqbench/sllqbench.c
