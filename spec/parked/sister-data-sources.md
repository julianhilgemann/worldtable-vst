# Parked — data sources for sister plug-ins

Moved out of `spec/02-data-sources.md` on 2026-11-05 when v1 narrowed to the
magnetosphere (ADR-0002). Kept because the research holds for the planned
sister plug-ins (ocean first). Nothing here is in scope for v1.

## Open questions carried over
- `DataReading` carries one scalar per timestamp. Spectra and waveforms need a
  vector per timestamp: encode them as channels (`wavespec.f00`…`fNN`) or
  extend the contract? `DataSource.h` is a protected interface, so this needs
  a decision before an ocean sister is built.
- Fixtures for plain-text sources: allow any extension under `tests/fixtures/<id>/`.
- Many installs calling the same keyless provider could exceed fair use. A
  small caching relay is the fix if it ever matters.
- POC result (TP.1): the ocean table's swell peak becomes a formant (partials
  3–4 dominate). Alternative mappings: log-frequency, or one bin per partial.

## Candidate catalogue (unverified; for sister plug-ins)

| ID | Domain | Source (provider) | Typical update | Auth | Natural feel → suggested mapping |
|---|---|---|---|---|---|
| `quake` | Seismology | USGS earthquake GeoJSON feeds (past hour/day) | ~1 min | none | Magnitude/energy → low-end weight, rumble, table harmonics; count → density |
| `kp` | Space weather | NOAA SWPC planetary K-index | 3 h (1-min estimates exist) | none | Storm level → shimmer, brightness, reverb size |
| `solarwind` | Space weather | NOAA SWPC solar wind (speed, density, Bz) | ~1 min | none | Speed → LFO rate, density → noise layer, Bz sign → stereo tilt |
| `weather` | Atmosphere | Open-Meteo forecast/current (temp, wind, pressure, humidity) at focus point ⚠ see note | 15 min–1 h | none | Wind → filter motion, pressure → root drift, humidity → reverb damping |
| `co2` | Climate | NOAA GML Mauna Loa CO₂ (daily/monthly) | daily | none | Very slow drift → table position over months ("the long tone") |
| `sunspots` | Solar | SILSO sunspot number | daily/monthly | none | Cycle phase → global brightness macro |
| `ocean` | Oceans | NOAA NDBC buoys (wave height, period, water temp) | ~30–60 min | none | Wave period → LFO1 rate (literally swell), height → amplitude breathing |
| `iss` | Orbit | ISS position API | seconds | none | Lat/lon → pan + slow pitch glide (sparingly) |
| `tides` | Oceans | NOAA CO-OPS water levels (station) | 6 min | none | Tide curve → table morph (~12.4 h cycle) |
| `nasa` | Space | NASA DONKI (flares, CMEs) | event-driven | api.nasa.gov key | Events → v1.1 "event voices" |

⚠ **`weather` licence:** Open-Meteo's free API is for non-commercial use only
(commercial needs a paid plan). This fails criterion 3 if Worldtable is sold.
Candidate replacement: MET Norway Locationforecast (`api.met.no`): free,
CC BY 4.0, commercial use allowed, needs an identifying User-Agent.

Optional later: Copernicus/ECMWF (heavy, keyed), GBIF biodiversity, OpenAQ air quality, lightning networks.

### Table-shape candidates (wavetable material)
A single scalar (e.g. wind speed) is a good modulator but a dull waveform.
Table material needs shape, in one of three forms:
- **Waveform:** a dense time series → builder A (series-as-cycle).
- **Spectrum:** energy per frequency bin → builder B (bins map onto partials).
- **Cycle:** data that repeats naturally (a day, a tide, a ring of points) →
  wraps without a seam; builder A or C.

Researched 2026-11-04; endpoints and licences not yet verified.

| ID | Shape | Source (provider) | Typical update | Auth / licence | Table idea | Caveats |
|---|---|---|---|---|---|---|
| `wavespec` | Spectrum → B | NOAA NDBC `data/realtime2/<STATION>.data_spec` | hourly | none; US public domain | Wave energy per frequency bin (~0.02–0.5 Hz) → partial amplitudes; one frame per hour, so 64 frames ≈ 2.7 days of swell | Plain-text format; number of bins varies by buoy type (verify) |
| `seasurface` | Waveform → A | CDIP buoys, `xyzZDisplacement` via THREDDS OPeNDAP (`thredds.cdip.ucsd.edu/thredds/dodsC/cdip/realtime/<stn>p1_xy.nc`) | ~30 min | none; free, cite CDIP (verify licence) | Sea-surface height sampled at 1.28 Hz; 2048 samples ≈ 27 min of real ocean as a literal waveform | Request the OPeNDAP ASCII form to avoid a netCDF dependency; stations mostly US/Pacific |
| `sunlight` | Cycle → C | Computed locally (NOAA/NREL solar-position algorithm) + solar spectrum bundled once (TSIS-1 via LASP LISIRD) | continuous | none; no network | Day curve of sun elevation as the cycle; solar spectrum tilted toward red as air mass grows at dawn/dusk, so the table darkens at sunset | Fully offline and freeze-safe; cloud dimming needs a weather source |
| `seismogram` | Waveform → A | FDSN `dataselect` (EarthScope; EIDA nodes in Europe) | event-triggered | none | When `quake` reports a large event, fetch ~30 min from a nearby station → that quake becomes a table. Microseism (0.05–0.3 Hz, ocean-driven ground hum) as a constant layer | miniSEED output needs a parser (new dependency, ask first); polling for real-time data is not allowed; IRIS timeseries/WAV service was scheduled to retire 2026-08-26. Suggested for v1.1 |
| `geomag` | Waveform → A | USGS Geomagnetism web service `geomag.usgs.gov/ws/data` (`sampling_period=1`) | 1 s / 1 min | none; US public domain | Earth's magnetic field as an "invisible wind"; 2048 s per frame | JSON capped at 172,800 samples per request |
| `windring` | Cycle → A | MET Norway Locationforecast `api.met.no` | hourly | none; CC BY 4.0, commercial OK | Wind speed at N points on a ring around the focus point → periodic by nature, no wrap blending needed | One request per point: keep N small (8–16) and interpolate; User-Agent must include contact info |
| `river` | Series → A / mod | Pegelonline REST v2 (`pegelonline.wsv.de/webservices/rest-api/v2`), German federal waterways | 15 min | none; likely DL-DE Zero 2.0 (confirmed for the WFS service only) | Water level over days → slow table morph or modulation | Germany only; at most 30 days of history. US equivalent: USGS Water Services (not checked) |

**Suggested table sources for an ocean sister:** `wavespec` (B), `seasurface` (A), `sunlight` (C).
None needs an API key or a new dependency, and all are easy to cover with fixtures.

**Not recommended:** Raspberry Shake FDSN. Data arrives ≥ 30 min late and the
licence appears to forbid redistribution.

