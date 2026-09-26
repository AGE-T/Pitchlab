#pragma once

// Pitch Lab — render manifest assembly (implementation specification §9
// naming / §4.8.1 item 7, frozen cycle 3).
//
// The manifest is a READ-ONLY MIRROR of registry + job + render accounting
// (provenance). It is NEVER authority (§4.6: "manifests never become
// authority") and never contains run-unique data (no timestamps — run-id =
// experiment id; §4.8.1 item 11): same (experiment, corpus, config,
// schedule, binary) ⇒ byte-identical manifest.

#include <string>

#include "core/engine_registry.h"
#include "harness/json_writer.h"
#include "harness/render_job.h"

namespace pitchlab {

/// Assemble the manifest value tree. Curve extrema are provided by the
/// caller (compiler-side min/max of the requested + effective signals);
/// hashes likewise (content hashing is the renderer's job — it owns the
/// bytes). `masterWritten`/`listeningWritten` tell whether the WAVs exist
/// (failed jobs write no WAV but still write a manifest).
[[nodiscard]] json::Value buildJobManifest(const RenderJob& job, const EngineDescriptor& descriptor,
                                           const JobResult& result,
                                           const std::string& inputWavSha256Hex,
                                           const std::string& requestedSignalSha256Hex,
                                           const std::string& effectiveSignalSha256Hex,
                                           double requestedMin, double requestedMax,
                                           double effectiveMin, double effectiveMax,
                                           bool masterWritten, bool listeningWritten);

/// Serialise + write (canonical JSON + trailing newline; parent directories
/// are created). Throws ConfigError on I/O failure.
void writeManifestFile(const std::filesystem::path& path, const json::Value& manifest);

}  // namespace pitchlab
