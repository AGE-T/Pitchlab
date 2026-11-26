# R25 csomag sértetlenség-ellenőrzés (R26 — 1. fejezet)

**Projekt:** PitchLab/Octavox clean-room reverse engineering · **Kör:** R26 (statikus audit)
**Ellenőrző ügynök:** Task ID: 1 · **Dátum:** 2026-11-22 (UTC) · **Branch:** `r26-static-audit` (nem módosítva)
**Módszer:** kizárólag statikus fájlrendszeri olvasás — a célbinárist (`Ox.vst3`, PE32+ DLL) **egyszer sem futtattuk, nem is módosítottuk**; a ZIP-et csak olvasható módban, ideiglenes munkakönyvtárba (`/tmp/r25verify/`) csomagoltuk ki.

---

## Cél és módszer

A `public/PitchLab_R25_DSP_Chain_Research_Package.zip` kutatási csomag (az R25 zárókör terméke) sértetlenségének független ellenőrzése, mielőtt annak tartalmát az R26 audit további fejezetei hivatkozási alapként használnák. Az ellenőrzés lefedte:

1. a csomag és a célbináris külső SHA-256-értékeit (munka előtt és után),
2. a csomag belső `SHA256SUMS` manifestjének teljes újraszámolását,
3. a csomag README-je és az R25 összefoglalója által dokumentált fájllisták és a tényleges archívumtartalom kereszthasonlatát,
4. a kulcs nyers evidenciafájlok jelenlétét és alaki épségét (sorszám, JSON-érvényesség),
5. a láncdokumentum (`OCTAVOX_ORIGINAL_DSP_CHAIN.md`) `analysis_r25/`-re mutató hivatkozásainak feloldását,
6. a repó `octavox-work/analysis_r25/` fája és az archívum `analysis_r25/` fája közti byte-szintű összehasonlítást (`diff -rq`).

Osztályozás a projekt konvenciója szerint: **OBS** (megfigyelt tény, közvetlen parancskimenet), **DER** (levezetett következtetés), **UNK** (nem eldönthető), **CONFLICT** (elvárt és tényleges érték ütközik). Minden állítás forrásparancs vagy fájlútvonal jelöléssel.

---

## Ellenőrzött elemek

### 1. táblázat — Külső hash- és méretellenőrzés (parancs: `sha256sum …`, `stat -c '%n | %s bytes' …` a repo gyökeréből)

| Elem | Elvárt | Tényleges (OBS) | Verdikt |
|---|---|---|---|
| `upload/Ox.vst3` SHA-256 | `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2` | `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2` | **PASS** |
| `upload/Ox.vst3` méret | 5 307 392 bájt | 5 307 392 bájt | **PASS** |
| `octavox-work/original/Ox.vst3` SHA-256 | a fenti értékkel azonos | `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2` (azonos az `upload/` másolattal) | **PASS** |
| `octavox-work/original/Ox.vst3` méret | 5 307 392 bájt | 5 307 392 bájt | **PASS** |
| `public/PitchLab_R25_DSP_Chain_Research_Package.zip` SHA-256 | `46e891c7145461ae7d1baebfa6ee81e70ad277d0db5a17e4bf463f9842962244` | `46e891c7145461ae7d1baebfa6ee81e70ad277d0db5a17e4bf463f9842962244` | **PASS** |
| ZIP méret | 1 253 135 bájt | 1 253 135 bájt | **PASS** |
| `public/…zip.sha256` tartalma vs. tényleges ZIP-hash | a fájlban szereplő hash egyezik a ZIP tényleges hash-ével | a társmutató fájl tartalma: `46e891c7145461ae7d1baebfa6ee81e70ad277d0db5a17e4bf463f9842962244  PitchLab_R25_DSP_Chain_Research_Package.zip` → egyezik | **PASS** |
| Bináris-hash a csomagon belül (`analysis_r25/r25_fir_tables.json` „sha256” mező) | `7bb260f6…32f1f2` | `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`; `target` mező: `/home/z/my-project/octavox-work/original/Ox.vst3` | **PASS** |

**DER:** a célbináris mindkét repóbeli másolata byte-azonos, és a csomag belső gépi olvasható táblája is ugyanezt a hashet rögzíti — a csomag provenanciája konzisztens a repóval. A `r25_fir_dump.txt` és a `fir_coefficients.md` / `pan_law.md` fejlécei szintén ezt a SHA-256-öt hirdetik „(verified before + after)” jelöléssel (OBS, fájlok 1–5. sora).

