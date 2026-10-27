# Notat til samtale med KSM-Stoker – iltsonde på KSM Tuxedo F-20 Auto

Udarbejdet 2026-10-26. KSM-Stoker A/S, Karby: tlf. **97 76 10 72**, mail **mail@ksm-stoker.dk**.

## Anlægget

| | |
|---|---|
| Kedel | KSM Tuxedo 20 F-Auto (flis, silo/fødning) |
| Styring | PCT400-001-001-TUXFAUTO, ver. **1.0.4309** (CTS 2019-08-12 11:36), bootloader rev. 1539, MX575F512H |
| Cust. ID / CFG ID | 1 / 1 |
| MAC | 68-27-19-02-57-3C (IP 169.254.1.1 – ikke sat op) |
| Infoskærm | **CAL: 100,100,100,100,100,100** og **Fuel: PARAM ERR** |
| Ur | Ikke indstillet (D09010104 – loggen viser 2009-01-01) |
| Iltsonde | Indgang: **Modul 2 I/O 1** (analog). Klemmer 21–24 ifølge ED 0415C-4: grå, hvid, sort, hvid. Varmelegeme **13 VAC** |

## Forløbet

1. **Gammel sonde** (H&S 118.02-7100 = NGK OZA685-WW1, afløser for Bosch LSM 11) viste 11,2 % i fri luft. Målt grå mod sort: **7,2 mV i luft og 6,5 mV i røggas**, altså næsten ingen reaktion (0,7 mV). Sonden var varm. Vurderet defekt.
2. I mellemtiden kørt med simuleret signal (batteri + spændingsdeler) og kalibreret styringen på simuleringen: **21 % ved 13,7 mV**, 11,5 % ved 1,25 mV, 13,5 % ved 4,8 mV. Det passer præcist med Nernst-ligningen (21 mV hældning), så **styringens omregning virker**.
3. **Ny sonde** (samme type) monteret. Varm (kan ikke røres), 13 VAC over varmelegemet.
   - Fri luft, stabil efter længere tid: **6,8–6,9 mV** (grå mod sort).
   - Røggas, lav last: **−0,7 til 4,3 mV**, varierer med røgudviklingen. Sonden reagerer altså (fald på 2,6–7,6 mV ≈ 14–18 % O₂ ved lav last, hvilket passer med iltsætpunkt ca. 14 % ved 0 % ydelse).
   - Styringens indgang uden sonde: **5,6 mV**. Vekselspænding på signalet: **< 0,1 mV AC**.
4. **Kalibrering virker ikke** med den ny sonde. Displayet viser **7,64 % i fri luft** og **0 % ved kraftig røg**. CAL-linjen står uændret på 100,100,100,100,100,100.

5. **Test hos naboen (2026-10-27):** den ny sonde viser 21 % i fri luft på naboens styring (firmware 1.0.5501), men ved ADC 113 i stedet for vores 412. Se [ksm-oxygen-sensor-neighbour.md](ksm-oxygen-sensor-neighbour.md).

