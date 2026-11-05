# Sky days

Recorded magnetosphere data for the Sky Model: the prototype's input (T1.2),
Journey presets and the bundled factory sky (M5), and test fixtures.
Each folder is 48 h from 00:00 UTC of the named day, observer station CMO
(College, Alaska, 64.874° N, 147.86° W).

| Day | Character | max Kp | min Bz | max \|ΔH\| at CMO |
|---|---|---|---|---|
| `2024-05-10` | "Gannon" storm (G5), reference day | 9.0 | −47.9 nT | 3342 nT |
| `2025-01-19` | moderate, two substorm nights | 4.33 | −9.4 nT | 1034 nT |
| `2025-01-25` | quiet | 1.0 | −2.8 nT | 44 nT |

Why each day was chosen, gap statistics and source URLs: `<day>/manifest.json`.

## Files per day
- `geomag_CMO.json`: USGS Geomagnetism web service response, H, 1 min,
  `type=quasi-definitive` (verbatim).
- `kp.json`: GFZ Kp API response, 3-hourly, definitive (verbatim).
- `solarwind.json`: NASA OMNI `OMNI_HRO_1MIN` via CDAWeb HAPI 2.0: BY_GSM,
  BZ_GSM, flow_speed, proton_density (verbatim, fill values included).
- `track.json`: derived 1-minute grid, channels `h` (nT, absolute), `kp`,
  `v` (km/s), `n` (cm⁻³), `bz`, `by` (nT, GSM); `null` = gap.
- `manifest.json`: span, why, station, sources with licence and credit,
  summary (max Kp, min Bz, bays, gaps), retrieval time.

Known gaps: OMNI has no plasma data on 10 May 2024 before 08:04 UT and
several 15–30 min plasma gaps during the storm; H is complete on all days.

## Regenerate
```sh
python3 tools/record_sky_day.py --day 2024-05-10 --why "…"
```
`track.json` is reproduced byte for byte; raw USGS responses differ only in
their `metadata.generated` stamp, `manifest.json` only in `retrieved_at`.

## Credits and licences
- Geomagnetic data: U.S. Geological Survey Geomagnetism Program (US public domain).
- Kp: Matzka, J. et al. (2021), Geomagnetic Kp index, GFZ Data Services,
  doi:10.5880/Kp.0001 (CC BY 4.0).
- Solar wind: NASA/GSFC's Space Physics Data Facility's OMNIWeb/CDAWeb
  service, and OMNI data (NASA open data).
