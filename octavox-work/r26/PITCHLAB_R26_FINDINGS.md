# PITCHLAB R26 — A BATCHELT INTERPOLÁLT OLVASÁS KÖRE (mért döntési rekord)

**Kör:** 26 (2026) · **Dátum:** 2026-11-22 · **Munkakönyvtár:** `octavox-work/r26/`
(a zöld R24-es forrás friss másolata: `r26/source`)

## 1. A kör kérdése

A 24. kör SIMD-kerneljei a detektort gyorsították (4,8–6,3× kernel-szinten); a
8-hangos profil ezzel a **voice_loop**-ra (64%) és az **interp_reads**-re (27%)
tolódott (R24 stage-mátrix, NET tick-ek). Az R26 célja: az interpolált
gyűrű-olvasások (a 6-tapos kernel-read) gyorsítása **bit-exakt** módon — a
ház-szabály szerint kemény memcmp-kapukkal, nem közeliséggel.

## 2. Amit megépítettünk

### 2.1 A lane-per-read AVX2 kernel (`simd_avx2.cpp: readInterp4AVX2`)

Egy 256-bit-es batch **négy független olvasást** számít — sávonként egyet —, és
minden sáv **pontosan a szkalár referencia műveleti sorozatát** futtatja a
saját késleltetésére: clamp → floor → mu → fázis-index → hat mul+add **szigorúan
növekvő k sorrendben**, sávenként egy szorzat- és egy összeadás-kerekítés
tipponként (nincs FMA ebben a TU-ban). A sáveredmény ezért **szerkezetből
fakadóan bit-azonos** a szkalár referenciával — ugyanaz a bizonyítási osztály,
mint az R24 lag-tengelyes kernel. A súly- és gyűrű-tapok gather-utasításokkal
(vgatherdpd / vgatherdps) töltődnek; a gather által betöltött ÉRTÉKEK
megegyeznek a szkalár terhelésekkel, így maga az aritmetika változatlan.
- Lineáris mód: maszkolt (w+i0) és (w+i0+1) gyűjtés, `a + (b−a)·mu` — a
  referencia műveleti sorrendben.
- ObsKernel6: a float sorok gather-e utáni `_mm256_cvtps_pd` konverzió EXAKT
  (minden float32 → double pontos), így a sáv ugyanazt a double-t szorzó
  értéket kapja, mint a szkalár `static_cast<double>(wv[k])`.
- **A mu == 0 (egész késleltetés) gyors út a hívó felelőssége**: az egész
  késleltetésű kérések (minden park-olvasás!) sosem mennek a kernelbe —
  közvetlen maszkolt gyűrű-olvasásként oldódnak meg (bit-exakt: a referencia
  readInterp saját integer útja ugyanez az olvasás).

### 2.2 A megosztott interpolációs táblák (`src/interp_tables.hpp`)

A Lagrange-táblák R12–R24-ben a `dsp.cpp`-ben voltak fájl-lokálisak; az R26
**szó szerint változatlanul** áthelyezte őket egy megosztott header + a
`simd_dispatch.cpp`-ben élő egyetlen definícióba (`octavox::interp::tables()`).
A konstruktor-aritmetika azonos → az értékek azonos → a bit-exaktság érintetlen
(a teljes pin-rekord rá munálja). A mód-kódok a `Interpolation` enum
alattértékei — `static_assert` rögzíti a szerződést.

### 2.3 A batchelt voice-út (dsp.cpp, v2: cross-voice request pool)

Kettős út: **Scalar** szinten a korábbi kód szó szerint változatlanul fut (a
hash-pinek null kockázata); **AVX2 / AVX2+FMA** szinten a hangciklus három
fázisra bomlik mintánként:

1. **Collect** — hangonként: engagement/gain slew, állapot-léptetés
   (`advanceVoice`, a korábbi kódból szó szerint kifejtve), a commit-árnyék
   tükör-előkészítése (a PREDIKÁLT inkrementált `bxf_c`-vel — ugyanaz az
   determinisztikus aritmetika, amit a szkalár út az inkrementálás UTÁN másol),
   és az olvasás-kérések gyűjtése egy közös poolba (A/B az elsődleges bázison,
   Ao/Bo régi bázison crossfade alatt, plusz az árnyék saját hármasa). Az egész
   késleltetések azonnal, közvetlen gyűrű-olvasásként oldódnak meg.
2. **Execute** — a pool **teli 4-sávos batchekben** fut (a rövid farok valódi
   kérés ismétlésével van kitöltve; a többlet-sávok eredménye eldobódik).
