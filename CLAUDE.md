# CLAUDE.md – Ventilregulator (returshunt til stokerfyr)

Denne fil samler projektets historik, beslutninger og status, så arbejdet kan fortsætte på en anden pc. Brugerdokumentation (ledningsdiagrammer, indregulering, fejlfinding) står i [README.md](README.md).

## Arbejdsregler

- **Alt koderelateret skrives på engelsk:** kode, identifikatorer, kodekommentarer, logbeskeder, UI-/entitetsnavne, filnavne, repository-navn og commit-beskeder. Det gælder `shunt.h`, `ventilregulator.yaml` og kommentarer i `secrets.yaml`. (Undtagelse indtil videre: enhedsnavnet `ventilregulator` og filnavnet `ventilregulator.yaml`, som ændres som en særskilt opgave, fordi enhedens adresse og Home Assistant-ID følger med.)
- **Samtalen med brugeren foregår på dansk**, og README.md er på dansk.
- Organisationens regler: metriske enheder, datoformat `yyyy-MM-dd HH:mm:ss`, aldrig `Co-Authored-By:` i svar.
- Reguleringen skal kunne køre **helt selvstændigt** på ESP'en, uafhængigt af WiFi, Home Assistant og fyrets controller.
- Revision 1-hardwaren (potentiometer + relæ) skal blive ved med at virke, når revision 2 (AS5600 + MOSFET) tilføjes.
- **Alle Python-pakker installeres i et virtuelt miljø**, aldrig globalt eller med `pip install --user`.
- Ændr ikke på firmwaren uden at kompilere den bagefter. Firmware lægges ind via OTA (se *Byg og flash*).

## Formål

PI-regulator på en ESP32, der styrer en motoriseret 3-vejs kugleventil i returen til et stokerfyr (KSM Toxido 20 F-Auto). Returtemperaturen til kedlen skal holdes på mindst sætpunktet (standard 55 °C).

- **Ventilstilling 0 %** = shunt lukket: alt returvand fra huset går til kedlen.
- **Ventilstilling 100 %** = shunt helt åben: kedlens fremløb blandes i returen (opstart, kold kedel).
- Ved opstart står shunten helt åben. Når blandetemperaturen passerer sætpunktet, lukker regulatoren gradvist op for husets returvand. Efter 30 min til flere timer står shunten helt lukket.
- Temperaturføleren (DS18B20) sidder **120 cm efter ventilens udløb, efter en cirkulationspumpe**. Vandet er fuldt opblandet ved føleren (brugerens oplysning 2026-10-24; tidligere stod her fejlagtigt 10 cm).

## Filer

| Fil | Indhold |
|---|---|
| `ventilregulator.yaml` | ESPHome-konfiguration: pins, WiFi, indstillinger (number/select/button), sensorer, intervaller |
| `hw/rev1.yaml`, `hw/rev2.yaml` | Package med stillingsføleren for hver hardwarerevision (rev 1: potentiometer på ADC, rev 2: AS5600 over I²C). Vælges under `packages:` i `ventilregulator.yaml`. Enheden kører rev 2. |
| `shunt.h` | PI-regulator + motorstyring som ren C++ (`namespace shunt`, global `shunt::ctl`). Ingen ESPHome-afhængigheder. |
| `datalog.h` | Datalog i RAM (`namespace datalog`, global `datalog::dlog`) og webhandler for `/datalog` (graf-side), `/datalog.csv` og `/starts.csv`. Bruger ESPHome's `web_server_idf`. |
| `secrets.yaml` | WiFi-SSID/kodeord, hotspot-kodeord (tomt = åbent), API-krypteringsnøgle. **Indeholder hemmeligheder; del den ikke.** |
| `README.md` | Brugerdokumentation på dansk |
| `.gitignore` | Oprettet af ESPHome (`/.esphome/`, `secrets.yaml`) |
| `.esphome/` | Lille ESPHome-cache (ca. 4 MB), ikke i Git. Den store build-mappe ligger uden for projektet (se nedenfor). |

Den tidligere Arduino/PlatformIO-version er slettet efter brugerens ønske.