### 2. táblázat — Kicsomagolás és ZIP-integritás (parancs: `python3 -c "import zipfile; …"` → `testzip()`, `extractall`, majd `os.walk` összegzés)

| Elem | Elvárt | Tényleges (OBS) | Verdikt |
|---|---|---|---|
| Kicsomagolt fájlok száma | ~190 | **190** (a ZIP 199 bejegyzéseből 190 fájl + 9 könyvtárbejegyzés) | **PASS** |
| Kicsomagolt összméret | — | 3 777 897 bájt | OBS |
| ZIP CRC-sérintetlenség (`zipfile.testzip()`) | hibátlan | `None` (nincs hibás CRC) | **PASS** |
| ZIP-gyökér | egyetlen csomagkönyvtár | minden bejegyzés a `PitchLab_R25_DSP_Chain_Research_Package/` prefix alatt; nincs elkóborló top-level bejegyzés | **PASS** |

### 3. táblázat — Belső `SHA256SUMS` manifest (fájl: csomaggyökér `SHA256SUMS`; parancs: `sha256sum -c SHA256SUMS` a kicsomagolt csomaggyökérből; kimenet: `/tmp/r25verify/manifest_check.txt`)

| Elem | Elvárt | Tényleges (OBS) | Verdikt |
|---|---|---|---|
| Manifest bejegyzésszám | a csomag minden fájlára kiterjed | **189 bejegyzés** (a 190 fájlból a `SHA256SUMS` saját magát nem tartalmazza — bevett gyakorlat) | **PASS** (DER: megfelelő lefedettség) |
| Ellenőrzött bejegyzések | mind | **189/189 OK** | **PASS** |
| Hibás (FAIL) bejegyzés | 0 | **0** — kimenetben nem szerepel hibás sor; `sha256sum -c` kilépési kód: **0** | **PASS** |

Idézet a kimenet utolsó soraiból (`/tmp/r25verify/manifest_check.txt`):
```
./evidence_docs/superseded/08_control_surface_and_parameter_wiring.md: OK
./evidence_docs/superseded/09_evidence_matrix.md: OK
./evidence_docs/superseded/10_open_questions_and_implications.md: OK
```

### 4. táblázat — Dokumentált vs. tényleges tartalom (források: a ZIP-en belüli `README.md`, a repó `octavox-work/r25/README.md`, valamint `octavox-work/r25/R25_OSSZEFOGLALO.md` §7 „Mit tartalmaz a csomag”)

Előzetes megállapítás (OBS): a ZIP-en belüli `README.md` és a repó `octavox-work/r25/README.md` **byte-szinten azonos** (`diff` üres kimenet); a `OCTAVOX_ORIGINAL_DSP_CHAIN.md` és az `R25_OSSZEFOGLALO.md` szintén byte-azonos a repóbeli változatukkal.

