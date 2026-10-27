# PCT400 I/O-print (modul 2) – virkemåde, fund og fejlsøgning

Samler det, vi ved om KSM/Pricatech PCT400's I/O-print, hvor iltsondens indgang sidder. Baggrunden (iltsonden, simulatoren, samtaler med KSM) står i [ksm-oxygen-sensor-notes.md](ksm-oxygen-sensor-notes.md) og [ksm-oxygen-sensor-neighbour.md](ksm-oxygen-sensor-neighbour.md).

Status 2026-10-27: **iltindgangen på vores print driver** (ca. 8 mV på få timer til et døgn ved fast indgangsspænding). Fyret er stoppet. Et lånt print fra naboen (Rev. E, 07-01-2013) er klar til at blive prøvet.

## Systemets opbygning

- **Display/CPU-enhed:** PIC32 MX575F512H, firmware (vores 1.0.4309, naboens 1.0.5501). Den regner rå ADC-værdier om til ilt %, temperaturer osv. og regulerer fyret.
- **I/O-modul(er) på CAN-bus:** hvert modul har sit nummer. Vores iltindgang er `02. 1.AI` = modul 2, analog indgang 1 (*Menu / … / Kedel / Ilt*, *Test* viser den rå ADC-værdi). Naboen har desuden modul 30 (PCT430-udvidelse med returtemperatur på `30.1.AI`).
- I/O-tildeling hos naboen (*Kedel*-menuen): Ilt 02.1.AI, Kedeltemperatur 02.2.AI, Røgtemperatur 02.3.AI, Retur temp. 30.1.AI, Varmstoker temp. 1 00.4.AI, Stokersnegl 1 02.1.DO, Blæser 1 02.4.DO.

## Komponenter på I/O-printet

