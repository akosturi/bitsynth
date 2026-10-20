# bitsynth

Nyolclépes, három üzemmódú wavetable szintetizátor Arduino Pro Minihez. A firmware a kevés kezelőszervet közvetlen, előadás közben is használható funkciókra osztja: sequencer, skálára kvantált free play és nyolcoszcillátoros drone mód.

## Hardver

A program 16 MHz-es ATmega328P alapú Arduino laphoz készült, például 5 V-os Pro Minihez, Nanóhoz vagy Unóhoz.

- Mux címvezetékek: `D8`, `D11`, `D10`
- Négy paraméterpoti: muxon keresztül `A0`
- Nyolc step gomb: muxon keresztül `A1`
- Nyolc step poti: muxon keresztül `A2`
- Módválasztó/panic gomb: `A3`
- 16×2 LiquidCrystal LCD: `D2–D7`
- 9 bites PWM audiokimenet: `D9`

A hangot Timer1 állítja elő 9 bites PWM-mel, a 20 kHz-es audio interruptot Timer2 adja. A PWM kimenet után külső analóg aluláteresztő- és kimeneti fokozat szükséges.

## Módváltás

Az `A3` gomb rövid megnyomása körben vált:

`SEQ → PLAY → DRONE → SEQ`

Egy másodperces nyomva tartás panic műveletet hajt végre. Ez leállítja a normál hangokat, PLAY módban törli a tartott billentyűket, DRONE módban pedig kikapcsolja mind a nyolc oszcillátort.

## SEQ — nyolclépes sequencer

- Paraméterpoti 1: preset
- Paraméterpoti 2: `60–220 BPM`
- Paraméterpoti 3: presetfüggő `COLOR`
- Paraméterpoti 4: presetfüggő `SHAPE`; `LEAD` esetén glide
- Step potik 1–8: kromatikus hangok `C2–B3` között; `DRUM` presetben dobhangszerválasztók
- Step gombok 1–8: az adott lépés be/ki

A lépések tizenhatodok. Az LCD alsó sorában `x` jelöli a bekapcsolt, `-` a kikapcsolt lépést, `O/o` pedig az aktuális lépést.

### Sequencer presetek

| Preset | Felépítés | COLOR | SHAPE |
|---|---|---|---|
| `SUB` | sine + square szuboktáv | detune | decay |
| `BASS` | saw + square szuboktáv | pitch sweep | decay |
| `PLUK` | triangle + square oktáv | detune | decay |
| `BELL` | sine felhangok + triangle | inharmonikus eltolás | ring hossz |
| `LEAD` | saw + ramp | spread | glide |
| `CHRD` | négyrétegű akkord | minor/major/sus2/sus4 | decay |
| `CHIP` | két square oktávban | sample-hold/crush | decay |
| `PERC` | noise + mély sine | zajhang/pitch sweep | decay |
| `DRUM` | monofonikus 8 bites PCM dobmotor | lejátszási hangolás | decay |

### DRUM preset

A `DRUM` nem hoz létre további sequencer-sávokat: minden lépés pontosan egy dobhangot tárol. A következő aktív lépés az előző dobhangot azonnal megszakítja, ezért nincs dobhang-átfedés vagy drum voice stealing.

- Step potik: `KICK`, `SNAR`, `CHH`
- Step gombok: az adott dobütés be/ki
- Paraméterpoti 3 / `T`: az aktuális dobok közös tone/tuning makrója
- Paraméterpoti 4 / `D`: hangszerfüggő decay

A dobok külön monofonikus PCM-lejátszóból szólnak. A minták unsigned 8 bites, monó, 20 kHz-es adatokként a `tables.h` fájlban találhatók. A lejátszó közvetlenül a flashből olvas, RAM-másolat nélkül. A `T` poti 0,75×–1,25× között változtatja a lejátszási sebességet, a `D` poti pedig a minta hosszának 25–100%-ára állítja a lecsengő amplitúdóburkolót. Új ütés az előző mintát azonnal újraindítja. PLAY módban a DRUM sequencer ugyanazon a fenntartott monó háttérhelyen fut tovább, így a három live voice megmarad.