### Repository og arbejdsgang
- Privat GitHub-repository: https://github.com/jacobrytter/esphome-boiler-shunt (gren `main`).
- Arbejdsmappe på alle pc'er: `C:\Users\jr\src\esphome-boiler-shunt`. Den gamle mappe `OneDrive\Claude\Ventilregulator` er kun et arkiv.
- Commits som "Jacob Rytter" <jr@pc-smeden.dk>, commit-beskeder på engelsk.
- `secrets.yaml` er i `.gitignore` og flyttes manuelt mellem pc'erne.
- Ved sessionens start: `git pull`. Ved sessionens slut: opdatér *Status* og *Historik* her, commit og push.

## Hardware

### Board
- **LilyGO T-Display** (ESP32-D0WDQ6 rev 1.0, 4 MB flash, ST7789 135×240), MAC `24:6f:28:7a:44:50`, USB-chip CP210x (har været COM4).
- Boardet er brugt og flere år gammelt. **Skærmen er død** (2026-10-18): fladkablets forbindelse til printet svigtede (svagt lys, "snestorm"). Displayet fra board 1 blev flyttet over og virkede kortvarigt, men loddeøerne blev beskadiget ved andet forsøg, og skærmen er opgivet. Brugeren bruger webUI'en i stedet. **Al skærmkode er fjernet fra firmwaren** (2026-10-24, brugerens ønske): `spi`, `font`, `color`, `display`, baggrundslyset (`ledc` på GPIO 4 + `light`), knappen på GPIO 35 og substitutionen `full_display`. Skal en skærm bruges igen, ligger den gamle konfiguration i Git-historikken (før commit "Remove the display").
- Det første board (CP210x på COM3) var defekt. ESP32'en svarede slet ikke over UART, heller ikke i download-tilstand. Det samme var sket ved en tidligere lejlighed. Dets display er brugt og ødelagt, så det kan ikke bruges til reservedele længere.
- IP-adresse på brugerens net: **192.168.4.188** (mDNS `ventilregulator.local`). WiFi-signal ca. -81 dBm (svagt).

### Pins

| Funktion | GPIO |
|---|---|
| DS18B20 (1-Wire, 4,7 kΩ pull-up til 3,3 V) | 27 |
| Motorstrøm 5 VDC (relæ i rev 1, MOSFET i rev 2), aktiv HIGH (`motor_inverted`) | 25 |
| Retning: optokobler i ventilen via 330 Ω (tændt = åbn shunt, `invert_dir`) | 26 |
| Stillingsføler rev 1 (potentiometer), skal være ADC1 | 33 (+100 nF og **1 MΩ til GND**) |
| Stillingsføler rev 2 (AS5600 via I²C, 100 kHz) | 21 (SDA), 22 (SCL) |
| Reserveret til energimålerens læsehoved (UART2) | 13 (RX), 17 (TX) |
| Skærm (fast på boardet, bruges ikke) | 4 (baggrundslys), 5, 16, 18, 19, 23 |
| Knapper (bruges ikke) | 0 (BOOT), 35 |

Undgå strapping-benene 0, 2, 5, 12 og 15 til eksternt udstyr.

### Motorventil
- HSH-Flo/HH-Flo CR305, 3-vejs L-type, messing, DN25 (mærkat: A20-T25-B3-C, CR305-AC110-230V, 500 mA). 90° på højst 5 s og 2 Nm. Ingen manuel overstyring.
- **Kuglen har to udboringer vinkelret på hinanden (L-boring).** Der sker ingen opblanding i yderområderne, og reguleringen tager kraftigt fat midt i området. Karakteristikken er altså stærkt ulineær: forstærkningen er størst i midten og næsten nul ved enderne. Målt vandring med AS5600: ca. 84° (284,0° = 0 %, 199,8° = 100 %), gangtid 4,7–4,8 s, efterløb ca. 2,5 %.
- **Ombygget af brugeren:** 230 V-delen og auto-retur er fjernet. Motoren kører direkte på **5 VDC og trækker 120–150 mA**. En **optokobler på ventilens print vælger retning** (aktiveret = drejer mod uret, ifølge brugeren). **De interne endestopkontakter sidder der stadig** og slukker motoren selv.
- Motoren har en vis dødtid ved start og et efterløb. Slør er ubetydeligt.

