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
- Temperaturføleren (DS18B20) sidder ca. 10 cm efter ventilen. Det er kort; README anbefaler 30–50 cm eller efter pumpen af hensyn til opblanding.

## Filer

| Fil | Indhold |
|---|---|
| `ventilregulator.yaml` | ESPHome-konfiguration: pins, WiFi, skærm, indstillinger (number/select/button), sensorer, intervaller |
| `shunt.h` | PI-regulator + motorstyring som ren C++ (`namespace shunt`, global `shunt::ctl`). Ingen ESPHome-afhængigheder. |
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
- Boardet er brugt og flere år gammelt. **Skærmen er død** (2026-10-18): fladkablets forbindelse til printet svigtede (svagt lys, "snestorm"). Displayet fra board 1 blev flyttet over og virkede kortvarigt, men loddeøerne blev beskadiget ved andet forsøg, og skærmen er opgivet. Brugeren bruger webUI'en i stedet. Firmwaren driver stadig skærmen, hvilket er harmløst. Ved et nyt T-Display (almindeligt ESP32, ikke S3) sættes `full_display: "true"`.
- Det første board (CP210x på COM3) var defekt. ESP32'en svarede slet ikke over UART, heller ikke i download-tilstand. Det samme var sket ved en tidligere lejlighed. Dets display er brugt og ødelagt, så det kan ikke bruges til reservedele længere.
- IP-adresse på brugerens net: **192.168.4.188** (mDNS `ventilregulator.local`). WiFi-signal ca. -81 dBm (svagt).

### Pins

| Funktion | GPIO |
|---|---|
| DS18B20 (1-Wire, 4,7 kΩ pull-up til 3,3 V) | 27 |
| Motorstrøm 5 VDC (relæ i rev 1, MOSFET i rev 2), aktiv HIGH (`motor_inverted`) | 25 |
| Retning: optokobler i ventilen via 330 Ω (tændt = åbn shunt, `invert_dir`) | 26 |
| Stillingsføler analog (potentiometer / AS5600 OUT), skal være ADC1 | 33 (+100 nF og **1 MΩ til GND**) |
| Reserveret til AS5600 via I²C (rev 2) | 21 (SDA), 22 (SCL) |
| Reserveret til energimålerens læsehoved (UART2) | 13 (RX), 17 (TX) |
| Skærm (fast på boardet) | 4 (baggrundslys), 5, 16, 18, 19, 23 |
| Knapper | 0 (BOOT), 35 (tænder/slukker baggrundslys) |

Undgå strapping-benene 0, 2, 5, 12 og 15 til eksternt udstyr.

### Motorventil
- HSH-Flo/HH-Flo CR305, 3-vejs L-type, messing, DN25 (mærkat: A20-T25-B3-C, CR305-AC110-230V, 500 mA). 90° på højst 5 s og 2 Nm. Ingen manuel overstyring. Blander proportionalt.
- **Ombygget af brugeren:** 230 V-delen og auto-retur er fjernet. Motoren kører direkte på **5 VDC og trækker 120–150 mA**. En **optokobler på ventilens print vælger retning** (aktiveret = drejer mod uret, ifølge brugeren). **De interne endestopkontakter sidder der stadig** og slukker motoren selv.
- Motoren har en vis dødtid ved start og et efterløb. Slør er ubetydeligt.

