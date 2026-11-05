"""Offline tests for tools/record_sky_day.py (no network).

Run: python3 -m unittest tools/test_record_sky_day.py
"""

import datetime as dt
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import record_sky_day as rsd  # noqa: E402

SKY = rsd.REPO / "resources" / "sky"
DAYS = sorted(p for p in SKY.iterdir() if (p / "manifest.json").exists())
T0 = dt.datetime(2024, 5, 10, tzinfo=dt.timezone.utc)


class RecordedDays(unittest.TestCase):
    def test_days_present(self):
        self.assertEqual([p.name for p in DAYS], ["2024-05-10", "2025-01-19", "2025-01-25"])

    def test_track_rebuilds_byte_for_byte(self):
        for folder in DAYS:
            with self.subTest(day=folder.name):
                self.assertEqual(rsd.rebuild_track(folder), (folder / "track.json").read_text(encoding="utf-8"))

    def test_track_shape(self):
        for folder in DAYS:
            track = json.loads((folder / "track.json").read_text(encoding="utf-8"))
            with self.subTest(day=folder.name):
                self.assertEqual(track["length"], 2880)
                self.assertEqual(set(track["channels"]), set(rsd.CHANNELS))
                for values in track["channels"].values():
                    self.assertEqual(len(values), 2880)

    def test_day_character(self):
        summary = {p.name: json.loads((p / "manifest.json").read_text())["summary"] for p in DAYS}
        storm, moderate, quiet = summary["2024-05-10"], summary["2025-01-19"], summary["2025-01-25"]
        self.assertGreaterEqual(storm["max_kp"], 8.333)
        self.assertLessEqual(storm["min_bz_nT"], -30)
        self.assertGreaterEqual(storm["max_abs_dh_nT"], 500)
        self.assertGreaterEqual(len(moderate["negative_h_bays_utc"]), 2)
        self.assertLessEqual(quiet["max_kp"], 2.0)
        for s in summary.values():
            self.assertEqual(s["gaps_percent"]["h"], 0.0)


class Gridding(unittest.TestCase):
    def test_kp_is_stepped_per_three_hours(self):
        body = json.dumps({"Kp": [2.0, 5.333], "datetime": ["2024-05-10T00:00:00Z", "2024-05-10T03:00:00Z"],
                           "status": ["def", "def"]}).encode()
        kp, status = rsd.grid_kp(body, T0, 360)
        self.assertEqual(kp[0], 2.0)
        self.assertEqual(kp[179], 2.0)
        self.assertEqual(kp[180], 5.333)
        self.assertEqual(kp[359], 5.333)
        self.assertEqual(status, ["def"])

    def test_omni_fill_values_become_gaps(self):
        body = json.dumps({
            "parameters": [{"name": "Time"}, {"name": "BY_GSM", "fill": "9999.99"},
                           {"name": "BZ_GSM", "fill": "9999.99"}, {"name": "flow_speed", "fill": "99999.9"},
                           {"name": "proton_density", "fill": "999.99"}],
            "data": [["2024-05-10T00:00:00.000Z", 1.5, -20.25, 99999.9, 999.99],
                     ["2024-05-10T00:02:00.000Z", 9999.99, 3.0, 700.0, 12.5]]}).encode()
        sw = rsd.grid_omni(body, T0, 3)
        self.assertEqual(sw["by"], [1.5, None, None])
        self.assertEqual(sw["bz"], [-20.25, None, 3.0])
        self.assertEqual(sw["v"], [None, None, 700.0])
        self.assertEqual(sw["n"], [None, None, 12.5])

    def test_geomag_nulls_and_out_of_span_samples(self):
        body = json.dumps({
            "metadata": {"intermagnet": {"imo": {"iaga_code": "CMO", "name": "College",
                                                 "coordinates": [-147.86, 64.874, 197.0]},
                                         "data_type": "quasi-definitive"}},
            "times": ["2024-05-09T23:59:00.000Z", "2024-05-10T00:00:00.000Z", "2024-05-10T00:01:00.000Z"],
            "values": [{"values": [1.0, 12686.5, None]}]}).encode()
        h, station = rsd.grid_geomag(body, T0, 2)
        self.assertEqual(h, [12686.5, None])
        self.assertEqual(station["code"], "CMO")
        self.assertEqual(station["lat"], 64.874)

    def test_bays(self):
        quiet = [100.0] * 300
        two = list(quiet)
        two[50] = -100.0           # first bay
        two[100] = -100.0          # too close: same bay
        two[250] = -100.0          # second bay
        self.assertEqual(rsd.count_bays(quiet), [])
        self.assertEqual(rsd.count_bays(two), [50, 250])


if __name__ == "__main__":
    unittest.main()