### Følere
- **DS18B20**, ROM-adresse `0x8200000078b1d228`. Den findes ved hver opstart, og adressen gemmes som fallback (se firmware).
- **Stillingsføler rev 1:** potentiometer på ventilakslen, yderben på 3,3 V/GND, slæber på GPIO 33. **1 MΩ til GND mangler endnu.** Uden den svæver indgangen på ca. 140–240 mV, softwaren tror det er en føler. (Det falske endepunkt på 142 mV, der lå i flash, forsvandt, da globals blev omdøbt til `g_end0`/`g_end100`.)
- **Stillingsføler rev 2 (i brug fra 2026-10-18):** AS5600 **digitalt via I²C**. Brugeren valgte den analoge OUT-løsning fra. Ledninger: VCC på **3,3 V (aldrig 5 V)**, GND, SDA → GPIO 21, SCL → GPIO 22, **DIR til GND**, OUT og GPO ikke forbundet. Firmwaren læser den rå vinkel (0–360°), så magneten kan sidde i en vilkårlig vinkel. Spring-punktet ved 360° håndteres af `delta()` i `shunt.h`. Diagnose: text sensor "Magnet" og sensorerne "AS5600 gain"/"AS5600 magnitude". Magneten skal være **diametralt magnetiseret** (magneter med skruehul er typisk aksiale og virker ikke). Luftspalte 0,5–3 mm, helst 1–1,5 mm. Det svarer til ca. 3–3,5 mm fra centrum af chippen til centrum af magneten for en magnet på 6×2,5 mm. Centrering inden for ±0,25 mm, og kun ikke-magnetiske materialer tæt på. Test af magnet: drej den under et kompas; følger nålen med rundt, er den diametral.

### Motorafbryder
- **Rev 1:** 3,3 V-relæmodul. Ved 5 V/150 mA slides kontakterne næsten ikke.
- **Rev 2:** logic-level MOSFET-modul. Brugeren har et modul med PC817-optokobler på indgangen (gaten drives fra 5 V-forsyningen), en glasdiode (sandsynligvis en zener, der beskytter gaten, **ikke** en diode over motoren) og en DPAK-MOSFET (FR120N/LR7843/AOD4184-klassen). LR7843 og AOD4184 er bedst. FR120N virker kun, fordi gaten får 5 V. **Sæt en Schottky (1N5819) over ventilens forsyning** med katoden mod +5 V.
- Et SSR er ikke relevant ved jævnspænding. En optokobler foran FET'en giver ingen gevinst, fordi alt deler samme 5 V og stel.

### Strømforsyning (anbefalet, ikke bygget endnu)
- Én 5 V-forsyning på mindst 2 A (Mean Well IRM-10-5 eller HDR-15-5). Spidsforbrug ca. 1 A.
- Hver sin gren og egen stelledning til motor og ESP ("stjerne"). Motorgren med polyfuse 0,5 A og 470 µF + 100 nF. Schottky 1N5819 i ESP'ens 5 V-forsyning, så strømmen ikke løber ud i pc'ens USB-port.
- Lukket kasse (IP54), væk fra varme rør. 230 V-delen skal udføres forsvarligt.

## Firmware

