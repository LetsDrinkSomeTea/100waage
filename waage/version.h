#pragma once
// Firmware-Version. tools/gen_version.sh erzeugt version_gen.h aus `git describe`
// (compile.sh ruft es auf, die CI im Schritt "Firmware-Version"). Ohne die
// Datei, z. B. beim Build in der Arduino-IDE, gilt "dev-<Datum>".
#if __has_include("version_gen.h")
#include "version_gen.h"
#endif
#ifndef FW_VERSION
#define FW_VERSION "dev-" __DATE__
#endif