3. **Combine** — hangonként, a szkalár `readVoiceAt`/`processVoiceSample`
   aritmetikájával azonos sorrendben (e/a/bxf/xf blendek, inkrementek), az
   akkumulátor a hangok ugyanabban a növekvő sorrendben törzszi.

**Bit-exaktségi bizonyítás (a tétel, amelyre a kapuk rámunálnak):**
- a gyűrű csak az io_input fázisban íródik, a voice ciklus ELŐTT — minden
  olvasás egy immutable gyűrű felett tisztafüggvény; a végrehajtás
  elhalasztása és ÁTRENDezÉSE nem változtat semelyik értéken;
- a hangállapot hangonként privát (VoiceState, shadow[vi]); az egyetlen
  hangok közti interakció az akkumulátor, amely ugyanabban a sorrendben és
  ugyanazzal a gain==0 ág-szemantikával frissül;
- a kernel minden sávja a szkalár műveletsorozatot futtatja.

### 2.4 FMA-szint: READS NEVER FUSE

Az AVX2+FMA szinten a read kernel **az AVX2 kernelt használja újra szó szerint**
— dokumentált szemantika: a hang-út olvasásai **minden** szinten bit-exaktok az
Scalar referenciához; a fúzott kerekítési eltérés kizárólag a detektor
difference-kernelét érinti (R24 óta dokumentált, opt-in).

## 3. A kapuk (mind fut, mind zöld — Release ÉS ASan)

**115/115 Release + 115/115 ASan** (a 110 R24-alapteszt + 5 új R26-eset).

| kapu | típus | mit bizonyít |
|---|---|---|
| `r26_read4_kernel_bitexact` | HARD (memcmp) | 4 mód × 1024 batch × 4 sáv: a kernel sávenként bit-azonos a szkalár referenciával; + 4096 egész-késleltetés-ügy: az integer gyors út == a közvetlen maszkolt gyűrű-olvasás |
| `r26_engine_readpath_bitidentity` | HARD (memcmp) | 6 konfig (dec/hyb/full × 44.1/48/96k × 4 interpoláció) KONTROLL-CHURN-nel (per-blokk ratio- [unity-t is!], delay-, gain/mute-, voices-váltások — park/engage/xf/bxf/ramp útvonalak): az AVX2 motor kimenete bájtról bájtra azonos a Scalarral; becslések azonosak |
| `r26_engine_fma_churn_closeness` | BOUNDED | churn alatt ≤ 0.05 cent / ≤ 1e-4 (R24 határok) |
| `r26_midstream_level_handover` | HARD | (a) 2^18 mintás churn-futtatás Scalar-vs-AVX2 bájt-azonossága (lassú állapot-eltérés-felügyelet); (b) in-place scalar→AVX2 re-prepare → a teljes újra-futtatás bájtonként azonos egy FRISS AVX2 motorral (a read-kernel re-resolve helyes; R13-c fresh-motor szemantika); (c) az ellenkező irányú váltás visszakapcsolja a szó szerinti utat, az alaphelyzet-egyenlőség fennáll |
| `r26_rt_allocfree_batched` | RT | churn + batchelt út alatt **0 allokáció** (pool/rds a stacken) |

A **nyolc történeti hash-pin** (default_neutrality ×2 és a referencia-rekordok)
változatlanul zöldek — a Scalar út átszerkezése (advanceVoice-kifejtés + a
táblák közösítő áthelyezése) bit-exakt. *(A folyamat tanulsága: az első
mid-stream handover-eset hamis elvárást fogalmazott meg [folytonosságot
várt az R13-c fresh-motor reset-szemantika helyett] — a tesztet átírtuk a
dokumentált szemantikára; a HIBA a tesztben volt, nem a kódban.)*

## 4. A mért eredmények (őszintén)

### 4.1 Kernel-szint (közvetlen timing, nincs stage-timer — `r26/probe/read4_timing`)

2^21 olvasás/valid, gyűrű 2^18, a motor késleltetés-eloszlásával:

| mód | szkalár ns/olvasás | avx2_read4 ns/olvasás | gyorsulás |
|---|---|---|---|
| Lagrange6 (production) | 12.07 | 5.49 | **2.20×** |
| Lagrange6Q | 11.49 | 5.17 | 2.22× |
| ObsKernel6 | 12.29 | 5.37 | 2.29× |
| Linear | 8.16 | 2.53 | 3.22× |

### 4.2 Motor-szint: a termelési profilon +1–2% fal (Amdahl — őszintén)

A 13-ases bench-mátrix (nincs timer, median, best-of-2; R24-re referálva):

