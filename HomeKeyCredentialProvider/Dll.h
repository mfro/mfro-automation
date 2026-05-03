#pragma once

#include "common.h"

extern HINSTANCE dllInstance;
#define HINST_THISDLL dllInstance

void DllAddRef();
void DllRelease();
