#pragma once

// Single include point for GLFW: pulls the Windows headers in first, through the
// one header that knows how this project includes them, and disables GLFW's own
// GL header so the GL loader (GLEW) owns the function declarations.

#include "platform/windows_api.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
