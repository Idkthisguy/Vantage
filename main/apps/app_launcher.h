#pragma once
#include "wm.h"

extern const app_descriptor_t app_launcher_app;

// Main launcher entry point to load and execute an ELF binary
bool app_launcher_exec_elf(const char *elf_path);