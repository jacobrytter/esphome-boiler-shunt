# Ventilregulator – returshunt til stokerfyr (ESPHome)

PI-regulator på en [LilyGO T-Display](https://wiki.lilygo.cc/products/t-display-series/t-display/) (ESP32; skærmen bruges ikke), der styrer en motoriseret 3-vejs ventil (HH-Flo A20-T25-B3-C, 230 VAC, 3-leder) i kedlens retur. Målet er at holde returtemperaturen til kedlen på mindst sætpunktet (typisk 55 °C).

| Ventil | Betydning |
|---|---|
| 0 % | Shunt lukket. Alt returvand fra huset går til kedlen (normal drift, varmt hus). |
| 100 % | Shunt helt åben. Kedlens fremløb går direkte tilbage i kedlen (opstart, kold kedel). |

Ved opstart står shunten helt åben, fordi returen er kold. Når kedlen varmer op, og blandetemperaturen passerer sætpunktet, lukker regulatoren gradvist shunten og lukker husets returvand ind. Til sidst står shunten helt lukket.

## Filer

| Fil | Indhold |
|---|---|
| [ventilregulator.yaml](ventilregulator.yaml) | ESPHome-konfiguration: pins, WiFi, indstillinger og sensorer |
| [shunt.h](shunt.h) | PI-regulatoren og motorstyringen (ren C++) |
| [secrets.yaml](secrets.yaml) | WiFi-kodeord og krypteringsnøgle. Del den ikke. |

## Hardwarerevisioner

| | Revision 1 (nu) | Revision 2 (planlagt) |
|---|---|---|
| Stillingsføler | Potentiometer på akslen, analogt på GPIO 33 | AS5600 magnetisk vinkelføler, I²C på GPIO 21/22 |
| Motorafbryder | 3,3 V-relæmodul | Logic-level MOSFET-modul (LR7843 eller AOD4184) |
| Firmware | `ventilregulator.yaml` som den er nu | Tilpasses, når hardwaren er klar. Revision 1 skal fortsat kunne bruges. |

## Skærmen

Skærmen på boardet bruges ikke (den er defekt, og firmwaren styrer den ikke længere). Al betjening og aflæsning sker i webinterfacet og i loggen (se *Betjening* og *Fejlfinding*).

## Tilslutning

### Pins på T-Display

| Funktion | GPIO |
|---|---|
| DS18B20 data | 27 |
| Motorstrøm 5 VDC (MOSFET eller relæ) | 25 |
| Optokobler: retning | 26 |
| Stillingsføler rev 1 (potentiometer) | 33 (skal være ADC1, fordi ADC2 ikke virker med WiFi) |
| Stillingsføler rev 2 (AS5600, I²C) | 21 (SDA), 22 (SCL) |
| Skærm (fast på boardet, bruges ikke) | 4, 5, 16, 18, 19, 23 |
| Knapper (fast på boardet, bruges ikke) | 0, 35 |

### Motorventil (5 VDC)

Ventilen er en [HSH-Flo CR305, 3-vejs L-type](https://www.hhflo.com/collections/brass-three-way/products/hsh-flo-brass-3-way-l-type-ac110-230v-cr305-electric-motorized-ball-valve-3-wires-switching-control-valve-auto-return-when-power-off?variant=41321457451205), 90° på højst 5 s og 2 Nm. Den er bygget om:
- 230 V-delen og auto-retur er fjernet.
- Motoren kører direkte på 5 VDC og trækker 120–150 mA under kørsel.
- En optokobler på ventilens print vælger retning.

Anbefalet løsning på længere sigt: en logic-level N-MOSFET på minussiden.

```
 +5 V ──[sikring 500 mA]──┬────────────────► ventilens +5 V
                          │   ┌──|<|──┐      Schottky-diode (fx 1N5819) over ventilens forsyning,
                          │   └───────┘      katode mod +5 V (aflaster ved motorens udkobling)
                          └── 470 µF + 100 nF
                                             ventilens GND
                                                   │
                                                 Drain
 GPIO 25 ──[100 Ω]──────────────────────────── Gate   N-MOSFET, fx AO3400A
                       └──[100 kΩ]── GND       Source ── GND
                         (holder motoren slukket under opstart)

 GPIO 26 ──[330 Ω]──► optokobler LED +   (tændt = åbn shunt, se invert_dir)
 GND ───────────────► optokobler LED −
```

Hvorfor MOSFET frem for relæ eller SSR:
- **SSR er ikke relevant ved 5 VDC.** Et almindeligt SSR er lavet til vekselstrøm (triac).
- **MOSFET:** intet slid, ingen klik, og den skifter på mikrosekunder. Den trækker næsten ingen strøm fra GPIO'en, og komponenterne koster få kroner.
- **Den skal være "logic level"** og lede fuldt ved 2,5–3,3 V på gaten. Gode bud er AO3400A og IRLML6344 (SOT-23) eller færdige moduler med D4184/AOD4184. Undgå IRF520- og IRFZ44N-moduler: de kræver ca. 10 V på gaten og leder kun delvist ved 3,3 V.
- **En optokobler foran FET'en giver ingen ekstra sikkerhed** her, fordi motor og ESP bruger samme 5 V-forsyning og samme stel. Den giver kun mening med to adskilte forsyninger.
- **Et lille relæ er også fint.** Ved 5 V og 150 mA slides kontakterne stort set ikke. Ulempen er en spolestrøm på 70–100 mA og et klik ved hver bevægelse. Trækker relæmodulet ved LOW, så sæt `motor_inverted: "true"`.

- Softwaren sætter retningen 150 ms **før** motoren tændes og slipper den først 150 ms **efter**, at motoren er slukket. Retningen skiftes aldrig, mens motoren kører.
- Optokoblerens LED skal have ca. 5–10 mA. 330 Ω fra 3,3 V giver ca. 6 mA. Har ventilprintet allerede en formodstand beregnet til 230 V, skal den erstattes eller forbigås.
- Sæt 470 µF + 100 nF over motorforsyningen tæt på ventilen. Træk motorens GND for sig selv tilbage til strømforsyningen, så motorstrømmen ikke forstyrrer målingen fra stillingsføleren.
- Relæet bryder kun 5 V/150 mA og slides derfor næsten ikke. En logic-level MOSFET kan bruges i stedet, men et relæ adskiller ventilprintet fuldstændigt fra ESP'en.
- Kører ventilen den forkerte vej, så sæt `invert_dir: "true"`.

**Endestop:** Ventilens interne endestopkontakter slukker selv motoren. Softwaren ser, at endestoppet er nået, når stillingsføleren har stået stille i 1,5 s, og slukker så relæet.

**Dødtid og efterløb** læres automatisk:
- Dødtiden er tiden fra relæet tænder, til akslen drejer.
- Efterløbet er, hvor langt ventilen løber efter, at relæet er slukket. Det måles 1 s efter hvert stop, og motoren stopper derefter tilsvarende før målet.

Begge står i loggen og læres forfra efter en genstart.

### Strømforsyning

Én 5 V-forsyning på mindst 2 A, fx Mean Well IRM-10-5 (til print) eller HDR-15-5 (til DIN-skinne). I værste fald trækker det hele ca. 1 A. Undgå HLK-PM01 (0,6 A) og USB-opladere uden mærke.

```
 230 V ──[sikring]── 5 V-FORSYNING (2 A)
                      +5V ─┬──[polyfuse 0,5 A]──┬── 470 µF + 100 nF ──► motor / relæ / MOSFET
                           │                    └── Schottky over ventilen
                           └──[Schottky 1N5819]──► T-Display 5V-ben
                      GND ─┬──────────────────────── motorens GND (egen ledning)
                           └──────────────────────── T-Display GND (egen ledning)
```

- Motor og ESP har hver sin gren og egne stelledninger fra forsyningens klemmer. Så forstyrrer motorstrømmen ikke målingen fra stillingsføleren.
- Polyfusen beskytter mod kortslutning i motorgrenen, så ESP'en kører videre.
- Dioden i ESP'ens forsyning forhindrer strøm i at løbe ud i pc'ens USB-port, når USB er tilsluttet samtidig.
- Relæmodul: et 3,3 V-modul kan forsynes fra 3V3-benet. Et 5 V-modul med transistor-indgang er bedre, fordi spolen så belaster 5 V-grenen.
- Brug en lukket kasse (IP54) væk fra varme rør. 230 V-delen skal udføres forsvarligt.

### Stillingsføler

Firmwaren vælger føler i `ventilregulator.yaml` under `packages`: `hw/rev2.yaml` er AS5600 (standard), og `hw/rev1.yaml` er potentiometeret.

**Rev 1: potentiometer**

```
 3,3 V ──── yderben
 GND   ──── yderben
 GPIO 33 ── midterben ──┬── 100 nF ── GND   (støjfilter)
                        └── 1 MΩ   ── GND   (afbrudt ledning = 0 V = fejl)
```

- Forsyn fra **3,3 V og aldrig 5 V**. ADC-indgangen tåler højst 3,3 V.
- Monter det, så ventilens 90° vandring giver ca. 0,3–3,0 V. Under 0,1 V eller over 3,15 V opfattes som "afbrudt".
- Retningen er ligegyldig, da kalibreringen finder ud af det.

**Rev 2: AS5600 over I²C (magnetisk, uden slid)**

```
 AS5600   T-Display
 VCC ──── 3,3 V   (aldrig 5 V)
 GND ──── GND
 SDA ──── GPIO 21
 SCL ──── GPIO 22
 DIR ──── GND     (må ikke svæve)
 OUT, GPO: ikke forbundet
```

- 100 kHz med 4,7 kΩ pull-up til 3,3 V (sidder normalt på modulet). Brug snoet ledning (SDA med GND og SCL med GND), ca. 20 cm, og hold den væk fra motorledningerne.
- Magneten kan sidde i en vilkårlig vinkel på akslen. Firmwaren bruger den rå vinkel (0–360°), og kalibreringen lærer endepunkterne. Spring-punktet ved 360° håndteres i softwaren.
- Diagnose i webUI'en: **Position sensor magnet** (OK / Too weak / Too strong / Not detected / No answer on I2C), **Position sensor gain** (automatisk forstærkning, 0–128 ved 3,3 V; midt i området betyder en passende luftspalte, højt betyder for langt væk, lavt for tæt på) og **Position sensor magnitude** (feltstyrke). **Position sensor** viser vinklen i grader.
- Når føleren er monteret og *Position sensor magnet* viser OK: tryk **Valve calibrate**.
- Magneten skal være **diametralt magnetiseret**, dvs. nord og syd på hver sin halvdel af den runde side. Magneter med forsænket skruehul er næsten altid aksialt magnetiseret og virker **ikke**. Køb helst et AS5600-modul med magneten i pakken. Sådan testes en magnet: læg den med fladen opad under et kompas, eller brug en magnetometer-app, og drej magneten rundt. Følger kompasnålen med rundt, er den diametral.
- Luftspalten mellem magnetens flade og chippens overflade skal være 0,5–3 mm, og helst ca. 1–1,5 mm. For en typisk magnet på 6 × 2,5 mm svarer det til ca. 3–3,5 mm fra centrum af chippen til centrum af magneten.
- Magneten skal sidde centreret over chippen. Den må højst være forskudt ca. 0,25 mm til siden.
- Brug ikke-magnetiske materialer tæt på magneten: messing, aluminium, plast eller A4-rustfrit.

### DS18B20

```
 3,3 V ───┬──────────── VDD  (rød)
          │
        4,7 kΩ   pull-up, tæt ved ESP'en
          │
 GPIO 27 ─┴──────────── DATA (gul, hvid eller blå)
 GND ────────────────── GND  (sort)
```

- **Forsyning:** DS18B20 kører på 3,0–5,5 V. **Brug 3,3 V**, så datalinjen aldrig kommer over 3,3 V, som ESP32 højst tåler. Brug normal tilslutning med VDD forbundet, ikke "parasit"-tilslutning, hvor VDD sættes til GND.
- **Pull-up på 4,7 kΩ er nødvendig.** ESP32's interne pull-up er på ca. 45 kΩ. Det er for svagt til 1-Wire, og den bliver ikke stærkere af at bruge en anden indgang. Indgangene GPIO 34–39 har slet ingen intern pull-up. Over 5 m kabel bruges 2,2–3,3 kΩ.
- **Nemmeste løsning:** et "DS18B20 adapter"-modul med skrueterminaler. Det har pull-up-modstanden og ofte en lille kondensator indbygget.
- **Ledningsfarver** varierer mellem fabrikanter. Rød er næsten altid VDD og sort GND. Er du i tvivl, så mål eller find databladet.
- **Kabel:** brug snoet ledning, og hold den væk fra motorledningerne. Sæt gerne 100 nF mellem VDD og GND ved føleren.
- **Adressen findes automatisk.** Ved hver opstart søges der efter føleren, og den fundne adresse gemmes i flash. Den kan ses under *Temperature sensor address* i webinterfacet. Svarer føleren ikke ved opstart, bruges den gemte adresse, og målingen kommer af sig selv, når føleren svarer igen. En ny føler bliver fundet og gemt ved næste opstart.
- **Flere følere** kan sidde på samme ledning, fx også en på kedlens fremløb. Hver føler har sin egen adresse, og så skal adressen angives i YAML.

Føleren skal have god termisk kontakt. Den bedste løsning er en dykrørslomme. Alternativt spændes den fast på røret med varmeledningspasta og isoleres godt udenpå.

**Placering:** 10 cm efter ventilen er kort. Det varme og det kolde vand er ikke nået at blande sig, så føleren kan måle en "stribe" af det ene. Det giver en urolig og misvisende måling. Sæt den helst 30–50 cm efter ventilen, efter en bøjning eller efter cirkulationspumpen, som blander godt.

## Installation

ESPHome er installeret i et virtuelt Python-miljø i `%LOCALAPPDATA%\ESPHome\venv`. Det kræver ikke administrator. Genvejen `%LOCALAPPDATA%\ESPHome\bin\esphome.cmd` ligger i bruger-PATH, så `esphome` virker i alle terminaler uden at venv'ets `python` kommer i PATH.

Opsætning på en ny pc (Python 3.12 installeres kun til brugeren):

```powershell
winget install --id Python.Python.3.12 --scope user
& "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe" -m venv "$env:LOCALAPPDATA\ESPHome\venv"
& "$env:LOCALAPPDATA\ESPHome\venv\Scripts\python.exe" -m pip install esphome
$bin = "$env:LOCALAPPDATA\ESPHome\bin"; New-Item -ItemType Directory -Force $bin
Set-Content "$bin\esphome.cmd" -Encoding ascii -Value '@"%LOCALAPPDATA%\ESPHome\venv\Scripts\esphome.exe" %*'
[Environment]::SetEnvironmentVariable('Path', [Environment]::GetEnvironmentVariable('Path','User') + ";$bin", 'User')
```

Build-mappen ligger i `C:\ProgramData\ESPHome\build` (`build_path` i YAML) og fylder flere hundrede MB. Projektmappen får kun en lille `.esphome`-cache på ca. 4 MB, som ikke kommer med i Git.

### Kildekode og flere pc'er

Projektet ligger i det private repository https://github.com/jacobrytter/esphome-boiler-shunt og hentes til `C:\Users\jr\src\esphome-boiler-shunt` (samme sti på alle pc'er):

```powershell
git clone https://github.com/jacobrytter/esphome-boiler-shunt.git C:\Users\jr\src\esphome-boiler-shunt
```

`secrets.yaml` er **ikke** i Git og skal kopieres ind i mappen for sig. Kør `git pull`, før du går i gang, og commit og `git push`, når du er færdig.

Kommandoerne køres fra projektmappen:

| Kommando | Funktion |
|---|---|
| `esphome compile ventilregulator.yaml` | Kompilér |
| `esphome upload --device ventilregulator.local ventilregulator.yaml` | Læg firmware ind trådløst (eller `--device 192.168.4.188`) |
| `esphome upload --device COM4 ventilregulator.yaml` | Læg firmware ind via USB |
| `esphome logs --device ventilregulator.local ventilregulator.yaml` | Vis loggen over WiFi |

ESPHome opdateres med `& "$env:LOCALAPPDATA\ESPHome\venv\Scripts\python.exe" -m pip install -U esphome`

## Fejlfinding

Loggen kan ses tre steder på samme tid:
- **Seriel port** (COM4, 115200 baud), fx med udvidelsen *Serial Monitor* i VS Code.
- **Over WiFi** med `esphome logs …`.
- **I browseren** på `http://ventilregulator.local`, også fra mobilen. Loggen står nederst på siden.

Firmware lægges ind trådløst, så Serial Monitor kan stå åben imens. Kun programmering via USB kræver, at COM4 er fri.

Loggen indeholder to slags linjer fra `shunt`:
- **Statuslinje hvert 10. sekund:** temperatur, sætpunkt, P- og I-led, stilling, følerværdi (mV eller grader) og endepunkter, indlært efterløb og dødtid, status og antal motorstarter.
- **Hændelser:** hver motorstart og hvert stop med årsag og varighed, efterløbet efter hvert stop, lærte endepunkter, kalibrering og følerfejl.

## WiFi-opsætning

ESPHome understøtter **ikke WPS**. Der er tre andre muligheder, og du kan bruge dem alle.

**A. I kildekoden:** skriv netværksnavn og kodeord i `secrets.yaml`.

**B. Fra mobilen via nødhotspot:** kan regulatoren ikke forbinde inden for 5 minutter, opretter den hotspottet **"Shuntventil"** (kodeord `ap_password` fra `secrets.yaml`). Forbind til det med mobilen. En side åbner af sig selv (ellers gå til `http://192.168.4.1`), hvor du vælger dit netværk og skriver kodeordet.

**C. Via USB fra browseren:** tilslut boardet og åbn https://web.esphome.io i Chrome/Edge. Vælg *Connect* og derefter *Configure WiFi*.

Vil du udelukkende bruge B eller C, så slet de to linjer `ssid:` og `password:` under `wifi:`. Ellers overskriver værdierne fra `secrets.yaml` det valgte netværk, hver gang der lægges ny firmware ind.

### Når WiFi falder ud

1. ESPHome forsøger straks at forbinde igen og bliver ved.
2. Efter 5 minutter uden forbindelse åbnes hotspottet "Shuntventil" (`ap_timeout` under `ap:`). Imens forsøger den stadig at forbinde til dit netværk hvert 30. sekund.
3. Når netværket er tilbage, forbinder den og lukker hotspottet.

Den genstarter **aldrig** på grund af manglende WiFi eller Home Assistant (`reboot_timeout: 0s` begge steder). ESPHome genstarter ellers som standard efter 15 minutter. Reguleringen kører uforstyrret hele tiden.

Vil du slet ikke have et hotspot, når først WiFi er sat op, så slet hele `ap:`-blokken og `captive_portal:`. Så prøver den bare igen og igen.

## Betjening

Alt kan styres fra webinterfacet `http://ventilregulator.local` (eller IP-adressen) og fra Home Assistant, hvor enheden dukker op automatisk.

WebUI'en viser enhederne i grupper: **Overview**, **Valve**, **Position sensor**, **Controller tuning**, **Temperature sensor** og **System**. Navnene begynder med gruppens navn, så de også står samlet i Home Assistants alfabetiske lister.

| Enhed | Funktion |
|---|---|
| Status | AUTO, MANUAL, MANUAL MOTOR, CALIBRATING, SENSOR FAULT eller VALVE FAULT |
| Mode | **Auto** (regulering), **Manual position** (ventilen kører til *Manual shunt position*) eller **Manual motor** (motoren styres direkte med *Valve motor* og *Valve direction*) |
| Setpoint | Ønsket minimum returtemperatur (standard 55 °C) |
| Manual shunt position | Stilling i *Manual position* |
| Valve direction | *Open* eller *Close* til *Manual motor* |
| Valve motor | Tænder motoren i den valgte retning. Skifter selv *Mode* til *Manual motor*. Slukker selv ved endestop og efter 1,5 × gangtiden. Skift af retning undervejs stopper motoren og starter den i den nye retning. |
| Valve calibrate | Kører helt åben og derefter helt lukket, lærer stillingsfølerens endepunkter og måler gangtiden. Slutter med shunten lukket. |
| Valve travel time 0-100 % | Måles automatisk af kalibreringen |
| Valve deadband | Mindste stillingsændring, før motoren kører (standard 2 %) |
| Controller Kp, Controller Ti | Regulatorparametre (se nedenfor) |

Alle indstillinger gemmes i flash. *Manual motor* gemmes ikke: efter en genstart står *Mode* på Auto.

## Idriftsættelse

1. **Tør test uden 230 V på motoren.** Tjek at relæerne klikker, og at temperaturen vises i webinterfacet.
2. **Retning.** Sæt *Valve direction* til Open, og tænd *Valve motor*. Ventilen skal nu åbne for shunten, og *Position sensor* skal ændre sig. Kører den forkert, så sæt `invert_dir: "true"`.
3. **Kalibrering.** Tryk *Valve calibrate*, og vent til *Valve calibration* viser "Calibration OK".
4. Sæt *Mode* til Manual position, og prøv 25, 50 og 75 %. *Shunt position* skal følge med inden for et par procent. Tjek samtidig, at ventilen faktisk blander gradvist.
5. Sæt *Mode* til Auto.

## Indregulering (Kp og Ti)

Udgangspunktet er **Kp = 4 %/°C og Ti = 150 s** med regulering hvert 10. sekund. Det er bevidst forsigtigt. (Ti var oprindelig 300 s, men føleren sidder 120 cm efter ventilen bag pumpen, så dødtiden er kort.)

Brug **dataloggen** på `http://ventilregulator.local/datalog` til at bedømme reguleringen: grafer over de sidste 4 timer (returtemperatur, sætpunkt, stilling, mål og I-led hvert 10. sekund) og motorstarter pr. 10 min og pr. time. Rådata kan hentes som `datalog.csv` og `starts.csv` (fx til Excel). Loggen ligger i RAM og starter forfra efter en genstart.

- **Kp** = hvor mange procent ventilen flytter sig pr. grad afvigelse. Et større tal giver hurtigere og mere urolig regulering.
- **Ti** = hvor hurtigt den resterende afvigelse fjernes. Et mindre tal giver hurtigere regulering, men mere oversving.

Fremgangsmåde med kurverne i dataloggen eller i Home Assistant:

1. **Temperaturen svinger op og ned**: halvér Kp. Hjælper det ikke, så fordobl Ti.
2. **Den er langsom og ligger længe under/over sætpunktet**: øg Kp med ca. 50 % ad gangen.
3. **Den rammer tæt på, men kryber langsomt ind**: gør Ti mindre (fx 150 → 100 s).

Det er normalt, at shunten står på 100 % i starten af en opfyring og på 0 %, når huset er varmt.

## Fejlhåndtering

| Situation | Hvad regulatoren gør |
|---|---|
| Føler afbrudt 3 målinger i træk | *FØLERFEJL*. Shunten lukkes helt, så kedlen altid kan komme af med varmen. Sæt `fault_hold_position: "true"`, hvis ventilen i stedet skal holde sin stilling. |
| Stillingsføler afbrudt/ukalibreret | Kører videre med stillingen estimeret ud fra kørselstid. *Position sensor OK* bliver slukket. |
| Motor kører, men akslen flytter sig ikke | *VENTILFEJL* |
| Genstart med kalibreret stillingsføler | Fortsætter fra den aktuelle stilling |
| Genstart uden stillingsføler | Lukker shunten helt og starter forfra |
| WiFi forsvinder | Reguleringen er upåvirket |

## Kendte udfordringer

| Udfordring | Hvad der er gjort / hvad du kan gøre |
|---|---|
| Auto-retur ved strømsvigt | Skal kobles fra, ellers kan ventilen ikke stå i mellemstilling (se *Motorventil*). Efter et strømsvigt står ventilen, hvor den var, og regulatoren fortsætter derfra. |
| Ingen manuel overstyring | Monter omskifteren AUTO / ÅBEN / LUK (se *Manuel omskifter*). |
| Føler for tæt på ventilen | Flyt den 30–50 cm længere ned, eller efter pumpen. |
| Motorslid | On/off-motorer er ikke bygget til tusindvis af starter. Tjek *Valve motor starts* i webinterfacet efter et døgn. Over et par hundrede starter i døgnet bør dødbåndet hæves eller Kp sænkes. |
| Relæslid | Relæ 1 bryder motorstrømmen. Brug RC-led over kontakten. |
| Varme ved ventilen | Motor og potentiometer sidder på et rør på op til ca. 90 °C. Brug et potentiometer, der tåler varmen, og placer ESP'en og relæerne væk fra rørene. |
| Kugleventilens karakteristik | Flowet ændrer sig ikke lineært med stillingen. Regulatoren kan derfor virke rolig i ét område og nervøs i et andet. Indregulér ved de stillinger, hvor den typisk arbejder. |
| Hurtig ventil | CR305 kører 90° på højst 5 s, så 1 % svarer til 50 ms. Motorstyringen kører derfor hver 20 ms. Stopper ventilen konsekvent forbi målet, så hæv `STOP_LEAD` i shunt.h. |
| Kedlen uden aftag | Står shunten på 100 %, får huset intet vand. Det sker kun, når kedlen er kold. Kedlens egen termostat og overkogssikring skal altid kunne klare sig selv. |

## Sikkerhed

- 230 V-delen skal udføres forsvarligt: lukket kapsling, sikring, trækaflastning og adskillelse mellem lavvolt og 230 V. Er du i tvivl, så få en elinstallatør til at tilslutte.
- Regulatoren er **ikke** en sikkerhedsanordning. Kedlens egen termostat, overkogssikring og sikkerhedsventil skal fungere uafhængigt af den.
- En kugleventil med on/off-motor er ikke lavet til konstant regulering. Hold øje med slid, og hæv dødbåndet, hvis motoren starter meget ofte.