### ESPHome-konfiguration (`ventilregulator.yaml`)
- ESPHome 2026.9.1, ESP-IDF, `board: esp32dev`, `build_path: C:/ProgramData/ESPHome/build/ventilregulator`.
- Substitutions: `invert_dir`, `motor_inverted`, `fault_hold_position`.
- WiFi fra `secrets.yaml`, **`reboot_timeout: 0s`** for både WiFi og API (ESPHome genstarter ellers efter 15 min). Hotspot **"Shuntventil"** med `ap_timeout: 5min`, og ESPHome bliver ved med at forsøge at forbinde (hvert 30. s) mens hotspottet er åbent. `captive_portal`, `improv_serial`, `api` med kryptering, `ota` (beskyttet af API-nøglen, uden kodeord), `web_server` v3 med `local: true`. **WPS understøttes ikke af ESPHome.**
- Logger på DEBUG med sensor/text_sensor/binary_sensor på INFO og component på ERROR. Statuslinje hvert 10. s og hændelser fra `shunt`.
- **Entiteterne er grupperet** med `web_server: sorting_groups` (Overview, Valve, Position sensor, Controller tuning, Temperature sensor, System), og navnene har gruppen som præfiks, så de også står samlet i HA's alfabetiske lister (brugerens ønske 2026-10-24). Hver entitet har `web_server: {sorting_group_id, sorting_weight}`. Omdøbningen nulstillede de gemte værdier for Kp, Ti, travel og deadband (de stod på standard).
- Indstillinger (template numbers, `restore_value`): Setpoint (55 °C), Manual shunt position, Valve travel time 0–100 % (5 s), Valve deadband (3 %), Controller Kp (4 %/°C), Controller Ti (150 s). Select "Mode" (Auto / Manual position / Manual motor, gemmes som indeks; Manual motor sættes til Auto i `on_boot`). Select "Valve direction" (Close/Open, gemmes ikke). Switch "Valve motor". Knapper "Valve calibrate" og "System restart".
- **Manual motor** (`p.direct`, `p.jog_on`, `p.jog_open` i `shunt.h`, kørsel `RM_JOG`): "Valve motor" tænder motoren i den valgte retning og skifter Mode til Manual motor. Den slukker selv ved endestop (stilstand) og efter 1,5 × gangtid + 2 s. Retningsskift undervejs stopper motoren, og den starter igen i den nye retning efter hviletiden. Retning og motor følger de samme pauser som ellers (DIR_SETTLE, REST).
- Globals i flash: `g_end0`, `g_end100` (stillingsfølerens endepunkter i følerens enhed, NAN = ikke lært), `g_ds_addr` (uint64, seneste DS18B20-adresse).
- **Packages `hw/rev1.yaml` / `hw/rev2.yaml`:** hver package definerer substitutionerne `pos_unit`/`pos_decimals` og et `script` med id `read_position`. Scriptet sætter `shunt::ctl.cfg` (`SENSOR_POT` eller `SENSOR_AS5600`) og kalder `set_sensor()` med en måling i mV eller grader (NAN = ingen måling). Rev 2 tilføjer `i2c`, `as5600` (id `as5600_hub`), global `g_magnet` og text sensor "Magnet". Kun rev 2 lægges ind. Rev 1 tjekkes ved at kompilere en midlertidig kopi med `hw/rev1.yaml` og en anden `build_path`, som slettes bagefter.
- `on_boot` (priority -100): indlæser endepunkter, sætter parametre, finder DS18B20 på bussen (family code 0x28). Den fundne adresse bruges og gemmes. Findes ingen, bruges `g_ds_addr` via `set_address()`, så målingen kommer af sig selv, når føleren svarer igen. Diagnose: text sensor "Temperature sensor address".
- Interval 20 ms: kører scriptet `read_position`, kopierer indstillinger til `shunt::ctl`, kalder `update(millis())`, skriver `dir_out`/`motor_out`, sender hændelser til loggen, gemmer lærte endepunkter og målt gangtid.
- Interval 10 s: `tick(temperatur, 10 s)`, en sample til dataloggen og statuslinjen.
- **Datalog (`datalog.h`):** 1440 samples à 16 byte (4 timer hvert 10. s: tid, returtemperatur, sætpunkt, stilling, mål, I-led, flag, motorstarter) og 48 timebuckets med motorstarter. Ca. 25 kB RAM (RAM i alt ca. 42 %). Handleren registreres i `on_boot` med `web_server_base::global_web_server_base->add_handler()`; typerne ligger i `esphome::web_server_idf`. Graf-siden tegner selv på canvas uden eksterne biblioteker. Tidsstempler kommer fra `time: sntp` (id `sntp_time`) i formatet `yyyy-MM-dd HH:mm:ss`; uden ur vises oppetid. CSV sendes i bidder med `httpd_resp_send_chunk`. Sensorerne "Valve motor starts last hour" og "... last 24 h" kommer fra timebuckets.
- Sensorer til HA: Status, Return temperature, Shunt position, Shunt target, Valve calibration, Valve coast-down, Valve dead time, Valve motor starts, Position sensor (mV eller °, 0 %, 100 %), Position sensor magnet/gain/magnitude (rev 2), Controller P/I term, Temperature sensor address, System WiFi signal, System IP address. Binary: Temperature sensor fault, Valve fault, Position sensor OK. REST-adresserne følger navnene, fx `/select/Mode`, `/switch/Valve%20motor`.