### Følere
- **DS18B20**, ROM-adresse `0x8200000078b1d228`. Den findes ved hver opstart, og adressen gemmes som fallback (se firmware).
- **Stillingsføler rev 1:** potentiometer på ventilakslen, yderben på 3,3 V/GND, slæber på GPIO 33. **1 MΩ til GND mangler endnu.** Uden den svæver indgangen på ca. 140–240 mV, softwaren tror det er en føler, og der står et falsk endepunkt (mv100 ≈ 142 mV) i flash. Det overskrives ved første kalibrering.
- **AS5600 i analog drift (fra 2026-10-18, erstatter potentiometeret):** brugeren har fået en AS5600 og monterer den nu. Den køres analogt på den nuværende firmware: VCC på **3,3 V (aldrig 5 V)**, OUT → **1 kΩ i serie** → GPIO 33 (100 nF og 1 MΩ til GND bevares), DIR til GND, SDA/SCL på GPIO 21/22 til senere I²C. Udgangen dækker 0–3,3 V over 360°, så 90° giver ca. 825 mV. Magneten drejes, så ventilen ved 50 % giver ca. 1650 mV, og spring-punktet undgås. Firmwaren klarer begge retninger (`fabs(mv100 - mv0)`), men endepunkterne skal være > 0 mV.
- **Stillingsføler rev 2:** AS5600, helst via I²C (digital og melder magnetfeltets status). Magneten skal være **diametralt magnetiseret** (magneter med skruehul er typisk aksiale og virker ikke). Luftspalte 0,5–3 mm, helst 1–1,5 mm. Det svarer til ca. 3–3,5 mm fra centrum af chippen til centrum af magneten for en magnet på 6×2,5 mm. Centrering inden for ±0,25 mm, og kun ikke-magnetiske materialer tæt på. Test af magnet: drej den under et kompas; følger nålen med rundt, er den diametral.

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
- Substitutions: `invert_dir`, `motor_inverted`, `fault_open_shunt`, `full_display`.
- WiFi fra `secrets.yaml`, **`reboot_timeout: 0s`** for både WiFi og API (ESPHome genstarter ellers efter 15 min). Hotspot **"Shuntventil"** med `ap_timeout: 5min`, og ESPHome bliver ved med at forsøge at forbinde (hvert 30. s) mens hotspottet er åbent. `captive_portal`, `improv_serial`, `api` med kryptering, `ota` (beskyttet af API-nøglen, uden kodeord), `web_server` v3 med `local: true`. **WPS understøttes ikke af ESPHome.**
- Logger på DEBUG med sensor/text_sensor/binary_sensor på INFO og component på ERROR. Statuslinje hvert 10. s og hændelser fra `shunt`.
- Indstillinger (template numbers, `restore_value`): Setpoint (55 °C), Kp (4 %/°C), Ti (300 s), Travel time 0–100 % (5 s), Deadband (2 %), Manual shunt position. Select "Mode" (Auto/Manual). Knapper "Calibrate valve" og "Restart".
- Globals i flash: `g_mv0`, `g_mv100` (stillingsfølerens endepunkter), `g_ds_addr` (uint64, seneste DS18B20-adresse).
- `on_boot` (priority -100): indlæser endepunkter, sætter parametre, finder DS18B20 på bussen (family code 0x28). Den fundne adresse bruges og gemmes. Findes ingen, bruges `g_ds_addr` via `set_address()`, så målingen kommer af sig selv, når føleren svarer igen. Diagnose: text sensor "Temperature sensor address".
- Interval 20 ms: kopierer indstillinger til `shunt::ctl`, læser `pos_adc.sample()` (16 målinger), kalder `update(millis())`, skriver `dir_out`/`motor_out`, sender hændelser til loggen, gemmer lærte endepunkter og målt gangtid.
- Interval 10 s: `tick(temperatur, 10 s)` og statuslinjen.
- Sensorer til HA: Return temperature, Shunt position, Shunt target, P/I term, Position sensor (mV, 0 %, 100 %), Coast-down, Dead time, Motor starts, WiFi signal, Status, Calibration, IP address. Binary: Sensor fault, Valve fault, Position sensor OK.
- Skærm: `mipi_spi` model `T-DISPLAY`, rotation 90°, `color_depth: 8`, `buffer_size: 100%`, opdatering hvert 2. s. Linje 1: temperatur (orange) og stilling (hvid). Linje 2: "SP xx.x°C" og status (rød ved fejl). Hver 3. opdatering vises IP-adressen i stedet, når `full_display` er false. Graferne er fjernet på grund af den defekte skærm.