| Dokumentált állítás (hely) | Elvárt/dokumentált | Tényleges (OBS) | Verdikt |
|---|---|---|---|
| `R25_OSSZEFOGLALO.md` jelenléte | a csomaggyökéren | jelen; 246 sor; byte-azonos a repóval | **PASS** |
| `OCTAVOX_ORIGINAL_DSP_CHAIN.md` „610 sor” (README + összefoglaló §7) | 610 sor | **609 sor** (`wc -l`; fájl 34 893 bájt, utolsó bájt `\n`, `splitlines`=609) | **CONFLICT (kisebbfokú)** — l. Eltérések |
| `analysis_r25/` „114 fájl” (README) | 114 | **114** | **PASS** |
| `r25_fir_dump.txt` „6500 sor” (README, összefoglaló) | ~6500 | **6525 sor** / 1 697 827 bájt | **PASS** (~ tolerancián belül) |
| `fir_coefficients.md`, `pan_law.md` jelenléte | mindkettő | mindkettő jelen (268, ill. 221 sor) | **PASS** |
| `r25_fir_tables.json` gépi olvasható tábla | érvényes JSON | érvényes JSON (`json.load` OK), 13 703 bájt, kulcsok: `target, sha256, image_base, mechanism, dispatch, thresholds, classes, ctor_tracker_init, ctor_window_class, analysis` | **PASS** |
| „step1–step14 szkriptek minden nyers kimenettel” (README) | step1–14 szkriptek | a `step*` szkriptek száma **27 db `.py`** (sorszámuk step1-től **step20-ig** terjed), a nyers kimenetek step1-től **step22d-ig** | **CONFLICT (kisebbfokú)** — a README alulhányol; l. Eltérések |
| `evidence_docs/core/` „14 alapdokumentum” (README + összefoglaló) | 14 | **16 fájl** | **CONFLICT (kisebbfokú)** — l. Eltérések |
| `evidence_docs/rounds/` „a 6–22. körök findings-dokumentumai” | körök 6–22 | 12 fájl: ROUND6 (×2: DSP + MEASUREMENTS), 9, 10, 11, 12, 15–20 | **PASS** (13. és 14., 21–22. körre külön rounds-fájl nincs — a 21–22. anyag a `deliveries/` PITCHLAB_ROUND21/22_FINDINGS.md-ben; DER) |
| `evidence_docs/deliveries/` „a 15–22. körök kézbesítési riportjai + handover” | 15–22 + handover | 12 fájl: DELIVERY_ROUND15–22 (8) + OCTAVOX_AGENT_HANDOVER_REPORT.md + PITCHLAB_ROUND21_FINDINGS.md + PITCHLAB_ROUND22_FINDINGS.md + README.md | **PASS** (3 db fájl a leírtakon felül: a két PITCHLAB findings és a gyári README — l. Eltérések) |
| `evidence_docs/superseded/` „a korai 01–10-es dokumentsorozat” | 01–10 | **10 fájl** (01…10) | **PASS** |
| `evidence_docs/r23_yin/` „16 YIN-kutatási dokumentum” | 16 | **16 fájl** | **PASS** |
| `evidence_docs/r24_simd/` „SIMD-architektúra- és benchmark-dokumentumok” | nincs darabszám megadva | **6 fájl** | **PASS** |
| `SHA256SUMS` jelenléte | csomaggyökér | jelen (19 999 bájt) | **PASS** |

**Dokumentált-de-hiányzó fájlok: 0 db.** **Jelen-de-nem-szám szerint dokumentált fájlok:** `evidence_docs/core/` +2 db (16 vs. 14), `evidence_docs/deliveries/` +3 db a kifejezetten felsoroltakon (8 delivery + handover) felül. Minden, a README-ben **név szerint** megjelölt fájl (`R25_OSSZEFOGLALO.md`, `OCTAVOX_ORIGINAL_DSP_CHAIN.md`, `fir_coefficients.md`, `pan_law.md`, `r25_fir_dump.txt`, `r25_fir_tables.json`, `SHA256SUMS`) megtalálható az archívumban — hiányzó név szerinti fájl nincs (OBS).

### 5. táblázat — Kulcs nyers evidenciafájlok szanitása (parancsok: `wc -l`, `stat`, `json.load`, fájlfejléc-olvasás)

| Fájl (archív `analysis_r25/`) | Tényleges (OBS) | Verdikt |
|---|---|---|
| `r25_fir_dump.txt` | 6525 sor, 1 697 827 bájt; fejléc: „PITCHLAB R25 -- tracker decimation FIR coefficient dump (STATIC, read-only)”, target-útvonal + SHA-256 + image base `0x180000000` + szekciótábla | **PASS** |
| `fir_coefficients.md` | 268 sor, 18 415 bájt; provenancia-blokk a célbináris SHA-256-jével, „read and disassembled only; never executed, never modified” | **PASS** |
| `pan_law.md` | 221 sor, 19 547 bájt; azonos provenancia-blokk | **PASS** |
| `r25_fir_tables.json` | 13 703 bájt; érvényes JSON; `sha256` = célbináris hash (egyezik); `image_base` = 6442450944 = `0x180000000` | **PASS** |
| `fun_*.txt` disassembly-dumpok | **16 db**, mindegyik nemüres; sorszámok 53–719 (pl. `fun_d8380_full.txt` 719 sor / 30 980 bájt) | **PASS** |
| `step*.py` szkriptek | **27 db** nemüres szkript (390–3 637 bájt); továbbá `r25_extract_tables.py` (10 235 bájt), `r25_fir_dump.py` (13 143 bájt), `r25lib.py` (4 236 bájt) — összesen 30 Python-fájl | **PASS** |

---

## A csomag belső struktúrája

