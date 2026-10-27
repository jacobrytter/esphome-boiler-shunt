# Iltsonde – test hos naboen (2026-10-27)

Den ny iltsonde (samme type som i [ksm-oxygen-sensor-notes.md](ksm-oxygen-sensor-notes.md)) er afprøvet på naboens KSM Tuxedo F-Auto, som er samme model, men lidt nyere. Formålet var at finde ud af, om fejlen ligger i sonden eller i vores styring.

## Naboens styring

| | Naboen | Vores |
|---|---|---|
| Styring | PCT400-001-001-TUXFAUTO | PCT400-001-001-TUXFAUTO |
| Firmware | **1.0.5501** (2025-04-02 08:10:16) | **1.0.4309** (2019-08-12 11:36) |
| Bootloader / CPU | 1539 / MX575F512H | 1539 / MX575F512H |
| CFG ID | 0 | 1 |
| CAL | 100,100,100,100,100,100 | 100,100,100,100,100,100 |
| Fuel | 1708 kg, time 202572 s | **PARAM ERR** |
| Iltindgang | Modul 2, I/O 1, AI | Modul 2, I/O 1, AI |

**CAL står på 6 × 100 hos begge**, også efter en vellykket kalibrering hos naboen. CAL-linjen har altså intet med iltkalibreringen at gøre.

Naboen har desuden **modul 30** (PCT430-udvidelsen) med *Retur temp.* på `30.1.AI`. I/O-tildelingen (*Kedel*-menuen) er: Ilt 02.1.AI, Røgtemperatur 02.3.AI, Kedeltemperatur 02.2.AI, Retur temp. 30.1.AI, Varmstoker temp. 1 00.4.AI, Stokersnegl 1 02.1.DO, Blæser 1 02.4.DO.

## Målinger

Rå værdi fra *Ilt / Test*. Der er ingen spændingsmålinger fra naboens klemmer.

| Tid | Test (ADC) | Vist ilt | Situation |
|---|---|---|---|
| 20:45:44 | 219 | – | Drift |
| 20:50 | 240 | 14,7 % | Drift (røgtemp. 163 °C, ydelse 15 %) |
| 20:54 | **113** | **21,00 %** | **Ny sonde i fri luft** |
| 21:40 | 363 | 9,33 % | Normal drift med naboens egen sonde |

Naboens hovedskærm viste samtidig røgtemperatur 162 °C, kedel 76,77 °C (SP 74), ydelse 15 %, PBlæser 15 og *SP: 15.00 %* (sandsynligvis iltsætpunktet).

## Vurdering

1. **Den ny sonde er i orden.** Den viser 21 % i fri luft på naboens styring.
2. **Naboens kurve passer med Nernst-ligningen.** Mellem 113 (21 %) og 363 (9,33 %) er hældningen ca. 308 tællinger pr. e-fold. Med ca. 15 tællinger/mV (målt på vores styring) svarer det til ca. **20 mV pr. e-fold**, og teorien giver ca. 21 mV ved 700 °C.
3. **Nulpunktet er meget forskelligt.** Samme sonde i fri luft giver **ADC 113 hos naboen og ADC 412 hos os**. Forskellen er ca. 300 tællinger, svarende til ca. 20 mV. Hos os giver indgangen ca. 5,6 mV uden sonde og 6,8 mV med sonden i luft, så vores indgang ser "0 V fra sonden" ved ca. ADC 410. Naboens ser det ved ca. 113.
4. **Mulige forklaringer:**
   - Firmware 1.0.5501 behandler indgangen anderledes end 1.0.4309 (offset eller bias i software).
   - Indgangskredsløbet på vores modul 2 er anderledes eller defekt (bias/reference), så indgangen er forskudt ca. 20 mV.
   - En forskel i ledningsføringen eller stellet.
5. **Med batteriet** (forskydningsspænding i serie) kalibrerede vores styring til ca. 21 mV / ADC ≈ 180 i luft med kun ca. 15 mV pr. e-fold. Naboens 20 mV passer bedre med en rigtig sonde. Vores visning med batteriet er derfor for lav i røggas (vist 8,3 % ≈ reelt ca. 10,7 %).

## Næste skridt

- **Mål spændingen grå mod sort ved naboens klemme** med den ny sonde i fri luft. Giver den ca. 7 mV ligesom hos os, ligger forskellen i styringen (firmware eller hardware). Giver den ca. 25 mV, ligger forskellen i vores ledninger, stel eller sondeinstallation.
- **Afprøv den gamle sonde hos naboen** på samme måde.
- **Spørg KSM om en firmwareopdatering** fra 1.0.4309 til 1.0.5501 eller nyere, og om ændringer i iltindgangen mellem versionerne. Spørg også om *Fuel: PARAM ERR*, som naboen ikke har.

## Bilag: rå moduldata fra naboens servicebillede

Forsøgsvis tolkning: de 16-bit værdier i linje 2 (little-endian) ser ud til at være rå AI-værdier.

| Modul | Linje 2 | 16-bit værdier | Tolkning |
|---|---|---|---|
| 2 | `E5 00 F4 01 66 01 00 00` | 229, 500, 358, 0 | AI1 = ilt (229 passer med Test 219–240 på samme tid), AI2 = kedeltemp., AI3 = røgtemp. |
| 24 | `00 12 55 74 FF 01 04 06` | – | Ukendt |
| 30 | `E1 01 F6 01 09 02 FF 03` | 481, 502, 521, 1023 | PCT430: AI1 = retur temp., AI4 ikke tilsluttet (1023) |

Linje 0 er ens for alle moduler bortset fra et par byte (`04 02 00 01 0C …`), sandsynligvis modulets type og version.
