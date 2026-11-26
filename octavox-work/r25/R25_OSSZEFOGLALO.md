# R25 ÖSSZEFOGLALÓ — Az eredeti Octavox VST3 belső DSP-láncának teljes feltárása

**Dátum:** 2026. november 22. · **Kör:** 25 (zárókör)
**Tárgy:** az eredeti Eventide Octavox VST3 („Ox.vst3", PE32+ x86-64, 5 307 392 bájt,
SHA-256 `7bb260f66bb9c1b21b27f64f29e6fdd2865c34f8a31ad1c0ef21a1db7f32f1f2`)
belső működésének pontos, statikusan bizonyított képe.

**A kérés:** „szeretném ha teljes belső dsp lánc fel lenne tárva, nem kell ezer teszt —
pontos képet szeretnék kapni az eredeti Octavox VST belső működéséről."
**Az eredmény:** igen — a lánc minden szakasza le van képezve, utasításszintű
bizonyítékkal. Ez a dokumentum az összefoglaló; a teljes részletes térkép a csomagban
`OCTAVOX_ORIGINAL_DSP_CHAIN.md` néven (610 sor, 14 fejezet).

---

## 1. Módszer — miért hihető ez a kép

* **Tisztán statikus visszavezetés**: Ghidra 12.1.4 (R1–R24) + capstone 5.0.7
  közvetlen PE-olvasás (R25). Az eredeti binárist a projekt teljes történetében
  **egyszer sem futtattuk és nem is módosítottuk** — minden állítás kódból,
  bájtából származik.
* **Megfordítható bizonyítéklánc**: minden elemzés előtt és után ellenőriztük a
  bináris SHA-256-át (fenti hash, változatlan).
* **Négyfokú bizonyosság-skála minden állításon**: **OBS** (a kódban/bájtokban
  megfigyelt) · **DER** (a megfigyelt szerkezettől levezetett) · **UNK**
  (statikusan nem megállapítható) · **CONFLICT** (két megfigyelés eltér — minden
  ismert eset dokumentált és feloldva).
* **Nem futásidejű megfigyelésre épít**: a viselkedéstanúságok (pl. null-tesztek)
  mindig a SAJÁT PitchLab újraimplementáción futottak — az eredetit sosem
  hangoltuk össze futó példányával (clean-room fegyelem).

---

## 2. A teljes jelfolyam — a lánc 12 lépésben

```
VST3 host
 └─ JuceVST3Component → JuceAudioProcessor → VoxAudioProcessor ("Octavox")
     └─ profilválasztás: (1,1)→mimo 395 modul · (1,2)/(2,2)→miso/siso 394 modul
        ▼
 ① IGAN   bemeneti erősítés (−96…+12 dB, dB→lin) + mono-összefésülés
 ② MODIIRQ  anti-alias IIR: 2 szekció, q=0.7, LP — a cutoff az aktuális
            hangolás LEGNAGYOBB lefelé-intervallumának frekvenciaaránya
            (pl. −1 oktáv → 0.5 · Nyquist) — dinamikusan követi a pitch-beállítást
 ③ SOFTCLIP a motor előtt (select=1, drive=0, order=1)
 ④ DTONICII9  A MOTOR (modul+0x430) — a szív, 4 mintás alblokkokban:
      4a  bemenet-előzmény gyűrű (44.1 kHz-en 2^17 = 131 072 minta)
      4b  PERIODICITÁS-TRACKER (~12 kHz-en, a 4 mintából 2-t fogyaszt):
          21-tapos szimmetrikus FIR decimáló → adaptív gate (lecsengő peak) →
          AGC (szintfüggetlenít) → 243 lag · 48 mintás ablakú korreláció →
          10-állapotú gép (csúcskeresés + parabolás finomítás + oktáv-
          összefésülés + konfidencia 0.203125) → 4096-float rekord-gyűrűn
          {correction, i4, i5, 2·period} rekordok → nyolc hang közösen olvassa
      4c  DIATONIKUS HARMÓNIA-TÉRKÉP: kulcs(17) × skála(19) × temperamentum(5:
          12-TET, just dúr, just moll, püthagorási, ¼-komma meantone) ×
          intervallum-mux(29) → 8 soros per-hang ráció-tábla ("page")
          — a nyolc hang EGYMÁST KÖVETŐ skálafokokon ül (diatonikus stack)
      4d  8 HANGMAG: változó sebességű olvasás a work-gyűrűből —
          step = 4·P(f)·ratio·correction, ahol P(f) a 2^(cents/1200)
          KVARTIKUS polinom-approximációja (illesztési hiba ≤ 0.101 cent,
          egység f=0-nál bit-exakt); olvasás a 256×6-os statikus
          interpolációs kernellel; settled (1 tap) és crossfade (2 tap,
          periódus-igazított átadás) út + re-arm ugrás
 ⑤ MODFILTERN  20 Hz magaspassz hangonként (DC-védelem)
 ⑥ EQNN  instrumentum-függő anti-alias EQ (neqs=1, q=0.7, vágó; a 13
          instrumentum-táblából CutFreq/MaxCut/CutMultiplier)
 ⑦ MODDELAY  de-click delay hangonként (nyers dlyN 0…1250 ms)
 ⑧ FBACKN  per-hang visszacsatolás (fbkN −100…0 dB → lin gain)
 ⑨ STEREOMIXER3  8 → sztereó: gain (dB→lin) + LINEÁRIS pan-törvény
          (L = 0.5·(1−p)·g, R = 0.5·(1+p)·g — konstans-összegű, NEM
          equal-power) + 25 ms egy-pólusú gain-ramp, double akkumuláció,
          ±clipvalue clamp
 ⑩ GLOBÁLIS VISSZACSATOLÁS  a post-pan sztereó wet jelekből:
          feedback (mfbk 0…100%) → softclip → delay_xf (2400 ms, 20 ms
          crossfade, tapdls1 = max(fbkd,50) ms) → 5 Hz DC-blokkoló →
          visszazáródik a hurokba — két egymásba ágyazott visszacsatolási
          rendszer (per-hang + globális), mindkettő védett
 ⑪ WET/DRY  mmix 0…100% (live módban 40 minta bemenet-kompenzációval),
          10 ms de-zipper slew
 ⑫ OGAN  kimeneti erősítés (−96…+24 dB) → kimeneti frakcionális-olvasók
          (ugyanaz a 6-tapos kernel) → hard bypass (iswitch) → KIMENET
```