Az archívum egyetlen gyökérkönyvtárral (`PitchLab_R25_DSP_Chain_Research_Package/`) bontható ki; 199 ZIP-bejegyzés = 9 könyvtár + **190 fájl** (parancs: `os.walk` alapú összegzés a kicsomagolt gyökérből).

| Szint | Bejegyzés | Fájlszám | Megjegyzés (OBS) |
|---|---|---|---|
| gyökér | `OCTAVOX_ORIGINAL_DSP_CHAIN.md` | 1 | 34 893 bájt, 609 sor — a lánc-térkép (§0–§14: 15 `##` szakasz = §0 egyoldalas áttekintés + 14 számozott fejezet) |
| gyökér | `R25_OSSZEFOGLALO.md` | 1 | 14 978 bájt, 246 sor — vezetői összefoglaló; §7 = csomagtartalom-jegyzék |
| gyökér | `README.md` | 1 | 2 453 bájt — csomag-index; byte-azonos a repó `octavox-work/r25/README.md`-jével |
| gyökér | `SHA256SUMS` | 1 | 19 999 bájt, 189 bejegyzés |
| 1. szint | `analysis_r25/` | **114** | sík könyvtár (alkönyvtár nélkül) — az R25 közvetlen elemzés: 16 `fun_*.txt` dump, 30 `.py` szkript, `r25_fir_dump.txt`, `r25_fir_tables.json`, `fir_coefficients.md`, `pan_law.md`, step-kimenetek |
| 1. szint | `evidence_docs/` | 0 laza fájl | 6 alkönyvtár |
| 2. szint | `evidence_docs/core/` | **16** | R20–R22 + OCTAVOX_* alapdokumentumok + `EVIDENCE_HASHES.md` |
| 2. szint | `evidence_docs/rounds/` | **12** | ROUND6–ROUND20 findings |
| 2. szint | `evidence_docs/deliveries/` | **12** | DELIVERY_ROUND15–22 + handover + PITCHLAB ROUND21/22 findings + README |
| 2. szint | `evidence_docs/superseded/` | **10** | korai 01–10-es dokumentsorozat (a feladatleírás „deprecated” nevű alkönyvtárának szerepét ez tölti be) |
| 2. szint | `evidence_docs/r23_yin/` | **16** | R23 YIN-kutatási dokumentumok |
| 2. szint | `evidence_docs/r24_simd/` | **6** | R24 SIMD-architektúra/benchmark/VST3-készenlét |
| — | **Összesen** | **190** | 4 gyökéri + 114 + 72 = 190 ✓ (elvárt ~190) |

A feladatban kötelezően ellenőzendő elemek mindegyike jelen: R25 összefoglaló ✓, láncdokumentum ✓, `analysis_r25/` ✓, `evidence_docs/` (core/rounds/deliveries/superseded/r23_yin/r24_simd) ✓, `SHA256SUMS` ✓ (OBS).

---

## Chain-dokumentum hivatkozás-ellenőrzés

Forrás: `octavox-work/r25/OCTAVOX_ORIGINAL_DSP_CHAIN.md` (609 sor). Keresés: `rg -n 'analysis_r25'` (találat: 1 sor), majd teljes szöveges mintavadászatok — `analysis[_ ]r25`, `octavox-work/…`, valamint minden `` `…\.(txt|py|json|md)` `` alakú hivatkozás (python `re`). **OBS:** a láncdokumentum az `analysis_r25/`-be pontosan **3 hivatkozással** mutat (1 könyvtárszintű + 2 konkrét fájlnév), valamint tartalmaz **1 önhivatkozást**; további `.txt/.py/.json` fájlhivatkozást a dokumentum nem tartalmaz (a `FUN_180…` alakú jelölések Ghidra függvénycímkék, nem fájlnevek — DER).

| Hivatkozás a láncdokumentumban (sor) | Archívumban? (kicsomagolt `analysis_r25/`) | Repóban? (`octavox-work/analysis_r25/`) | Verdikt |
|---|---|---|---|
| `octavox-work/analysis_r25/` (s607, könyvtárhivatkozás) | IGEN — csomaggyökér `analysis_r25/` (114 fájl) | IGEN — a repó `octavox-work/analysis_r25/` (114 fájl) | **PASS** |
| `fir_coefficients.md` (s608, az analysis_r25 kontextusában) | IGEN | IGEN | **PASS** |
| `pan_law.md` (s608, ugyanott) | IGEN | IGEN | **PASS** |
| `octavox-work/r25/OCTAVOX_ORIGINAL_DSP_CHAIN.md` (s609, önhivatkozás) | IGEN — csomaggyökéri `OCTAVOX_ORIGINAL_DSP_CHAIN.md` | IGEN | **PASS** |

