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
5. Hvad betyder **"Fuel: PARAM ERR"**, og hvordan rettes det?
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