6. **Drift (2026-10-26/27):** kun simulatoren er tilsluttet (sonden er taget af). Spændingen er målt i stikket ca. 20 cm fra sonden, samme metode hele tiden. Den viste ilt falder gradvist ved uændret indgangsspænding: to gange på et døgn er den faldet til 8–9 %, og spændingen har måttet skrues op for at holde ca. 12 %. Ellers stopper fødningen, og falsk luft gennem sneglen giver risiko for tilbagebrænding. **Fyret er stoppet og slukket 2026-10-27.**
   - Måleserie med simulatoren (*Ilt / Test*), før besøget hos naboen:

     | Simulator | ADC | Vist ilt |
     |---|---|---|
     | 11,2 mV | 324 | – |
     | 11,9 mV | 315 | – |
     | 13,0 mV | 301 | 12,34 % |
     | 10,4 mV | 340 | 10,52 % |
     | 6,9 mV | 394 | 8,28 % |
     | 5,1 mV | 421 | 7,32 % |
     | 10,5 mV | 343 | 10,41 % |

     Lineært: ADC ≈ 498 − 15,2 · U. Visningen følger 20,9 % · exp(−(ADC − 180)/230) inden for 0,15 procentpoint, dvs. en Nernst-formet kurve med ca. 15 mV pr. e-fold og 20,9 % ved ca. 21 mV.
   - Senere samme aften (efter besøget hos naboen): **12,9 mV → ADC 427 → 7,10 %**. Skiftet fra 13,0 mV → ADC 301 er ca. 126 tællinger (ca. 8 mV) ved næsten samme indgangsspænding fra simulatoren. Omregningen fra ADC til ilt er uændret (20,9 % · exp(−(ADC − 180)/230) giver 7,1 %), så **fejlen ligger i den analoge indgang før ADC'en**, ikke i softwaren eller kalibreringen.
   - I/O-printet (modul 2, med iltindgangen på klemme 21–24) har PIC18F25K80 og to ST LM324 operationsforstærkere ved de analoge indgange. LM324 har typisk 2 mV offset (højst 7 mV) og ca. 7 µV/°C drift, så 8 mV på et døgn er en fejl og ikke normal drift. Mistanke: komponentfejl, lækstrøm (støv/fugt) eller en defekt LM324.
   - Lånt print fra naboen: Rev. E, 07-01-2013.
7. **Samtale med Jens, KSM (teknik), 2026-10-27:** sonden giver ca. ±10 mV. Han mistænker **iltforstærkeren** og foreslår at skifte hovedprintet.

## I/O-printet (modul 2) – forsøgsvis blokdiagram

Ud fra fotos af komponentsiden (lånt print Rev. E, 07-01-2013, og det gamle print). Banerne er ikke fulgt, så diagrammet er en kvalificeret gætning ud fra komponenterne og de målte tal.

```
 Klemme 21-24 (sonde)                     Analog del (2 x LM324)                       PIC18F25K80
 ┌──────────────┐     ┌────────────┐   ┌──────────────────────┐   ┌──────────┐      ┌──────────────────┐
 │ 21 grå  (+)  ├──R──┤ beskyttelse├───┤ differensforstærker  ├───┤ RC-filter├──────┤ AN-indgang (ADC) │
 │ 23 sort (−)  ├──R──┤ + RC-filter├───┤ forstærkning ca. 50-70│   └──────────┘      │                  │
 │ 22/24 hvid   │     └────────────┘   │ inverterende         │                     │ ECAN-controller  │
 │ = varme 13 V~│                      │ + bias Vref/2        │◄── Vref/2 (deler/   │ (indbygget)      │
 └──────────────┘                      └──────────────────────┘     buffer)         └────────┬─────────┘
       ▲                                                                                     │ CANTX/CANRX
       └── stor modstand til bias ("brudt sonde"-detektering, giver 5,6 mV uden sonde)       ▼
                                                                                    ┌──────────────────┐
                                                                                    │ CAN-transceiver  │
                                                                                    │ (SO-8 ved RJ45)  │
                                                                                    └────────┬─────────┘
                                                                                             │ RJ45 + 4-polet stik
                                                                                             ▼
                                                                          Display/CPU (PIC32MX575F512H)
                                                                          omregner ADC → ilt % og regulerer
```

Hvad tallene siger om forstærkeren:
- ADC 512 ved 0 mV = midten af 10-bit-området, altså bias på Vref/2, så indgangen kan måle både positive og negative spændinger.
- −14,7 til −15,2 tællinger pr. mV. Med en ADC-reference på 5 V (4,88 mV/tælling) er forstærkningen ca. 72, og med 3,3 V ca. 48. Fortegnet er vendt (inverterende eller ombyttede indgange).
- 8 mV forskydning ved indgangen svarer til ca. 0,4–0,6 V på forstærkerens udgang.