**Számítás (OBS): 4/4 hivatkozás feloldva (mind PASS); 0 feloldatlan (dangling) hivatkozás.** A s608 „+ a nyers szkriptek/kimenetek” gyűjtőmegnevezés név szerint nem hivatkozik egyedi fájlra — ezt az R26 későbbi fejezetei a `SHA256SUMS`-on és a tényleges fájllistán át ellenőrizhetik (DER).

---

## Repo vs. archívum `analysis_r25` (task 7)

Parancs (a repó gyökeréből):

```
diff -rq octavox-work/analysis_r25 \
        /tmp/r25verify/unpack/PitchLab_R25_DSP_Chain_Research_Package/analysis_r25
```

Eredmény (OBS): **kilépési kód 0, kimenet üres** (a teljes kimenet archíválva: `/tmp/r25verify/diff_rq.txt`, 0 sor).

| Elem | Tényleges (OBS) | Verdikt |
|---|---|---|
| Repó `octavox-work/analysis_r25/` fájlszáma | 114 | — |
| Archív `analysis_r25/` fájlszáma | 114 | — |
| Eltérő (tartalmilag különböző) fájl | **0 db** | **PASS** |
| Csak az egyik oldalon lévő fájl („Only in …”) | **0 db** | **PASS** |
| Összegzés | a két fa **byte-szinten azonos** (minden fájl párja megegyezik) | **PASS** |

**DER:** az R25 elemzési anyag a csomagba változatlan, elveszett/átírt fájl nélkül került át — a csomag és a repó munkaállapota az `analysis_r25/` referenciára nézve szinkronban van.

---

## Eltérések és következtetések

**Kritikus integritási eltérés: nincs.** Minden hash, minden manifest-bejegyzés és a két `analysis_r25`-fa byte-azonos. Az alábbiak **kisebbfokú, dokumentációs önéleírás-eltérések** (a csomag saját README/összefoglaló szövegének pontatlanságai), amelyeket nem javítottunk — a feladat előírása szerint változatlanul jelentjük:

1. **[CONFLICT — kisebbfokú]** A README és az `R25_OSSZEFOGLALO.md` §7 a láncdokumentumot „**610 sor**”-nak írja; a tényleges fájlszám **609 sor** (`wc -l`; a fájl utolsó bájtja `\n`, tehát a 609 érték nem trailing-newline-móduszából fakadó mérési műszer). Eltérés: +1 sor a dokumentáció részéről (off-by-one).
2. **[CONFLICT — kisebbfokú]** A README/összefoglaló az `evidence_docs/core/`-ot „**14 alapdokumentum**”-ként írja; a tényleges fájlszám **16**. A 2 db többletfájl névszerint nem azonosítható be a szövegből (a README nem sorolja fel egyenként a fájlokat); a legvalószínűbb magyarázat, hogy a gyári `EVIDENCE_HASHES.md` és pl. az `OCTAVOX_DSP_DEMO_USAGE.md` nem „alapdokumentum”-ként volt számlálva — ez azonban **UNK** (nem eldönthető spekuláció). A megfigyelt tény: 16 ≠ 14.
3. **[CONFLICT — kisebbfokú]** A README „a **step1–step14** szkriptek”-ről beszél, miközben az archívumban a `step*.py` szkriptek sorszáma **step20-ig**, a nyers kimenetek **step22d-ig** terjednek (27 szkript + ~50 kimenetfájl). Az `R25_OSSZEFOGLALO.md` §7 ezzel szemben pontosan fogalmaz („+ az összes stepN szkript/nyers kimenet”). A README tehát **alulhányolja** a tényleges anyagot — hiányzó fájl nincs, többlet van.
4. **[OBS]** Az `evidence_docs/deliveries/` 12 fájlból 3 (PITCHLAB_ROUND21_FINDINGS.md, PITCHLAB_ROUND22_FINDINGS.md, README.md) nincs a README szövegében kifejezetten megnevezve (a „findings” anyag máshol, a `rounds/` leírás tárgyköréhez tartozik) — ismét leírási pontatlanság, nem hiány.
5. **[OBS]** A `r25_fir_dump.txt` „6500 soros” megjelöléshez képest a tényleges 6525 sor — a „~6500” elváráson belül, gyakorlatilag pontos stílusú kerekítés.