### Regulator og motorstyring (`shunt.h`)
- **PI** i positionsform: `e = SP - T`, `P = Kp·e`, `I += Kp·dt/Ti·e`. Anti-windup med betinget integration og I begrænset til 0–100. Stødfri overgang fra manuel til auto. Start: I = 100 (shunt åben).
- **Temperaturen filtreres ikke** (bevidst: filtre giver forsinkelse, og et trin på 0,0625 °C × Kp 4 = 0,25 % ligger under dødbåndet). Gyldighedskontrol: NaN, uden for -50…125 °C, 85,0 °C uden forvarsel. SENSOR FAULT efter 3 ugyldige målinger. Ved følerfejl holdes stillingen (eller åbnes shunten med `fault_open_shunt`). Et median-filter over 3 kan overvejes, hvis målingen hopper i drift.
- **Motorstyring** (tilstandene IDLE → PREP → RUN → REST): retningen sættes 150 ms før motoren tændes og slippes 150 ms efter stop. Mindst 1 s hvile mellem pulser. Med kalibreret stillingsføler køres der til den målte stilling er nået (minus indlært efterløb). Uden føler køres der efter tid.
- **Endestop:** mål ≤ 0,5 % eller ≥ 99,5 % kører mod endestop med 25 % ekstra tid. Endestoppet regnes for nået, når føleren har stået stille i 1,5 s (`STALL_MS`), og endepunktet læres (gemmes kun ved ændring > 20 mV).
- **Kalibrering** (knap): kører helt lukket og derefter helt åben, lærer mv0/mv100 og måler gangtiden.
- **Indlæring:** dødtid (fra motoren tændes til akslen drejer, glidende gennemsnit) og efterløb (måles 1 s efter hvert stop, 0–5 %). Læres forfra efter genstart.
- **VALVE FAULT**, hvis akslen står stille eller målet ikke nås i tide. Motorstarter tælles.
- **Genstart:** med kalibreret føler fortsættes fra målt stilling. Uden føler køres shunten helt åben.
- Hændelseskø (8 × 112 tegn) hentes af YAML med `next_event()` og skrives med `ESP_LOGI("shunt", ...)`.
- Vigtige konstanter: `MIN_PULSE_MS 150`, `DIR_SETTLE_MS 150`, `REST_MS 1000`, `STOP_LEAD 0.5`, `SENSOR_MIN_MV 100`, `SENSOR_MAX_MV 3150`, `SENSOR_MIN_SPAN 300`, `STALL_MV 15`, `STALL_MS 1500`.

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
- Den seneste firmware er kompileret og lagt ind 2026-10-05 (RAM ca. 28 %, flash ca. 55 %).

## Status (2026-10-18)

**Virker:**
- Firmwaren kører på T-Display nr. 2 og er på WiFi. OTA, webserver og log over WiFi virker. Repository'et er pushet til GitHub (`main` følger `origin/main`).
- Skærmen er død. Al betjening og aflæsning sker via webUI'en (http://192.168.4.188).

**Mangler / næste skridt:**
1. **DS18B20 svarer ikke** efter skærmreparationen (`reading scratch pad failed bus reset`, SENSOR FAULT). Tilslut den igen, og tjek 4,7 kΩ og GPIO 27.
2. Montér AS5600 analogt (se *Følere*), tilslut relæ (GPIO 25) og optokobler (GPIO 26 via 330 Ω). Tjek relæmodulets aktive niveau (`motor_inverted`) og retningen (`invert_dir`). Indstil magneten til ca. 1650 mV ved 50 %, og kalibrér ("Calibrate valve"). Følg efterløb, dødtid og motorstarter i loggen.
3. Derefter: tilføj ESPHome's `as5600`-komponent via I²C for magnetstatus (første skridt mod rev 2). Overvej en substitution `has_display: "false"`, der slår skærmen fra (sparer ca. 32 kB RAM og frigør GPIO 4, 5, 16, 18, 19 og 23).
4. Indregulér Kp/Ti under rigtig drift (start: Kp 4, Ti 300 s).
5. Byg strømforsyningen som beskrevet ovenfor.
6. Overvej at sætte et kodeord på hotspottet (`ap_password` er tomt, så "Shuntventil" er åbent).

## Planer