## PLAY — skálabiztos élő játék

A sequencer PLAY módban is megszakítás nélkül fut tovább. Voice 0 a kapcsolható monó háttérszekvenszeré, voice 1–3 az élő játéké.

- Paraméterpoti 1: live preset
- Paraméterpoti 2: gyökérhang `C–B`
- Paraméterpoti 3: `MAJ`, `MIN`, `PEN`, `BLU` skála
- Paraméterpoti 4: `POLY`, `CHRD`, `UP`, `DOWN`, `UPDN`
- Step poti 1: `C2–C5` oktávsáv
- Step potik 2–8: nincs funkció
- Step gombok 1–8: a kiválasztott skála egymást követő hangjai

A gombok lenyomásra indítják, felengedésre leállítják a hangot. `POLY` módban három hang szólhat egyszerre; a negyedik lenyomás a legrégebbi voice-t veszi át. `CHRD` skálán belüli 1–3–5 akkordot játszik. Az arpeggiók a tartott gombokat a sequencer tizenhatod-órajelével járják be.

PLAY módban a MODE gomb nyomva tartása közben az első step gomb kapcsolja be vagy ki a háttérszekvenszert. A kijelzőn `SEQ+` jelzi a bekapcsolt, `SEQ-` a némított állapotot. A szekvenszer órája és pozíciója kikapcsolt háttérhang mellett is tovább fut, ezért az arpeggiók tempója nem áll meg.

MODE nyomva tartása közben a négy paraméterpoti a három élő voice közös hullámtábla-morphját és low-pass szűrőjét vezérli:

- Paraméterpoti 1: háromszög LFO `OFF`, illetve `0,1–5,0 Hz` között, `0,1 Hz`-es lépésekben
- Paraméterpoti 2: második hullámforma; teljesen letekerve `OFF`, fölötte `SIN`, `TRI`, `SQR`, `SAW`
- Paraméterpoti 3: a három élő voice közös egyszerű low-pass szűrője; `OFF`, majd körülbelül `6,6 kHz`, `4,4 kHz`, `2,2 kHz`, `916 Hz`, `425 Hz`, `205 Hz` és `101 Hz`
- Paraméterpoti 4: háromszög filter-LFO `OFF`, illetve `0,1–5,0 Hz` között, `0,1 Hz`-es lépésekben

Kikapcsolt második hullámformánál az LFO nem állítható. Második hullámforma és kikapcsolt LFO esetén csak a második hullám szól; aktív LFO-nál a hang folyamatosan halad az eredeti preset-hullám és a második hullám között. A két táblát minden voice ugyanannál a fázispozíciónál olvassa, ezért a morph nem használ további oszcillátort, és nem változtatja meg a hangmagasságot. A háttérszekvenszer számára fenntartott voice 0 nem kap morphot.

Kikapcsolt filter mellett a filter-LFO nem állítható. Aktív filter és kikapcsolt filter-LFO esetén a 3. potival kiválasztott fix cutoff érvényesül; aktív LFO-nál a cutoff a teljesen nyitott állás és a kiválasztott legmélyebb érték között lépcsőzetesen mozog. A filter és mindkét LFO csak a három élő voice-ra hat, a háttérszekvenszerre nem.

SHIFT-szerkesztés után mind a négy poti soft takeoverrel tér vissza a preset, a gyökérhang, a skála és a játékstílus vezérléséhez: egyik érték sem ugrik el a MODE felengedésekor, csak a poti következő szándékos elmozdításakor.

Live presetek: `PURE`, `SOFT`, `KEYS`, `PLUK`, `BASS`, `LEAD`, `ORGN`, `LOFI`.