---

## 3. A legfontosábbi számszerű megállapítások

| # | Megállapítás | Érték | Bizonyosság |
|---|---|---|---|
| 1 | Paraméterfelület | **73 paraméter = 17 globális + 8×7 per-hang** | OBS |
| 2 | Hangok száma | 8, mindet **EGY** tracker táplálja rekord-gyűrűn át | OBS |
| 3 | Tracker futási ráta | ~12 kHz (kadencia-osztó 2/4/8 SR-osztály szerint) | OBS |
| 4 | Korrelációs mag | 243 lag, 48 mintás ablak → ~49 Hz padló; keresést a `minp` (C0…B3, alap C2) szűkíti | OBS/DER |
| 5 | Interpolációs kernel | **256×6-os statikus float-tábla** (.rdata 0x1803B6170) — NEM Lagrange; pontszimmetrikus, DC-egység, −3 dB 0.60–0.77·Nyquist | OBS |
| 6 | Ráta-törvény | `step = 4·P(f)·ratio·correction`; P kvartikus approx., hiba ≤ 0.101 cent; egység f=0-nál bit-exakt | OBS |
| 7 | Strukturális késleltetés | ~288 minta ≈ 6.5 ms @44.1 kHz (280 init + gyűrű + kernel) | DER |
| 8 | Random-moduláció | egyoldalúan FELFELÉ (cent ∈ [0, +200] — sosem negatív); csúcsok +49.94¢ (mélység 50) és +199.92¢ (200) | OBS |
| 9 | Pan-törvény | **lineáris, konstans-összegű** (L = 0.5·(1−p)·g, R = 0.5·(1+p)·g) — **R25-ös lezárás** | OBS |
| 10 | FIR decimáló | 3 sebesség-osztály (5/6, 10/11, 20/21 taps), együtthatók R25-ben kinyerve — **lásd §4** | OBS |
| 11 | Temperamentumok | 5 (12-TET, just dúr, just moll, püthagorási, meantone — centtáblák kinyerve) | OBS |
| 12 | Intervallum-készlet | a 29-es mux realizált hangzata felfelé {P1, M3, P4, P5, M7, P8} + tükrös inverziók; a 2. és 6. skálafok SOHA nem célpont | OBS |
| 13 | RNG | késleltetett-Fibonacci additív generátor (rendje 2^22, 4 kimenet/hívás), |noise|→S&H→slew-lánc | OBS |
| 14 | Anti-alias IIR cutoff | dinamikus: a legnagyobb lefelé-intervallum aránya (2^(−c_minmax2/7)), slew-simítva | OBS |
| 15 | Vezérlés-architektúra | zár-mentes „pull" gráf: piszkos-bit LOCK OR-ral, fogyasztó LOCK CMPXCHG-gel — az audio-szál sosem vesz zárat | OBS |

---

