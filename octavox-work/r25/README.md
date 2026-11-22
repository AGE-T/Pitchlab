# PitchLab R25 — DSP-lánc kutatási csomag

**Tárgy:** az eredeti Eventide Octavox VST3 belső DSP-láncának teljes statikus
feltárása · **Kör:** 25 (zárókör) · **Dátum:** 2026-11-22
**Célbináris:** `Ox.vst3` — PE32+ x86-64, 5 307 392 bájt,
SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`
*(a bináris maga IP-védelem miatt NEM része a csomagnak; sosem futtattuk,
nem módosítottuk)*

## Kezdje itt

| Fájl | Mit ad |
|---|---|
| `R25_OSSZEFOGLALO.md` | **Vezetői összefoglaló** — a teljes lánc 12 lépésben, kulcsszámokkal |
| `OCTAVOX_ORIGINAL_DSP_CHAIN.md` | **A teljes lánc-térkép** — 610 sor, 14 fejezet, utasításszintű bizonyítékokkal (OBS/DER/UNK osztályozás) |

## Bizonyítékanyag

| Könyvtár | Tartalom |
|---|---|
| `analysis_r25/` | Az R25 közvetlen elemzés (114 fájl): `fir_coefficients.md` (tracker FIR-lezárás), `pan_law.md` (pan-törvény), a teljes annotált disassembly (`r25_fir_dump.txt`, 6500 sor), gépi olvasható táblák (`r25_fir_tables.json`), és a step1–step14 szkriptek minden nyers kimenettel |
| `evidence_docs/core/` | 14 alapdokumentum: DSP-architektúra, core-internals, tracker mélyelemzés, vezérlésfolyamat-mátrix, paraméterszemantika, harmonia-mapper spec, minimum-pitch út, állapot/átmenet-elemzés, rekonstrukciós riport |
| `evidence_docs/rounds/` | A 6–22. körök findings-dokumentumai |
| `evidence_docs/deliveries/` | A 15–22. körök kézbesítési riportjai és a handover-jelentés |
| `evidence_docs/superseded/` | A korai 01–10-es dokumentsorozat (archív, részben felülírt olvasatokkal) |
| `evidence_docs/r23_yin/` | A 23. kör 16 YIN-kutatási dokumentuma |
| `evidence_docs/r24_simd/` | A 24. kör SIMD-architektúra- és benchmark-dokumentumai |
| `SHA256SUMS` | A csomag minden fájlának ellenőrző összege |

## Szigor

* A célbinárist a projekt teljes történetében **egyszer sem futtattuk, nem
  módosítottuk**; minden megállapítás kódból/bájtokból származik (Ghidra
  12.1.4 + capstone 5.0.7), utasításcímekkel visszakövethetően.
* Minden állítás **OBS** (megfigyelt) / **DER** (levezetett) / **UNK** (nem
  megállapított) osztályba tartozik; az ismert CONFLICT-eket a master
  dokumentum kezeli.
* A futásidejű hangzásbeli viselkedésekről a bináris nem futtatásából fakadóan
  nem teszünk állítást (l. a master dokumentum őszinte-hézag fejezetét).