| eset | szkalár | R24-avx2 | R26-avx2 | R24→R26 |
|---|---|---|---|---|
| mono_1v_b128 | 367.5 | 135.6 | 135.1 | +0.4% |
| mono_8v_b128 | 587.5 | 348.0 | 346.9 | +0.3% |
| stereo_8v_b128 | 1175.8 | 708.8 | 694.6 | +2.0% |
| mono_8v_b128_96k | 588.7 | 352.2 | 347.6 | +1.3% |
| mono_8v_b128_ctrl512 | 307.7 | 284.2 | 279.1 | +1.8% |

A kettős méréstanulság:
- A **stage-műszer** (R24 óta a ház-standard, overhead-kompenzált NET
  tick-ek) a read-aritmetika összeomlását mutatja: dec_8v_48k-nél az
  interp_reads NET ~115.6M (R24) → ~5–16M tick, a scope-szám 1.536M → 96k
  (sávonként 4 olvasás, egy pool-hívás mintánként). A hang-út hátralévő
  ~84%-a a TIMER-profilban a soros állapotgép + blend-aritmetika.
- A **falóra valósága**: a production 8-hangos fal +1–2%. Az ok Amdahl: az
  olvasások margóköltsége a motorban kicsi volt (az OoO mag a szkalár
  terhelések latenciáját már átfedte a soros állapotgéppel); a soros
  hang-állapotgép (glide/frac/wrap/commit + e/a/bxf/xf blendek, adatfüggő
  elágazásokkal) a valódi korlát. **További falnyereséghez az állapotgépet
  kell átalakítani (R27-es téma), nem az olvasásokat.**

### 4.3 Amit mérés közben javítottunk

1. A v1 per-hang batchelés csak 2/4 sávot töltött (A+B hangonként) — a v2
   cross-voice pool teli sávokat ad; a kernel-nyers-munka ~3,5×-öt esett
   (stage-NET 95.6M → 27.6M nyers tick @48k), a fal ugyanaz (+1–2%) —
   megerősítve az Amdahl-olvasatot.
2. A v2 fejlesztés közben az árnyék-collectből kimaradt az `advanceVoice(sh)`
   — a churn bit-identity kapu AZONNAL elkapta (5/6 konfig elvált). Ez a
   kapu-rendszer pontosan azt csinálja, amire való.

## 5. R27-jelöltek (rögzítve, nem elindítva)

1. **A soros voice-állapotgép** — a mért új szűk keresztmetszet (~84% a
   timer-profilban). A bit-exaktségi korlát miatt nem vektorizálható
   közvetlenül (adatfüggő elágazások + mellékhatások); a járható út a
   mintákra előre-haladó (lookahead) állapot-ütemezés és a blend-aritmetika
   maszkolt kiértékelése lenne — nagy tervezési kör.
2. Pre-shift anti-alias szűrő pozitív transzponálásnál (R24-ből örökölt
   jelölt; +12 st-nél +9.4 dB HF a mérés szerint).
3. Per-host FMA-mérés.

## 6. Őszinte határok

- A referenciagép MEGOSZTOTT 2-magos cloud vCPU — az abszolút számok
  futásról futásra szórnak; a bench13-median + best-of-2 és a kernel-próba
  (best-of-több-pass) a stabil műszerek; a stage-falak a timer-distorsióval
  terheltek (kevesebb scope = kisebb zavar — ezért NEM a fal a műszer, hanem
  a NET tick).
- Valós DAW-ellenőrzés továbbra is NEM TESZTELT (a Validation Kit marad az
  eszköz); natív MSVC a sandboxban BLOCKED (zig-cross útvonal érintetlen).
- A kernel lane-per-read bizonyítása szerkezeti — de a gyakorlati bizonyság
  a kemény kapu: minden átmenet-típus (park/engage/részleges/xf/bxf)
  churn-nel, 4 interpolációs móddal, bájt-azonos kimenetet követel.

## 7. Provenancia

- Munkakönyvtár: `octavox-work/r26/` — a forrás az r24/source zöld
  állapotának másolata, az R26-deltákkal (simd.hpp/simd_dispatch.cpp/
  simd_avx2.cpp/dsp.cpp/tests + a probe).
- Bizonyítékfák: `r26/source/evidence/analysis_round26/logs/` (g1–g7:
  stage-mátrixok szkalár/avx2 v1+v2, bench13 ×3, ASan-futam kimenete a
  szokásos futtatásban rögzül).
- A szerver-oldali körmunka (weboldal, csomag, git) a megszokott láncban
  követte a kódot; az eredeti Eventide bináris a projekt történetében
  továbbra is sosem futott (tisztaasztal-fegyelem).