### Regulator og motorstyring (`shunt.h`)
- **Skridtregulator (inkrementel PI, fra 2026-10-25):** hvert 10. s lægges `Δ = Kp·(e − e_forrige) + Kp·dt/Ti·e` til akkumulatoren `acc_` (ønsket ændring af stillingen, begrænset til `[−pos, 100 − pos]`, hvilket også er anti-windup). Når `|acc_|` ≥ dødbåndet (et retningsskift kræver `REVERSE_FACTOR` 1,5 × dødbåndet), kører motoren mod `pos + acc_`. Efter stop og efterløb trækkes den **faktisk målte** bevægelse fra `acc_`, så en bevægelse, der blev for lang eller kort, ikke korrigeres af sig selv – kun hvis temperaturen kræver det. Ingen P-spark ved start (e_forrige sættes til e), og `acc_` nulstilles ved manuel drift, følerfejl og manglende temperatur. "Shunt target" = pos + acc_, "I term" = target − P (kun til visning). **Positionsformen** (kør til `target_` og korrigér) bruges kun i Manual position, ved følerfejl (luk shunten) og ved opstart uden kendt stilling (`homing_`).
- **Hvorfor:** loggen 2026-10-25 viste 21 starter/time; halvdelen var korrektionspulser inden for 10 s, og 30 af 45 bevægelser skiftede retning. Brugeren afviste en fast ventetid (en for lang bevægelse er stadig for lang efter 30 s) og bad om en separat motorstyring, der cyklisk sigter efter regulatorens output. Ventilens **mindste skridt er ca. 3–4 %** (dødtid ca. 250 ms + efterløb ca. 2,8 %), så dødbåndet er sat til 3 %.
- **Temperaturen filtreres ikke** (bevidst: filtre giver forsinkelse, og et trin på 0,0625 °C × Kp 4 = 0,25 % ligger under dødbåndet). Gyldighedskontrol: NaN, uden for -50…125 °C, 85,0 °C uden forvarsel. SENSOR FAULT efter 3 ugyldige målinger. Ved følerfejl **lukkes shunten** (I nulstilles, så regulatoren starter fra lukket, når føleren svarer igen); med `fault_hold_position: "true"` holdes stillingen i stedet. Før første gyldige måling efter opstart holdes stillingen. Et median-filter over 3 kan overvejes, hvis målingen hopper i drift.
- **Motorstyring** (tilstandene IDLE → PREP → RUN → REST): retningen sættes 150 ms før motoren tændes og slippes 150 ms efter stop. Mindst 1 s hvile mellem pulser. Med kalibreret stillingsføler køres der til den målte stilling er nået (minus indlært efterløb). Uden føler køres der efter tid.
- **Stillingsføler (`SensorCfg`):** følerens enhed og grænser ligger i `cfg`, som YAML'en vælger: `SENSOR_POT` (mV, gyldig 100–3150, spænd ≥ 300, stilstand < 15 mV i 1,5 s, læring ved > 20 mV) og `SENSOR_AS5600` (grader, wrap 360, spænd ≥ 20°, stilstand < 0,5° i 0,5 s, læring ved > 1°). `delta(a, b)` giver korteste vej rundt ved wrap. Målingen glattes (0,3), og op til 5 manglende målinger i træk (`SENSOR_MAX_MISS`) ignoreres.
- **Endestop:** mål ≤ 0,5 % eller ≥ 99,5 % kører mod endestop med 25 % ekstra tid. Endestoppet regnes for nået, når føleren har stået stille i `cfg.stall_ms`, og endepunktet læres (gemmes kun ved ændring > `cfg.learn_delta`).
- **Kalibrering** (knap): kører helt åben og derefter helt lukket, lærer `end100`/`end0` og måler gangtiden på lukketuren. Slutter med shunten lukket.
- **Indlæring:** dødtid (fra motoren tændes til akslen drejer, glidende gennemsnit) og efterløb (måles 1 s efter hvert stop, 0–5 %). Læres forfra efter genstart.
- **VALVE FAULT**, hvis akslen står stille eller målet ikke nås i tide. Motorstarter tælles.
- **Sikker stilling = shunten lukket (0 %)** (brugerens krav 2026-10-24): en helt åben shunt kan ikke afsætte kedlens varme, heller ikke ved minimumsdrift. Alle fejl- og ukendte situationer skal derfor lukke shunten.
- **Genstart:** med kalibreret føler fortsættes fra målt stilling. Uden føler lukkes shunten helt.
- Hændelseskø (8 × 112 tegn) hentes af YAML med `next_event()` og skrives med `ESP_LOGI("shunt", ...)`.
- Vigtige konstanter: `MIN_PULSE_MS 150`, `DIR_SETTLE_MS 150`, `REST_MS 1000`, `STOP_LEAD 0.5`, `SENSOR_MAX_MISS 5`. Følergrænserne står i `SENSOR_POT` og `SENSOR_AS5600`.

