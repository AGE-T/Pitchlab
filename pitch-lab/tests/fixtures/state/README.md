# State fixtures (binary)

Byte-exact processor states in the historical formats, used by
`vst_processor_test` ("state fixtures"). Layout (little-endian):
`magic[4]` · `int32 count` · `count x (uint32 tag, float64 normalised)`.

| magic | era | kEngine normalisation | kPitch normalisation |
|---|---|---|---|
| PLV1 | before task 33 | index / 4 (5 engines) | (st + 12) / 24 |
| PLV2 | task 33 .. 2026-10 | index / 5 (6 engines) | (st + 48) / 96 |
| PLV3 | 2026-11 | index / 7 (8 engines) | (st + 48) / 96 |

Generated once by the script recorded in the commit that added them; the
expected values are listed in `cases.json`.