## 4. Az R25-ös újdonságok — a lánc utolsó két nyitott kötése

A korábbi 24 kör a lánc minden elemét feltérképezte, két kérdés kivételével,
amelyeket ez a kör zárt le (mindkettő utasításszintű, kódba ágyazott
bizonyítással):

### 4.1 A tracker FIR-decimáló valódi együtthatói

* A gyakorlati jelentőség: ez a szűrő dönti el, **milyen sávon „lát" a pitch-
  detektor** — eddig csak a szerkezetét ismertük, az értékeit most kinyertük.
* A együtthatók **nem .rdata-ban, hanem inline float32 kód-immmediánsokként**
  élnek az `m01` setparam `FUN_1800E10B0` case-5 (mintavételi ráta) ágában —
  ezért nem találtuk korábbbban a táblázatokban.
* **Három sebesség-osztály** (a küszöbök 50 kHz és 100 kHz):

| osztály | SR | taps (elő/hátra) | DC-gain | −6 dB (kb.) |
|---|---|---|---|---|
| A | <50 kHz | 5/6 | 0.9972 | ~2.2 kHz |
| B | 50–100 kHz | 10/11 | 0.9926 | ~2.3 kHz |
| C | ≥100 kHz | 20/21 | 0.9896 | ~2.3 kHz |

* **A felezőlánc bit-exakt**: B[2k] = A[k]/2 és C[2k] = B[k]/2 pontosan
  (karakterisztika−1) — vagyis egyetlen windowed-sinc prototípus stb.
  1×/2×/4× tapsűrűséggel. Ez a legmagstrendűbb szerkezeti bizonyíték: nem
  három egymástól független szűrő, hanem egy tervező három skálázása.
* Osztály-A pontos együtthatói (6 db):
  `0.015604496, 0.0480358601, 0.0866985321, 0.122535944, 0.147476673, 0.156473994`
  (a B és C osztály teljes táblái a master dokumentum §6.2-ben és az
  `analysis_r25/` kibontható bizonyítékanyagában).

### 4.2 A stereomixer3 pan-törvénye

* **Lineáris, konstans-összegű** (−6 dB középpontú): `L = 0.5·(1−p)·g`,
  `R = 0.5·(1+p)·g`, ahol `g = 10^(dB/20)`.
* Határozottan NEM equal-power sin/cos (nincs π/4-referencia a kódban), NEM
  √-törvény (nincs sqrt), NEM táblás (a korábbi jelölt táblának nulla
  hivatkozása van — hivatatlan blob).
* Utasításonkénti bizonyíték: a `0x1800D9C56..DE` sorozat (×0.5;
  (0.5−0.5p); ×g) és a `(p+1)·0.5; ×g` pár — a FUN_1800D9B20-ben.
* A keverő PROCESS-funkciója maga is azonosításra került (`FUN_1800D7F10`),
  a worker-fából pontos hívási ponttal (a motor prepare és a 8 hanglánc után —
  a végrehajtási sorrend így teljesen zárt).

---

## 5. Mennyire teljes a kép?

| Láncszakasz | státusz |
|---|---|
| VST3 burkolat, profilválasztás, graph-topológia (395 modul) | **teljes** (OBS) |
| Paraméterfelület (73 paraméter, tartományok, alapértékek, célok) | **teljes** (OBS) |
| Bemeneti kondicionálás (gain, anti-alias IIR, instrumentum-táblák) | **teljes** (OBS) |
| Periodicitás-tracker (FIR, gate, AGC, korreláció, 10-állapotú gép, rekordok) | **teljes** (OBS) — FIR-értékek R25 |
| Harmonia-térkép (temperamentum/skála/kulcs/intervallum/séta) | **teljes** (OBS) |
| Hangmotor (ráta-törvény, RNG, olvasó-út, kernel, késleltetés) | **teljes** (OBS/DER) |
| Per-hang utófeldolgozás (HP, EQ, delay, visszacsatolás) | **teljes** (OBS) |
| Keverés és kimenet (pan, wet/dry, globális visszacsatolás) | **teljes** (OBS) — pan R25 |
| Kernel generáló algoritmusa | **jellemmezve, de nem rekonstruálható** (build-time adat) |
| Minden futásidejű hangzásbeli viselkedés | clean-room elvből **nem állítjuk** (UNK) |

---

## 6. Őszinte hézagok — ami statikusan nem zárható le

A master dokumentum §12 kilenc tételt sorol; a legfontosabbak:

1. **A 6-tapos kernel generáló algoritmusa** — a tábla maga kinyerve és
   karakterizálva (szimmetria, DC-egység, sávszélesség), de a pontos tervező
   (Lagrange/Farrow/equiripple/… mind kizárva) build-time adat — nem
   rekonstruálható.
