// Pitch Lab CLI — implementation-phase introspection state.
//
// Architecture §B.1 entry points (compile|render|analyze|report|verify|
// listen-index + engines introspection) are v0.1 IMPLEMENTATION work. Only
// honest introspection exists so far: --version and the (empty) engine
// registry listing. Nothing pretends to render audio.

#include <cstdio>
#include <cstring>

#include "core/engine_registry.h"
#include "core/version.h"

namespace {

int cmdVersion() {
  std::printf("pitchlab %s (%s)\ncompiler: %s\nstatus: no engines implemented — "
              "registry content equals implemented engines\n",
              pitchlab::versionString(), pitchlab::phaseString(), pitchlab::compilerId());
  return 0;
}

int cmdEngines() {
  pitchlab::EngineRegistry registry;
  pitchlab::registerProductionEngines(registry);
  registry.seal();

  std::printf("pitchlab %s (%s) — %zu engine(s) registered\n",
              pitchlab::versionString(), pitchlab::phaseString(), registry.size());
  if (registry.size() == 0) {
    std::printf(
        "no engines implemented yet\n"
        "v0.1 planned (implementation specification §14): native.varispeed, "
        "native.vardelay, native.pv.classic, native.pv.phaselocked, native.granular\n");
    return 0;
  }
  for (std::size_t i = 0; i < registry.size(); ++i) {
    const pitchlab::EngineDescriptor& d = registry.at(i);
    std::printf("  %s  %s  reference=%s\n", d.info.id, d.info.version,
                d.isReferenceRole ? "yes" : "no");
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && (std::strcmp(argv[1], "--version") == 0 || std::strcmp(argv[1], "version") == 0)) {
    return cmdVersion();
  }
  if (argc == 2 && std::strcmp(argv[1], "engines") == 0) {
    return cmdEngines();
  }
  if (argc == 2 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "help") == 0)) {
    std::printf(
        "pitchlab %s (%s)\n"
        "implemented so far: --version, engines (registry introspection)\n"
        "  pitchlab --version   print version, phase, compiler\n"
        "  pitchlab engines     list registered engines (authoritative registry)\n",
        pitchlab::versionString(), pitchlab::phaseString());
    return 0;
  }
  std::fprintf(stderr,
               "pitchlab: '%s' is not implemented yet\n"
               "(v0.1 subcommands compile|render|analyze|report|verify|listen-index are "
               "implementation-phase work — see research/pitch-lab-v0.1-implementation-specification.md)\n",
               argc > 1 ? argv[1] : "");
  return 2;
}