Se [billederne](#billeder).

| Komponent | Funktion (sandsynlig) |
|---|---|
| **PIC18F25K80** (QFN-28, med krystal), øverst på printet | Modulets mikrocontroller. Har **indbygget ECAN-controller** og ADC. Der er ingen separat CAN-controller. |
| SO-8-kreds ved RJ45-stikket | CAN-transceiver. RJ45 + 4-polet stik = CAN-bus og forsyning til display og andre moduler. |
| 5-polet stiftliste mærket "P+T" | ICSP-programmeringsstik til PIC'en |
| **2 × ST LM324** (quad op-amp, mærket "324 GZ4C053" på det lånte print, "324 GZ13634" på vores) | Analoge indgangstrin. Det **højre** (nærmest klemme 21–24) er sandsynligvis iltforstærkeren. Det **venstre** betjener ifølge brugeren klemme 18–20 (kedeltemperatur). |
| MOC3043 + triacs (D²PAK), sikringer T250 mA, X-kondensatorer | 230 V-udgange (snegle, blæser osv.) med nulgennemgangstænding |
| Optokoblere "181" (TLP181-type) | Digitale indgange |
| 2 × 22 µH-spoler ("220 L2D"), 10 µF-elektrolytter, dioder "B4 GN1M" | Små switch-mode-forsyninger / ensretning (ikke fulgt) |

### Klemmer

- Klemmerækken langs printets nederste kant er stikbar (grønne stik), nummereret fra venstre: …, 11–14, 15–17, 18–20, **21–24**.
- **Klemme 21–24 = det sidste firepolede stik, klemme 24 længst ude mod kanten.** Iltsonde ifølge eldiagram ED 0415C-4: 21 grå, 22 hvid, 23 sort, 24 hvid. Grå/sort = signal, de hvide = varmelegeme (13 V~).
- **Klemme 18–20 = kedeltemperatur** (brugerens iagttagelse 2026-10-27), forbundet til det venstre LM324.
- På bagsiden har klemme 21–24 ingen spor. Banerne løber på komponentsiden.
- I bagsidebilledet er alt spejlvendt: klemme 24 er længst til venstre.

### Aflæste modstandskoder (lånt print)

EIA-96-koder (to cifre + bogstav, 1 %) og 3-cifrede koder:

| Ved | Koder | Værdier |
|---|---|---|
| Højre LM324 | ca. 6 × `01D`, 5 × `222`, `45B` | 100 kΩ, 2,2 kΩ, 2,80 kΩ |
| Venstre LM324 | flere `05B`, `01C`, `472`, `223`, `124`, `05E` | 1,10 kΩ, 10 kΩ, 4,7 kΩ, 22 kΩ, 120 kΩ, 1,10 MΩ |

Koderne kan stå på hovedet på billederne: `05B` ses som "850", `01D` som "0lD".

## Iltkanalen – forsøgsvis diagram

Banerne er ikke fulgt. Diagrammet bygger på komponenterne og måletallene.

```
21 grå ──R 2,2k──┬─────────────┐
                 │   LM324     │ 100k i tilbagekobling
23 sort ─R 2,2k──┴─ (diff.)  ──┴── RC ── PIC18F25K80 ADC ── CAN ── display (PIC32)
                     ▲
                bias ca. ½ Vref (giver ADC ≈ 512 ved 0 mV)
22/24 hvid: sondens varmelegeme (13 V~)
```

Måletal, der understøtter det:
- **Overføring:** ADC ≈ 498…512 − 15 · U[mV], målt med simulatoren. 0 mV giver ca. midten af 10-bit-området, altså bias på ½ Vref, så indgangen kan måle både plus og minus. Fortegnet er vendt.
- **Forstærkning:** 100 kΩ / 2,2 kΩ ≈ 45. Med en 10-bit ADC og en reference på ca. 3,0–3,3 V giver det 14–15,5 tællinger/mV (målt 14,7–15,2).
- **Åben indgang:** 5,6 mV uden sonde. Det passer med en "brudt sonde"-detektering, fx 1,1 MΩ fra forsyningen ind i ca. 2 kΩ. Den 1,1 MΩ, der er fundet, sidder dog ved det venstre LM324, så det er ikke afgjort.
- 8 mV forskydning ved indgangen svarer til ca. 0,37 V på op-amp'ens udgang.
- **Omregning i displayet** (efter vores kalibrering på simulatoren): ilt ≈ 20,9 % · exp(−(ADC − 180)/230). Den har ikke ændret sig, mens fejlen udviklede sig. Fejlen ligger altså før ADC'en.

## Fejlbilledet

| Tid | Simulator | ADC | Vist ilt |
|---|---|---|---|
| 2026-10-27, før besøg hos naboen | 13,0 mV | 301 | 12,34 % |
| 2026-10-27, sent aften | 12,9 mV | 427 | 7,10 % |

Den fulde måleserie står i [ksm-oxygen-sensor-notes.md](ksm-oxygen-sensor-notes.md).

- **Kedeltemperaturen passer** (PCT400 55,8 °C, vores DS18B20 i samme følerlomme 55,1 °C). Den går til det venstre LM324 (klemme 18–20).
- **Brugerens hypotese:** de to LM324 kan have **hver sin reference/bias**, fordi signalerne er vidt forskellige (mV-signal fra iltsonden, større signal fra temperaturføleren). Så kan iltkanalens bias drive uden at påvirke kedeltemperaturen. Det er sandsynligt.
- **Kandidater:**
  1. Iltkanalens bias/reference (deler eller buffer ved det højre LM324).
  2. En modstand eller kondensator i forstærker eller filter, der ændrer sig eller lækker.
  3. Et defekt LM324.
  4. Lækstrøm fra støv eller fugt ved indgangen (vores print er støvet).
- LM324's egen offset er typisk 2 mV (højst 7 mV), og dens temperaturdrift ca. 7 µV/°C. 8 mV på et døgn er altså en fejl, ikke normal drift.

## Instrukser

### A. Prøv det lånte print (hurtigste afgørende test)
1. Sluk på hovedafbryderen, og fotografér alle klemmer, før de trækkes af. Klemmerne er stikbare.
2. Spørg naboen, om printet skal have modulnummer sat (det skal være modul 2). Tjek bagefter i *Kedel*-menuen, at *Ilt* står som `02. 1.AI`.
3. **Kun simulatoren**:
   - Aflæs *Ilt / Test* ved 0 mV (grå kortsluttet til sort) og ved ca. 13 mV.
   - Gentag efter nogle timer og næste dag. Værdierne skal ligge stille (få tællinger).
4. **Sonden i fri luft** i mindst 5 min:
   - Kør *Service / Reset kalibrering* og derefter *Kalibrere O2*.
   - Forventet visning ca. 21 %. Hos naboen gav samme sonde ADC 113 ved 21 %.

### B. Gennemgangsmålinger til et rigtigt diagram (lånt print, uden strøm)
1. Mål med ohmmeter fra klemme 21, 22, 23 og 24 til hvert ben (1–14) på begge LM324. Notér forbindelse eller modstand i Ω.
2. Mål fra LM324 ben 4 (V+) og ben 11 (GND) til klemmerne og til stel ved et montagehul.
3. Hvis der er tid: følg det ben, der går mod PIC18F25K80, og find RC-filtret.

LM324-benforbindelser: udgange 1, 7, 8, 14. Indgange − / +: 2/3, 6/5, 9/10, 13/12. V+ = 4, GND = 11.

### C. Måling i drift på det gamle print

> **Advarsel:** Der er 230 V på triac-udgangene, MOC3043 og sikringerne. Mål kun på LM324's ben, med isolerede prøvespidser.

1. Sæt simulatoren fast på fx 10–13 mV. Mål udgangene 1, 7, 8 og 14 på det højre LM324 mod ben 11.
2. Skru lidt på simulatoren. Den udgang, der flytter sig ca. 45 mV pr. mV, er iltkanalen.
3. Log den udgang, dens to indgangsben og ben 4 over nogle timer:
   - **Udgangen driver, men indgangene og V+ ligger stille:** forstærkeren eller dens modstande/kondensatorer.
   - **Biasbenet (+-indgangen) driver:** referencen/biassen.
   - **Indgangsbenene driver ved fast simulator:** lækstrøm før forstærkeren (rens med isopropylalkohol).
4. Sammenlign med samme ben på det venstre LM324 (kedeltemperatur) for at afgøre, om de har hver sin reference.

### D. Fotos, hvis der skal flere
- Printet ude, taget **lige oppefra** med diffust lys uden blitz: området fra det firepolede stik (21–24) op til begge LM324, for- og bagside.
- Samme område **med lys bagfra**: en kraftig lampe bag printet, så banerne ses som mørke streger.
- Montagehullerne skal være med i billedet, så billederne kan skaleres og lægges oven på hinanden.

## Billeder

I [images/](images/), nedskaleret til højst 2400 px.

**I/O-print (lånt print fra naboen, Rev. E 07-01-2013):**
- [io-board-top-mcu.jpg](images/io-board-top-mcu.jpg): PIC18F25K80, krystal, ICSP-stik "P+T"
- [io-board-top-right-half.jpg](images/io-board-top-right-half.jpg): højre halvdel med RJ45, 12-polet stik, LM324'erne, triacs og klemmer
- [io-board-top-analog-closeup.jpg](images/io-board-top-analog-closeup.jpg) og [io-board-top-analog-corner.jpg](images/io-board-top-analog-corner.jpg): de to LM324 med modstande, optokoblere, spoler
- [io-board-bottom-overview.jpg](images/io-board-bottom-overview.jpg) og [io-board-bottom-terminals-15-24.jpg](images/io-board-bottom-terminals-15-24.jpg): bagsiden (spejlvendt; klemme 24 længst til venstre)

**Vores gamle print i fyret:**
- [old-io-board-in-boiler-terminals.jpg](images/old-io-board-in-boiler-terminals.jpg): klemmerne nummereret 1–24, LM324 "GZ13634"

**Vores styring:**
- [own-pct400-info-screen.jpg](images/own-pct400-info-screen.jpg): ver. 1.0.4309, CAL 6 × 100, Fuel PARAM ERR
- [own-pct400-log-screen.jpg](images/own-pct400-log-screen.jpg): loggen (12,1 = sandsynligvis iltsætpunkt, 7,6 = målt ilt)

**Naboens styring (2026-10-27):**
- [neighbour-pct400-info-screen.jpg](images/neighbour-pct400-info-screen.jpg): ver. 1.0.5501, CAL 6 × 100
- [neighbour-o2-io-test-113-free-air.jpg](images/neighbour-o2-io-test-113-free-air.jpg), [neighbour-o2-io-test-219.jpg](images/neighbour-o2-io-test-219.jpg), [neighbour-o2-io-test-240.jpg](images/neighbour-o2-io-test-240.jpg): *Ilt / Test*
- [neighbour-main-screen-163C-14.7pct.jpg](images/neighbour-main-screen-163C-14.7pct.jpg): hovedskærm, røg 163 °C, ilt 14,7 %
- [neighbour-drift-screen.jpg](images/neighbour-drift-screen.jpg): driftsskærm (ydelse, PID, outputs)
- [neighbour-kedel-io-assignment.jpg](images/neighbour-kedel-io-assignment.jpg): I/O-tildeling
- [neighbour-module-list.jpg](images/neighbour-module-list.jpg), [neighbour-module-data-2.jpg](images/neighbour-module-data-2.jpg), [neighbour-module-data-24.jpg](images/neighbour-module-data-24.jpg), [neighbour-module-data-24-30.jpg](images/neighbour-module-data-24-30.jpg): rå moduldata (tolkning i [ksm-oxygen-sensor-neighbour.md](ksm-oxygen-sensor-neighbour.md))