- **Revision 2:** AS5600 via I²C (GPIO 21/22, ca. 20 cm kabel tæt på motoren, 100 kHz, 4,7 kΩ pull-up, snoet ledning) og MOSFET-modul. Når hardwaren er klar, deles konfigurationen op i en fælles del og en del pr. revision (ESPHome-`packages`), så rev 1 stadig kan bygges. Med AS5600 kan `STALL_MS` sættes ned til ca. 0,3–0,5 s.
- **Aflæsning af energimåleren** fra samme ESP (måleren sidder under 30 cm fra ventilen):
  - Måler: **Siemens Ultraheat 2WR5**, type 2WR5152-6HC60-0FA8, fra 2001, forsynet med 230 V (ingen batteri-hensyn), flowmåler i retur, Pt500, plads til pulsmodul.
  - Optisk port efter EN 61107 (= IEC 62056-21). Det er **det ovale vindue til venstre for den gule mærkat**. Den runde ting til højre er en trykknap. Der er sandsynligvis ingen stålring, så magneten holder ikke.
  - Plan: **Hichi TTL-print ("Platine")** fra eBay.de i et hjemmelavet, lystæt hus (PETG/ABS, skumpakning), monteret aftageligt med tape/velcro og retningen afprøvet. 3,3 V, GND, TX → GPIO 13, RX → GPIO 17.
  - Protokol: IEC 62056-21 mode B, opvækning og forespørgsel ved 300 baud, data ved 2400 baud (7E1/7E2). Samme familie som Landis+Gyr UH50/T550: [esphome-uh50reader](https://github.com/psvanstrom/esphome-uh50reader) (koderne 6.8 energi, 6.26 volumen, 6.4 effekt, 6.27 flow, 6.29/6.28 frem/retur, 6.30 ΔT) og den generelle IEC 62056-21-komponent fra HA-forummet. 2WR5 er ikke nævnt direkte, så **første version skal skrive målerens rå svar i loggen**, og koderne afkodes ud fra det. Tællerne skal være `total_increasing`-sensorer til HA's energioversigt.
  - Det fravalgte RS485-læsehoved (YDM220803C) kræver 5–24 V og en RS485-omformer.
- **Flere følere på fyret** i en senere version (fx en ekstra DS18B20 på fremløbet, som også kan bruges til fremkobling).
- **Fyrets controller** (KSM Toxido 20 F-Auto, TCP/IP) skal aflæses **direkte af Home Assistant og ikke af ESP'en**. Protokollen (Modbus TCP, HTTP/JSON eller færdig integration) er ikke undersøgt endnu.
- **Ventilens karakteristik** (valgfrit): flow fra energimåleren ved manuelle trin på 0–100 % kan bruges til en omregningstabel i firmwaren, hvis reguleringen opfører sig forskelligt i forskellige områder.

## Historik i hovedtræk

- **2026-09-30:** Første version som Arduino/PlatformIO-firmware med 3-punkt-styring af en 230 V-ventil og webinterface. Derefter tilføjet potentiometer-tilbagemelding med automatisk læring af endepunkter, og så skiftet til ESPHome med skærm, grafer og WiFi-provisionering (hotspot/Improv).
- **2026-09-30:** Ventildata fundet (CR305, højst 5 s pr. 90°), motorsløjfen sat til 20 ms, Arduino-versionen slettet. Gennemgået SSR med/uden zero-cross (zero-cross er fint, fordi en triac alligevel først slukker ved nulgennemgang) og potentiometertyper (ledende plast eller AS5600 anbefales).
- **2026-10-04:** Board nr. 1 viste sig defekt, og firmwaren blev lagt ind på board nr. 2 (COM4). Skærmens nederste halvdel er defekt, så layoutet blev reduceret til det vigtigste plus hændelseslog og statuslinje på serielporten. Motoren ombygget til 5 VDC med optokobler-retning. ESPHome installeret i Program Files med build-mappen i ProgramData. WiFi sat op og OTA i brug. Indlæring af dødtid og efterløb tilføjet. Al kode oversat til engelsk og IP-adressen vist på skærmen. Valg af MOSFET og DS18B20-tilslutning gennemgået. DS18B20 fundet og fallback for adressen lavet.
- **2026-10-05/06:** Hardwarerevisionerne fastlagt (rev 1: potentiometer + relæ, rev 2: AS5600 + MOSFET). Strømforsyningen designet. Energimåleren identificeret som Siemens Ultraheat 2WR5, og valget af læsehoved gennemgået (Hichi TTL fra eBay.de, printversion i eget hus). Det er besluttet, at KSM-controlleren håndteres af Home Assistant.
- **2026-10-18:** Arbejdet fortsat på pc 2. Python 3.12, ESPHome 2026.9.1 (i venv) og Git 2.55 installeret. Firmwaren kompilerer uændret. CLAUDE.md oprettet som fælles status mellem pc'erne. Overgang til et privat GitHub-repository besluttet (commits som "Jacob Rytter" <jr@pc-smeden.dk>) og første commit pushet. Skærmen fejlede (fladkablets forbindelse). En test med `data_rate: 10MHz` gav samme config-hash som standarden, så den ændrede intet og blev fjernet igen. Displayet fra board 1 blev flyttet over, virkede kortvarigt, og blev så opgivet efter beskadigede loddeøer. Brugeren har fået en AS5600, der først kobles analogt på GPIO 33.
