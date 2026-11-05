#include "TestHelpers.h"
#include "data/WorldData.h"
#include "data/WorldSources.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace worldtable::data;

TEST_CASE ("NDBC spectral fixture parses oldest-first", "[data][ndbc]")
{
    SpectrumSeries s;
    REQUIRE (parseNdbcSpectral (testing::loadFixture ("wavespec/46026_6rows.data_spec"), s).wasOk());

    // File rows: 17:20, 16:20, 15:50, 15:20, 14:50 (the 16:50 record is missing upstream).
    REQUIRE (s.rows.size() == 5);
    const double expectedGaps[] = { 1800.0, 1800.0, 1800.0, 3600.0 };
    for (size_t i = 1; i < s.rows.size(); ++i)
        CHECK (s.rows[i].timeUtc - s.rows[i - 1].timeUtc == Catch::Approx (expectedGaps[i - 1]));

    const auto& newest = s.rows.back();
    REQUIRE (newest.frequencyHz.size() == 46);
    CHECK (newest.frequencyHz.front() == Catch::Approx (0.033f));
    CHECK (newest.frequencyHz.back() == Catch::Approx (0.485f));
    CHECK (newest.density[7] == Catch::Approx (9.770f));   // the 0.068 Hz swell peak in that row

    // 2026-11-04 17:20 UTC
    CHECK (newest.timeUtc == Catch::Approx (1793812800.0));
}

TEST_CASE ("NDBC parser skips malformed and missing rows", "[data][ndbc]")
{
    SpectrumSeries s;
    REQUIRE (parseNdbcSpectral (testing::loadFixture ("wavespec/malformed.data_spec"), s).wasOk());
    REQUIRE (s.rows.size() == 1);
    CHECK (s.rows[0].density.size() == 4);

    CHECK (parseNdbcSpectral ("", s).failed());
    CHECK (parseNdbcSpectral ("<html>404</html>", s).failed());
}

TEST_CASE ("USGS geomag fixture parses with gaps filled", "[data][geomag]")
{
    ScalarSeries s;
    REQUIRE (parseUsgsGeomag (testing::loadFixture ("geomag/BOU_300.json"), s).wasOk());
    REQUIRE (s.values.size() == 300);
    CHECK (s.periodSeconds == Catch::Approx (60.0));
    CHECK (s.startUtc == Catch::Approx (1793620800.0));   // 2026-11-02T12:00:00Z

    // index 10 was nulled in the fixture: filled by interpolation between neighbours
    CHECK (s.values[10] == Catch::Approx (0.5f * (s.values[9] + s.values[11])));
    CHECK (s.values[10] > 20000.0f);

    CHECK (parseUsgsGeomag ("{", s).failed());
    CHECK (parseUsgsGeomag ("{\"times\": [], \"values\": []}", s).failed());
}

TEST_CASE ("Every factory-world recording parses", "[data][factory]")
{
    for (int i = 0; i < numSources(); ++i)
    {
        const auto& def = sourceDef (i);
        const auto text = loadFactoryData (def);
        INFO (def.station);
        REQUIRE (text.isNotEmpty());

        if (def.kind == SourceDef::Kind::ndbcSpectral)
        {
            SpectrumSeries s;
            REQUIRE (parseNdbcSpectral (text, s).wasOk());
            CHECK (s.rows.size() >= 64);
        }
        else
        {
            ScalarSeries s;
            REQUIRE (parseUsgsGeomag (text, s).wasOk());
            CHECK (s.values.size() >= 2000);
        }
    }
}

TEST_CASE ("Geomag request URL asks for 48 h of 1-min H data", "[data][url]")
{
    const auto url = buildRequestUrl (sourceDef (4), 1793812800.0).toString (true);
    INFO (url);
    CHECK (url.startsWith ("https://geomag.usgs.gov/ws/data/"));
    CHECK (url.contains ("id=BOU"));
    CHECK (url.contains ("sampling_period=60"));
    CHECK (url.contains ("starttime=2026-11-02T17%3A20%3A00Z"));
    CHECK (url.contains ("endtime=2026-11-04T17%3A20%3A00Z"));
}