2. **Futásidejű viselkedések** — az eredetit sosem futtattuk; pl. a m02 12/13
   olvasatok kettős konverziós gate-hibájának KIMENETE előrejelzés (DER), de
   nem futásidején ellenőrzött.
3. **`sht1` (±2400) vs `sht2..8` (±1) tartományütközés** — a 2..8 hangoknál
   a kijelzés cent-hányadosra utal, az 1-es nyersen centben — szerzői
   ellentmondás a binárisban (dokumentálva, nem feloldható).
4. **MIDI/tempo-útvonal** — a DSP-records közt nincs (negatív eredmény,
   mindhárom profilban ellenőrizve); a JUCE-oldali keretben él.
5. A rekord `i4` mezője **dead field** (a motoron kívülről senki nem olvassa).

---

## 7. Mit tartalmaz a csomag (PitchLab_R25_DSP_Chain_Research_Package.zip)

```
R25_OSSZEFOGLALO.md            — ez a dokumentum (vezetői összefoglaló)
OCTAVOX_ORIGINAL_DSP_CHAIN.md   — a TELJES lánc-térkép, 610 sor, 14 fejezet
                                  (§0 egyoldalas áttekintés → §14 provenance)
analysis_r25/                   — az R25 közvetlen bizonyítékanyaga (114 fájl):
                                  • fir_coefficients.md — a FIR-lezárás teljes
                                    elemzése + bit-exakt felezőlánc-bizonyíték
                                  • pan_law.md — a pan-törvény lezárása
                                  • r25_fir_dump.py/.txt (6500 soros annotált
                                    disassembly), r25_fir_tables.json,
                                    r25_extract_tables.py + az összes stepN
                                    szkript/nyers kimenet
evidence_docs/
  core/        — 14 alapdokumentum (DSP-architektúra, core-internals, tracker
                 mélyelemzés, vezérlésfolyamat-mátrix, paraméterszemantika,
                 harmonia-mapper spec, minimum-pitch út, rekonstrukciós riport…)
  rounds/      — a 6–22. körök findings-dokumentumai
  deliveries/  — a 15–22. körök kézbesítési riportjai + handover
  superseded/  — a korai 01–10-es dokumentsorozat (archív)
  r23_yin/     — a 23. kör 16 YIN-kutatási dokumentuma
  r24_simd/    — a 24. kör SIMD-architektúra/benchmark-dokumentumai
SHA256SUMS     — a csomag minden fájljának ellenőrző összege
README.md      — csomag-index
```

*(Az eredeti Eventide bináris IP-védelem miatt NEM része a csomagnak —
minden megállapítás a csomagban lévő, saját kezű statikus elemzésekből
származik, utasításcímekkel visszaellenőrizhetően.)*

---

## 8. A nagy történet egy bekezdésben

Az Octavox egy JUCE-burkolatú, profilfüggő modul-gráf (mono/sztereó
kombinációk szerint 394/395 modullal), amelynek magja a **dtonicii9**
nevű motor. A bemenetet egy dinamikus anti-alias szűrő kondicionálja,
amelynek cutoffja a beállított legnagyobb lefelé-intervallumhoz igazodik.
Egyetlen, ~12 kHz-en futó **periodicitás-tracker** méri a bemenet
alapfrekvenciáját (243 lagos ablakozott korreláció + parabolás
finomítás + oktáv-összefésülés), majd 4096-elemű rekord-gyűrűn osztja meg
nyolc hang között. A **diatonikus harmonia-térkép** (kulcs × skála ×
temperamentum × intervallum) nyolcelemű ráció-sort állít elő — a nyolc hang
egymást követő skálafokokon ülnek. Minden hang a bemenet-előzmény
gyűrűjéből olvas **változó sebességgel** (a kvartikus polinom-approximáció
≤0.101 cent hibájával), a bináris egyetlen 6-tapos kernelével interpolálva,
settled/crossfade úton oktáv-határokon átugorva. Hangonként 20 Hz-es
magaspassz és instrumentum-függő anti-alias EQ, majd a **lineáris pan-
törvényű** 8→sztereó keverő következik; a post-pan wet jelek egy globális,
soft-clippelt, DC-védett visszacsatolási hurokba záródnak; wet/dry összeállítás
és kimeneti erősítés zárja a láncot. A vezérlés zár-mentes piszkos-bit
gráf, az audio-szál sosem vesz zárat. A struktúra ~288 mintás (~6.5 ms)
késleltetést hordoz.

**A kép megvan. Nem ezer teszt kellett hozzá — hanem 25 kör állhatatos,
állításról állításra bizonyítható statikus olvasmány.**