## Byg og flash

Der arbejdes på flere pc'er. ESPHome installeres på hver pc for sig:
- **Pc 1 (den oprindelige):** `C:\Program Files\ESPHome\venv` (kræver administrator), `...\venv\Scripts` i bruger-PATH.
- **Pc 2 (fra 2026-10-18):** Python 3.12 installeret til brugeren (`winget --scope user`), ESPHome i `%LOCALAPPDATA%\ESPHome\venv`, genvejen `%LOCALAPPDATA%\ESPHome\bin\esphome.cmd` i bruger-PATH. Kræver ikke administrator. Fremgangsmåden står i README under *Installation* og er den, der skal bruges på nye pc'er.
- Build-mappen ligger på begge pc'er i `C:\ProgramData\ESPHome\build`. Den må ikke ligge i projektmappen eller i OneDrive (flere hundrede MB).

**På en ny pc:**
1. Følg *Installation* i README.
2. `secrets.yaml` skal kopieres med (den er ikke i Git).
3. Første kompilering henter ESP-IDF-toolchain (tager 10+ min). **Kør ESPHome fra PowerShell eller cmd, ikke fra Git Bash/MSYS** – ESP-IDF-installationen fejler under MSYS.
4. I en terminal, der er åbnet før PATH blev ændret, kaldes `& "$env:LOCALAPPDATA\ESPHome\venv\Scripts\esphome.exe"` direkte.

Kommandoer (fra projektmappen):
```
esphome compile ventilregulator.yaml
esphome upload --device 192.168.4.188 ventilregulator.yaml     # OTA
esphome upload --device COMx ventilregulator.yaml              # USB
esphome logs --device 192.168.4.188 ventilregulator.yaml       # log over WiFi
```
- Genstart via webserveren: `POST http://192.168.4.188/button/Restart/press`.
- Webserverens REST-adresser bruger entitetens navn, fx `GET /text_sensor/Temperature%20sensor%20address`.
- Brugeren følger loggen med udvidelsen *Serial Monitor* i VS Code (115200 baud). Med OTA kan monitoren stå åben.
- ESPHome søger kun efter 1-Wire-følere ved opstart. Den gemte adresse dækker udfald.
- Den seneste firmware (rev 2, AS5600 over I²C) er kompileret og lagt ind 2026-10-18 (RAM ca. 28 %, flash ca. 56 %).
- Linjen `fatal error: bits/c++config.h` tidligt i kompileringen er støj og stopper ikke bygningen.

## Status (2026-10-24)

**Virker:**
- Boardet sidder i fyrrummet på et nyt access point (-47 dBm). OTA, webserver, log over WiFi og datalog (`/datalog`) virker. Al betjening sker via webUI'en (http://192.168.4.188); skærmen bruges ikke.
- DS18B20 (returtemperatur), AS5600 over I²C, FET-modul og retningsoptokobler er tilsluttet og virker. Ventilen er kalibreret (gentagelsespræcision bedre end 0,15 %), og reguleringen kører i Auto.

**Mangler / næste skridt:**
1. **Ekstra DS18B20 på kedlens fremløb og husets retur** (til fremkobling og karakteristik). Der er ingen følerlommer, så de monteres udvendigt på røret med varmeledende pasta og isolering. **Firmwaren skal opdateres, før de sættes på bussen:** `on_boot` vælger i dag den første DS18B20, den finder, og vil gemme en vilkårlig adresse som returføler. Følerne skal tildeles efter adresse.
2. AS5600-magneten sidder for langt væk ("Position sensor gain" = 128, maksimum). Flyt den tættere på (gain ca. 40–90), og kalibrér igen.
3. Indregulér Kp/Ti under rigtig drift ud fra dataloggen (`/datalog`). Ti er sat til 150 s (2026-10-24), Kp 4. Brugerens første indtryk ved Ti 300: for langsom indsvingning og for stort oversving.
4. Byg strømforsyningen som beskrevet ovenfor, og sæt 1N5819 over motoren.
5. Energimåleren (se *Planer*).
6. Overvej at sætte et kodeord på hotspottet (`ap_password` er tomt, så "Shuntventil" er åbent).