**DER következtetések:**

* A csomag **belső és külső provenanciája konzisztens**: a célbináris SHA-256-je egyformán szerepel a ZIP társmutatójában közvetetten (ZIP-hash), a csomag README-jében, a láncdokumentum §14-ben, a `r25_fir_tables.json`-ben és a nyers dumpok fejlécében — és mindegyik egyezik a repó két binárismásolatának tényleges hashével.
* A `SHA256SUMS` 189/189-as újraszámolása és a ZIP `testzip()`-eredménye együtt azt jelzi, hogy a csomag **sérülés nélkül, változatlanul** került a `public/` könyvtárba, és a repóbeli munkapéldány (legalábbis az `analysis_r25/` anyagra) naprakész.
* A dokumentációs off-by-one/off-by-two eltérések nem érintik a bizonyítékanyag tartalmi épségét — javaslat (nem végrehajtva): az R25 csomag következő kiadásában a README két darabszámát („610 sor”, „14 alapdokumentum”) és a „step1–step14” tartományt a tényleges értékekre (609, 16, step1–step22) igazítani.

---

## Verdikt

### Összverdikt: **PASS — a csomag épsége igazolva** (két kisebbfokú, dokumentációs CONFLICT kísérőjellel, amelyek nem érintik a tartalmi integritást)

| # | Tétel | Verdikt |
|---|---|---|
| 1 | `upload/Ox.vst3` hash + méret (előtte és utána azonos) | **PASS** |
| 2 | `octavox-work/original/Ox.vst3` hash + méret; a két másolat azonossága | **PASS** |
| 3 | ZIP SHA-256 + méret | **PASS** |
| 4 | `.zip.sha256` társmutató tartalmának egyezése | **PASS** |
| 5 | Kicsomagolás: 190 fájl, egygyökerű struktúra, CRC OK | **PASS** |
| 6 | `SHA256SUMS`: 189/189 OK, 0 FAIL, exit 0 | **PASS** |
| 7 | README-dokumentált elemek jelenléte (hiányzó: 0) | **PASS** (darabszám-jellegű önéleírás-eltérések l. fent) |
| 8 | Nyers evidenciafájlok szanitása (dumpok, szkriptek, JSON) | **PASS** |
| 9 | Láncdokumentum-hivatkozások `analysis_r25`-re: 4/4 feloldva | **PASS** |
| 10 | Repo vs. archív `analysis_r25`: byte-azonos, 114/114 fájl | **PASS** |
| 11 | A láncdokumentum sorszáma vs. README-állítás (609 vs. 610) | **CONFLICT (kisebbfokú, dokumentációs)** |
| 12 | `core/` fájlszám vs. README-állítás (16 vs. 14) | **CONFLICT (kisebbfokú, dokumentációs)** |
| 13 | README „step1–step14” vs. tényleges step1–step22 anyag | **CONFLICT (kisebbfokú, dokumentációs)** |

**A R25 csomag és a mögötte álló bizonyítékanyag az R26 audit további fejezeteihez megbízható hivatkozási alapként használható.** A célbináris a teljes ellenőrzés során egyszer sem került végrehajtásra (statikus olvasás sem történt meg a binárisból e fejezetben — csak a hashét számoltuk újra).

---

## Provenancia

* **Ügynök / feladat:** R26 — 1. fejezet: bizonyítékanyag- és dokumentációintegritás-ellenőrzés; **Task ID: 1**; generikus ellenőrző ügynök a `r26-static-audit` branchen (git-művelet nem történt).
* **Dátum:** 2026-11-22 (UTC), 16:21-kor lezárva; az ellenőrzés alatt a `date -u` szerint 2026-11-22.
* **Munkakönyvtárak:** kicsomagolás — `/tmp/r25verify/unpack/PitchLab_R25_DSP_Chain_Research_Package/`; segédkimenetek — `/tmp/r25verify/manifest_check.txt`, `/tmp/r25verify/diff_rq.txt`; jelentés — `/home/z/my-project/octavox-work/r26/verification/R25_PACKAGE_VERIFICATION.md` (új fájl; ez volt az egyetlen repóbeli írás).
* **Védett elemek (READ-ONLY):** `upload/Ox.vst3`, `octavox-work/original/Ox.vst3`, `public/PitchLab_R25_DSP_Chain_Research_Package.zip`, `public/PitchLab_R25_DSP_Chain_Research_Package.zip.sha256` — egyik sem módosult (a kezdő és záró hash azonos, l. alább).