Aflæste modstandskoder (lånt print, EIA-96 og 3-cifret): ved det højre LM324 (nærmest klemme 21–24) ca. 6 × `01D` = 100 kΩ, 5 × `222` = 2,2 kΩ og `45B` = 2,80 kΩ. Ved det venstre LM324: `05B` = 1,10 kΩ (flere), `01C` = 10 kΩ, `472` = 4,7 kΩ, `223` = 22 kΩ, `124` = 120 kΩ og `05E` = 1,10 MΩ.
- 100 kΩ / 2,2 kΩ giver en forstærkning på ca. 45–46. Med en 10-bit ADC og en reference på ca. 3,0–3,3 V giver det 14–15,5 tællinger pr. mV, hvilket passer med de målte −14,7 til −15,2. Det højre LM324 er derfor sandsynligvis mV-forstærkeren til iltsonden.
- 1,1 MΩ fra forsyningen ind i en indgang med ca. 2 kΩ til stel giver ca. 6 mV. Det passer med de 5,6 mV, der er målt på indgangen uden sonde ("brudt sonde"-detektering). Den sidder dog ved det venstre LM324, så det er ikke afgjort.
- Banerne fra klemme 21–24 går ikke på bagsiden (der er ingen spor ved de fire ben), så de løber på komponentsiden, delvis skjult af spolerne og fotograferet skråt.
- Kedeltemperaturen passer stadig (PCT400 55,8 °C mod vores DS18B20 55,1 °C, 2026-10-27). ADC-referencen og forsyningen er derfor sandsynligvis i orden, og fejlen ligger i selve mV-forstærkeren.

Kandidater til drift i offset: referencen Vref/2 (vil også flytte de andre analoge indgange, fx kedeltemperaturen), en modstand eller kondensator i forstærker eller filter, der ændrer sig eller lækker, en defekt LM324, eller lækstrøm fra støv/fugt ved indgangen.

