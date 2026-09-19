#pragma once
#include <cstddef>
struct EmbeddedShader { const char* name; const unsigned char* data; size_t size; };
const EmbeddedShader* embedded_shader(const char* name);
