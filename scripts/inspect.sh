#!/bin/sh

cd "${MESON_BUILD_ROOT}" || exit

printf "\nrun checkpatch.pl\n"
checkpatch.pl --color=always -f --no-tree --strict "${MESON_SOURCE_ROOT}"/src/*

printf "\nrun cppcheck\n"
cppcheck "${MESON_SOURCE_ROOT}"/src/*

printf "\nrun clang-check\n"
clang-check -p "${MESON_BUILD_ROOT}/compile_commands.json" "${MESON_SOURCE_ROOT}"/src/*