Øvrige komponenter: 5-polet stiftliste mærket "P+T" (sandsynligvis ICSP-programmering af PIC'en), triac-udgange med MOC3043 (230 V, nulgennemgang), optokoblere (TLP181-type) til digitale indgange, sikringer T250 mA, to små switch-mode forsyninger (22 µH-spoler).

## Rå ADC-værdier (Modul 2 I/O 1)

| Spænding grå mod sort | ADC |
|---|---|
| 6,8 mV (fri luft) | 412 |
| 2,3 mV (røggas) | 478 |
| 0,8 mV (røggas) | 500 |

Lineært: **−14,7 tællinger pr. mV**, og **0 mV svarer til ADC 512** (midten af en 10-bit ADC). Indgangen er altså bipolær, ca. 68 µV/tælling, ca. ±35 mV område, og fortegnet er vendt (mindre ilt → højere ADC). En ideel sonde ville give ADC ≈ 512 i luft. Den ny sonde giver **412, dvs. 100 tællinger (6,8 mV) forskudt**. Den gamle gav næsten det samme (ca. 7–8 mV).

## Spørgsmål til KSM

1. **Hvad betyder CAL: 100,100,100,100,100,100**? Er det kalibreringstabellen, som *Reset kalibrering* nulstiller (Pricatech: "Nu er tabellen reset")?
2. **Hvor stor afvigelse accepterer *Kalibrere O2***? Afvises 6,8 mV / ADC 412 i fri luft som "for stor afvigelse"?
3. Er **6–8 mV i fri luft normalt** for NGK OZA685-WW1 / LSM 11-typen på PCT400? (Begge sonder giver det.) Kan det skyldes stel/jord, eller at sonden skal hænge **uden at røre metal** under kalibrering (står i Pricatechs vejledning)?
4. Er der **parametre for Modul 2 I/O 1** (offset, skalering, sondetype), der skal sættes efter sondeskift?
5. Hvad betyder **"Fuel: PARAM ERR"**, og hvordan rettes det? *(KSM vidste det ikke, 2026-10-27. Det er sandsynligvis brændselstælleren, som mangler afvejningen under Menu / Tilbehør / Transportsnegl (Afvejet mængde g., Køretid s.); se KSM-manualens afsnit 6.4.1 og 14.4. Den har intet med iltmålingen at gøre.)*
6. Kan vi få **servicemanual / parameterliste** til PCT400 ver. 1.0.4309 (KSM-versionen)?
7. Pricatechs **Menu / Drift / Stokersnegl / Manual = JA** (kørsel uden lambdasonde) **findes ikke i KSM's menu** (tjekket 2026-10-26). Hvordan kører man uden iltsonde på KSM-versionen? Indtil videre bruges en forskydningsspænding (batteri + spændingsdeler) i serie med sondens signal.
8. Hvilke **iltsætpunkter** anbefales til flis (manualen for P-Line: 8 % ved 100 %, 14 % ved 0 % ydelse)?

## Fundet i dokumentation og fora

- **Kalibrering** (Pricatech PCT400-vejledning, afsnit 19, og KSM-brugervejledning 6.9.6–6.9.7): hæng sonden i fri luft i 5 min **uden at røre metal** → *Menu → Service (tryk 5 gange) → Reset kalibrering* → *Kalibrere O2* → skal vise 20–21 %. "Hvis afvigelsen er for stor, kan den ikke kalibreres, og sonden bør skiftes." Kalibrér aldrig, mens fyret er i drift.
- **Kørsel uden sonde** (Pricatech afsnit 25): *Menu / Drift / Stokersnegl / Manual = JA*. Så bestemmer røgtemperaturen (*Opstart / Røg Temp. / Skift til Drift* = 60 °C) opstart/drift, og udluftning før pause styres af *Drift / Stokersnegl / Udluft s.* (30 s) i stedet for ilt %.
- **Ilt Alarm – Genstart påkrævet**: ilt % har låst sig / fejl på lambdasonde (Pricatech afsnit 35).
- **Bosch LSM 11 er udgået**. Erstatninger (fx Lambdapower LSM11B, Bosch LSF 4.2) kan bruges, "forudsat at regelelektronikken kan justere offset ved luft" (Knödler, tysk undersøgelse af erstatningssonder).
- **Fora** (stokerpro.dk): ingen tråde om netop KSM/PCT400-kalibrering. Andre mærker beskriver samme princip (kalibrering i frisk luft med kold røggas), og at en sonde, der ikke kan kalibreres, typisk er slidt.

## Dokumenter

Gemt i `Q:\Dokumenter\Møllegården\KSM Tuxedo F-20 Auto`:
- `KSM Tuxedo P-Line PCT400 brugervejledning.pdf` (KSM, 84 sider)
- `Pricatech PCT400 vejledning 2020.pdf` (66 sider) og `Pricatech PCT400 menuforklaring.pdf` (40 sider)
- `ED 0415C-1..4 UK.pdf` (eldiagrammer)

Kilder:
- [KSM Tuxedo P-Line brugervejledning (mmvvs.dk)](https://mmvvs.dk/wp-content/laes_mere/308_100_020_bruger.pdf)
- [Pricatech: Vejledning PCT 400](https://pricatech.dk/wp-content/uploads/2022/10/Vejledning.pdf)
- [Pricatech: Menuforklaring](https://pricatech.dk/wp-content/uploads/2022/10/Menu.pdf)
- [KSM-Multistoker 775-75 PCT 400 (ManualsLib)](https://www.manualslib.com/manual/1945911/Ksm-Stoker-Ksm-Multistoker-775-75-Pct-400.html)
- [Stoker Pro: F2 hele tiden (kalibrering, Baxi)](http://www.stokerpro.dk/viewtopic.php?t=23834)
- [Stoker Pro: Iltstyring slået fra (RTB)](https://stokerpro.dk/viewtopic.php?t=29946)
- [Stoker Pro: Ny med KSM 275-18](https://stokerpro.dk/viewtopic.php?t=32557)
- [Knödler: Undersøgelse af erstatningssonder for LSM11](https://knoedler-technologie.de/wp-content/uploads/go-x/u/b61e15e2-be42-4795-8df6-524c71e41210/2021-01-31-Untersuchung-Ersatzsonden-fuer-LSM11.pdf)
- [Lambdapower LSM11B datablad](https://secure.lambdapower.co.uk/datasheets/LSM11B-DE.pdf)