### Kezdő hash-ellenőrzés (a munka megkezdése ELŐTT; parancs: `sha256sum upload/Ox.vst3 octavox-work/original/Ox.vst3 public/PitchLab_R25_DSP_Chain_Research_Package.zip public/PitchLab_R25_DSP_Chain_Research_Package.zip.sha256`)

```
7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2  upload/Ox.vst3
7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2  octavox-work/original/Ox.vst3
46e891c7145461ae7d1baebfa6ee81e70ad277d0db5a17e4bf463f9842962244  public/PitchLab_R25_DSP_Chain_Research_Package.zip
ce664ae5c2bd218415973104df3de8078a9c5ab8745618eddf72260d38c21dba  public/PitchLab_R25_DSP_Chain_Research_Package.zip.sha256
```

### Záró hash-ellenőrzés (a munka befejezése UTÁN; azonos parancs, futásidő: 2026-11-22 16:21:16 UTC)

```
7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2  upload/Ox.vst3
7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2  octavox-work/original/Ox.vst3
46e891c7145461ae7d1baebfa6ee81e70ad277d0db5a17e4bf463f9842962244  public/PitchLab_R25_DSP_Chain_Research_Package.zip
ce664ae5c2bd218415973104df3de8078a9c5ab8745618eddf72260d38c21dba  public/PitchLab_R25_DSP_Chain_Research_Package.zip.sha256
```

**Eredmény (OBS): a kezdő és záró hash minden védett elemen byte-szinten azonos — az ellenőrző munka semmit nem változtatott a repó állapotán.**

### Fő parancslista (a jelentés minden OBS-adata az alábbiakból származik)

| # | Parancs / művelet | Kulcskimenet |
|---|---|---|
| 1 | `sha256sum` + `stat -c '%n | %s bytes'` a 4 védett fájlon (előtte és utána) | a fenti két hashblokk |
| 2 | `python3 -c "import zipfile; … testzip(); extractall('/tmp/r25verify/unpack'); len(namelist())"` | 199 bejegyzés / 190 fájl; `testzip()` = None |
| 3 | `os.walk('/tmp/r25verify/unpack')` összegzés | 190 fájl, 3 777 897 bájt |
| 4 | manifest-keresés `os.walk`-kal | találat: csomaggyökér `SHA256SUMS` |
| 5 | `sha256sum -c SHA256SUMS` (csomaggyökérből) | 189 OK / 0 FAIL / exit 0 |
| 6 | `diff` a ZIP-belüli és a repóbeli `README.md`, `OCTAVOX_ORIGINAL_DSP_CHAIN.md`, `R25_OSSZEFOGLALO.md` között | mindhárom pár byte-azonos |
| 7 | `os.listdir`-alapú darabszámlálás az `analysis_r25`, `evidence_docs/*` könyvtárakon | 114 / 16 / 12 / 12 / 10 / 16 / 6 |
| 8 | `wc -l`, `stat`, `json.load` a kulcs-evidenciafájlokon | 5. táblázat értékei |
| 9 | `Grep`/`rg -n 'analysis_r25'` + python `re` mintavadászat a láncdokumentumban | 1 sor (s607) + 2 fájlnév (s608) + 1 önhivatkozás (s609) |
| 10 | `diff -rq octavox-work/analysis_r25 <archív analysis_r25>` | exit 0, üres kimenet |
| 11 | python fejléc-/struktúra-olvasás (`OCTAVOX_ORIGINAL_DSP_CHAIN.md` `##` szakaszok, `R25_OSSZEFOGLALO.md` §7) | 15 `##` szakasz (§0–§14); §7 csomagjegyzék |

**A célbináris (`Ox.vst3`) feldolgozása kizárólag a SHA-256-újraszámításra korlátozódott; tartalmát nem olvastuk, végrehajtására nem került sor — a projekt „sosem futtattuk” elvéhez az ellenőrzés is maradéktalanul igazodott.**