## DRONE — nyolc folyamatos oszcillátor

DRONE mód leállítja a sequencert és külön, nyolcoszcillátoros audio keverőt használ.

- Paraméterpoti 1: az 1–2. oszcillátor hullámformája
- Paraméterpoti 2: a 3–4. oszcillátor hullámformája
- Paraméterpoti 3: az 5–6. oszcillátor hullámformája
- Paraméterpoti 4: a 7–8. oszcillátor hullámformája
- Step potik: folyamatos, nem kvantált frekvencia `80–600 Hz` között
- Step gombok: a hozzájuk tartozó oszcillátor be/ki

Választható hullámformák: `SIN`, `TRI`, `SQR`, `SAW`. A gate-ek rövid amplitúdó-rámpát kapnak a kapcsolási kattanások mérséklésére. DRONE után a sequencer az első lépésről indul újra.

MODE nyomva tartása közben a négy paraméterpoti külön VCA-LFO sebességet állít az 1–2., 3–4., 5–6. és 7–8. oszcillátorpárhoz. A teljesen letekert állás `OFF`, fölötte a teljes potiút finom, `0,1 Hz`-es lépésekben fedi le a `0,1–5,0 Hz` tartományt. Ha LFO-t állítottál, a MODE felengedése nem vált üzemmódot, és a hullámforma csak a paraméterpoti következő szándékos elmozdításakor veszi vissza a vezérlést.

A PWM kimenet továbbra is 20 kHz-en frissül. A processzorterhelés kordában tartásához a drone motor a négy oszcillátorpárt körkörösen, páronként 5 kHz-en számolja újra, a köztes PWM mintákhoz pedig megtartja a párok legutóbbi értékét. Ez a 80–600 Hz-es drone tartományban tudatos hangminőség/teljesítmény kompromisszum.

## LCD

- SEQ: preset, BPM, COLOR/SHAPE, gate maszk és aktuális lépés
- PLAY: preset, gyökér, skála, játékmód, oktáv és a háttérszekvenszer állapota
- DRONE: a négy oszcillátorpár hullámformája és a nyolc gate

A paraméterpotik értékei közvetlenül a főképernyőn változnak, nincs külön szerkesztési képernyő. Sequencer pitch szerkesztésekor a hangnév, PLAY shift-szerkesztéskor a `B:` hullámforma, az `L:` morph-LFO, az `F:` filterfrekvencia vagy az `FL:` filter-LFO értéke, drone pitch szerkesztésekor a frekvencia, VCA-LFO szerkesztésekor pedig az `L1`–`L4` csoport és annak `OFF`/Hz értéke jelenik meg röviden a főképernyő jobb oldalán. Az LCD csak a ténylegesen megváltozott karaktereket írja újra, így nem villog minden sequencer-lépésnél.

## Fordítás

Nyisd meg a `bitsynth.ino` fájlt Arduino IDE-ben, válaszd az Arduino Pro or Pro Mini / ATmega328P 5V 16 MHz célt, és telepítsd a hivatalos `LiquidCrystal` könyvtárat. A szintetizátormotor és a wavetable adatok a projekt részei.

Arduino CLI példa:

```powershell
arduino-cli compile --fqbn arduino:avr:pro:cpu=16MHzatmega328 --library <LiquidCrystal-könyvtár> .
```

## Korlátok

- Nincs EEPROM-mentés; a beállítások újraindításkor alapértékre állnak.
- A PLAY mód három élő voice-a közös, rezonancia nélküli egy pólusú low-pass szűrőt használ; a legacy, voice-onkénti `setFilter()` függvény továbbra sem része a hangútnak.
- A normál hangmotor négyszólamú. PLAY módban ebből egy voice a háttérszekvenszeré és három az élő játéké.
- A nyolc drone oszcillátor egyszerűsített, burkológörbe nélküli keverőágat használ az ATmega328P időkeretének betartásához.
