//
//  kern_tgl_decode.hpp
//  WhateverXe
//
//  Hardware video decode withdrawal (-wexnohwdecode).
//

#pragma once
#include <string.h>

// Hardware decode can hang RCS in a media kernel that never retires (App
// Store previews through VTDecoderXPCService). AppleGVA loads one accelerator
// list for decode and encode alike, so no registry key withdraws decode alone.
// Refuse video contexts in the process that hosts hardware decoders instead;
// VideoToolbox then decodes in software and VTEncoderXPCService keeps the GPU.
namespace TGLDecode {

// The kernel keeps 16 characters of the command name: "VTDecoderXPCServ".
constexpr const char *DecoderProcess = "VTDecoderXPCService";
constexpr size_t CommandName = 16;

inline bool refused(const char *name) {
	return name && strlen(name) >= CommandName && !strncmp(name, DecoderProcess, CommandName);
}

} // namespace TGLDecode