## Planer

- **Revision 2:** AS5600-delen er lavet (packages, se *Firmware*). MOSFET-modulet mangler; det kræver ingen firmwareændring (samme GPIO 25, tjek `motor_inverted`).
- **Aflæsning af energimåleren** fra samme ESP (måleren sidder under 30 cm fra ventilen):
  - Måler: **Siemens Ultraheat 2WR5**, type 2WR5152-6HC60-0FA8, fra 2001, forsynet med 230 V (ingen batteri-hensyn), flowmåler i retur, Pt500, plads til pulsmodul.
  - **Placering:** flowdelen sidder på **returrøret fra stuehuset, før shuntventilen**. Den måler altså husets flow, og dens to Pt500-følere giver husets fremløb og retur. Dermed kan måleren alene give blandingsforholdet `x = (T_blandet − T_retur_hus) / (T_fremløb − T_retur_hus)` og husets flow som funktion af ventilstillingen, dvs. ventilens reelle karakteristik. Udlæsningen er langsom (IEC 62056-21), så den egner sig til karakteristik og statistik, ikke til hurtig fremkobling.
  - Optisk port efter EN 61107 (= IEC 62056-21). Det er **det ovale vindue til venstre for den gule mærkat**. Den runde ting til højre er en trykknap. Der er sandsynligvis ingen stålring, så magneten holder ikke.
  - Plan: **Hichi TTL-print ("Platine")** fra eBay.de i et hjemmelavet, lystæt hus (PETG/ABS, skumpakning), monteret aftageligt med tape/velcro og retningen afprøvet. 3,3 V, GND, TX → GPIO 13, RX → GPIO 17.
  - Protokol: IEC 62056-21 mode B, opvækning og forespørgsel ved 300 baud, data ved 2400 baud (7E1/7E2). Samme familie som Landis+Gyr UH50/T550: [esphome-uh50reader](https://github.com/psvanstrom/esphome-uh50reader) (koderne 6.8 energi, 6.26 volumen, 6.4 effekt, 6.27 flow, 6.29/6.28 frem/retur, 6.30 ΔT) og den generelle IEC 62056-21-komponent fra HA-forummet. 2WR5 er ikke nævnt direkte, så **første version skal skrive målerens rå svar i loggen**, og koderne afkodes ud fra det. Tællerne skal være `total_increasing`-sensorer til HA's energioversigt.
  - Det fravalgte RS485-læsehoved (YDM220803C) kræver 5–24 V og en RS485-omformer.
- **Flere følere på fyret** i en senere version (fx en ekstra DS18B20 på fremløbet, som også kan bruges til fremkobling).
- **Fyrets controller** (KSM Toxido 20 F-Auto, TCP/IP) skal aflæses **direkte af Home Assistant og ikke af ESP'en**. Protokollen (Modbus TCP, HTTP/JSON eller færdig integration) er ikke undersøgt endnu.
- **Ventilens karakteristik og linearisering:** L-boringen gør karakteristikken stærkt ulineær (se *Motorventil*). Plan: mål husets flow og temperaturerne fra energimåleren ved manuelle trin på 0–100 % i stabil drift, og lav en omregningstabel i firmwaren, så PI-regulatorens udgang er "ønsket blandingsforhold" og tabellen oversætter til ventilstilling. Indtil da skal Kp/Ti indstilles til midterområdet (højest forstærkning), så reguleringen er stabil der og blot langsommere ved enderne.
- **Fremkobling:** kræver hurtige temperaturer for kedlens fremløb og husets retur, dvs. to ekstra DS18B20 på 1-Wire-bussen. Energimålerens temperaturer er for langsomme til det.
- **Autotune** er vurderet og fravalgt foreløbig (2026-10-24): forstærkningen skifter med ΔT og ventilstilling, anlægget er sjældent i ro, og relæmetoden slider på motoren. Hvis det bliver nødvendigt, er en trin-test med forslag efter SIMC-reglerne at foretrække.

## Historik i hovedtræk

- **2026-09-30:** Første version som Arduino/PlatformIO-firmware med 3-punkt-styring af en 230 V-ventil og webinterface. Derefter tilføjet potentiometer-tilbagemelding med automatisk læring af endepunkter, og så skiftet til ESPHome med skærm, grafer og WiFi-provisionering (hotspot/Improv).
- **2026-09-30:** Ventildata fundet (CR305, højst 5 s pr. 90°), motorsløjfen sat til 20 ms, Arduino-versionen slettet. Gennemgået SSR med/uden zero-cross (zero-cross er fint, fordi en triac alligevel først slukker ved nulgennemgang) og potentiometertyper (ledende plast eller AS5600 anbefales).
- **2026-10-04:** Board nr. 1 viste sig defekt, og firmwaren blev lagt ind på board nr. 2 (COM4). Skærmens nederste halvdel er defekt, så layoutet blev reduceret til det vigtigste plus hændelseslog og statuslinje på serielporten. Motoren ombygget til 5 VDC med optokobler-retning. ESPHome installeret i Program Files med build-mappen i ProgramData. WiFi sat op og OTA i brug. Indlæring af dødtid og efterløb tilføjet. Al kode oversat til engelsk og IP-adressen vist på skærmen. Valg af MOSFET og DS18B20-tilslutning gennemgået. DS18B20 fundet og fallback for adressen lavet.
- **2026-10-05/06:** Hardwarerevisionerne fastlagt (rev 1: potentiometer + relæ, rev 2: AS5600 + MOSFET). Strømforsyningen designet. Energimåleren identificeret som Siemens Ultraheat 2WR5, og valget af læsehoved gennemgået (Hichi TTL fra eBay.de, printversion i eget hus). Det er besluttet, at KSM-controlleren håndteres af Home Assistant.
- **2026-10-18:** Arbejdet fortsat på pc 2. Python 3.12, ESPHome 2026.9.1 (i venv) og Git 2.55 installeret. Firmwaren kompilerer uændret. CLAUDE.md oprettet som fælles status mellem pc'erne. Overgang til et privat GitHub-repository besluttet (commits som "Jacob Rytter" <jr@pc-smeden.dk>) og første commit pushet. Skærmen fejlede (fladkablets forbindelse). En test med `data_rate: 10MHz` gav samme config-hash som standarden, så den ændrede intet og blev fjernet igen. Displayet fra board 1 blev flyttet over, virkede kortvarigt, og blev så opgivet efter beskadigede loddeøer. Brugeren har fået en AS5600. Den analoge løsning blev fravalgt, og firmwaren blev omlagt til AS5600 over I²C: `shunt.h` blev uafhængig af følertypen (`SensorCfg`, wrap ved 360°), og den hardwarespecifikke del blev lagt i `hw/rev1.yaml`/`hw/rev2.yaml`. Begge revisioner kompilerer, og rev 2 er lagt ind.
- **2026-10-24:** Board flyttet til fyrrummet. Nyt access point (signal -47 dBm, IP uændret). DS18B20 og AS5600 tilsluttet og virker (AS5600 gain 128 = magneten lidt for langt væk). FET-modulet (LR7843, PC817-indgang) virker; det afbryder minussiden, så motorens plus går direkte til +5 V og minus til OUT. Retningen passer (GPIO 26 tændt = åbn). Sikker stilling ændret til **lukket shunt**: følerfejl og genstart uden kalibreret føler lukker shunten (`fault_open_shunt` erstattet af `fault_hold_position`), og kalibreringen kører nu åben → lukket. Manuel motorstyring tilføjet (Mode "Manual motor", switch "Valve motor", select "Valve direction"), og entiteterne grupperet i webUI'en med præfiks i navnene. Første drift i Auto: stabil og rolig, men langsom (lå 1–2 °C over SP, top 57,2 °C). Ti sat til 150 s, og datalog i RAM tilføjet (`/datalog`, 4 timer + starter pr. time). Slideren for "Manual shunt position" erstattet af et talfelt. Al skærmkode fjernet. Udvendigt monterede DS18B20 på fremløb og retur gennemgået (ingen følerlommer).
- **2026-10-25:** Dataloggen (SP 50 °C) viste meget præcis regulering (±0,4 °C, sd 0,16 °C), men 21 motorstarter/time med mange korrektionspulser og retningsskift. Regulatoren omlagt til skridtregulator (inkrementel PI med akkumulator), og dødbåndet sat til 3 %. Første minutter: ingen korrektionspulser. Fyrets iltsonde (H&S 118.02-7100) viser 11,2 % i fri luft i stedet for 20,9 %; den er varm, så opvarmning er ikke forklaringen.
