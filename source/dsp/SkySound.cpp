#include "SkySound.h"

#include "sky/SkyRandom.h"

#include <algorithm>
#include <cmath>
#include <numbers>

// prototype/sky/sound/engine.js `tick()`, `geometry()`, `voiceBank()` and `applyChord()`, line by
// line. The model step happens in SkyRuntime, the harmony's requests come through requestChord().

namespace worldtable::dsp
{

static_assert (! (Exciter::kTickSeconds < sky::SkyModel::kTickSeconds) && ! (Exciter::kTickSeconds > sky::SkyModel::kTickSeconds),
               "the exciter's tick is the Sky Model's");
static_assert (static_cast<int> (Phase::quiet) == static_cast<int> (sky::SkyPhase::quiet)
               && static_cast<int> (Phase::growth) == static_cast<int> (sky::SkyPhase::growth)
               && static_cast<int> (Phase::onset) == static_cast<int> (sky::SkyPhase::onset)
               && static_cast<int> (Phase::recovery) == static_cast<int> (sky::SkyPhase::recovery),
               "Harmony's Phase mirrors the sky's");

namespace
{
constexpr double kPi = std::numbers::pi;
constexpr double kPanSlewSeconds = 0.5;
constexpr double kDisplacementForFullDetuneKm = 160.0;
constexpr double kDriftKnotSeconds = 20.0;

double midiHz (double note) noexcept { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

int minuteOf (double modelTimeUtc) noexcept { return static_cast<int> (std::floor (modelTimeUtc / 60.0)); }

/** Two band settings voice alike: the same edges, reach (the blend as it is rounded), tones and
    shifts. A blend move that keeps its rounding re-voices nothing. */
bool sameVoicing (const Bands& a, const Bands& b) noexcept
{
    for (int i = 0; i < BandEdges::kCount; ++i)
        if (a.edges.edge (i) != b.edges.edge (i))
            return false;
    return std::lround (a.edges.blend) == std::lround (b.edges.blend) && a.tones == b.tones && a.shift == b.shift;
}
} // namespace

void SkySound::prepare (double rate, int nodes, uint32_t soundSeed, const Settings& s, const Chord& start) noexcept
{
    settings = s;
    settings.voicesPerNode = std::clamp (s.voicesPerNode, 1, 2);
    settings.bands = clampBands (s.bands);
    sampleRate = rate;
    seed = soundSeed;
    numNodes = std::clamp (nodes, 2, sky::SkySnapshot::kMaxNodes);
    numVoices = std::min (kMaxVoices, numNodes * settings.voicesPerNode);

    resonators.prepare (numVoices, rate, seed);
    exciter.prepare (numVoices, settings.voicesPerNode, settings.exciter);

    for (int v = 0; v < numVoices; ++v)
    {
        pos[static_cast<size_t> (v)] = std::min (static_cast<double> (numNodes - 1), v / static_cast<double> (settings.voicesPerNode));
        slotOf[static_cast<size_t> (v)] = static_cast<int8_t> (v % kSlots);
    }
    gain.fill (0.0);
    brightness.fill (0.0);
    panL.fill (0.0); panR.fill (0.0); presence.fill (0.0);
    panNode.fill (0.0); presenceNode.fill (0.0);
    panInit = false;
    tickCount = 0;
    seeksSeen = 0;
    lastPhase = -1;

    cur = 0;
    xf = 1.0;
    breath01 = 0.0;
    xfStart = xfEnd = 0.0;
    dampStart = -1.0;
    dampT60 = kDampT60;
    settled = settings.bands;
    voicedWith.fill (settled);
    bandsMovedTick = 0;
    revoiceWanted = false;
    applied = start;
    pending = {};
    pendingValid = pendingUrgent = false;
    changes = 0;
    chordTick = 0;
    pendingFromHarmony = false;
    guardResets = 0;
    harmonyEngine.discardPending();
    voiceBank (0, applied, -1);
    resonators.setActive (0, true);
}

void SkySound::setSettings (const Settings& s) noexcept
{
    const int vpn = settings.voicesPerNode;
    const Bands bands = clampBands (s.bands);
    if (! sameVoicing (bands, settings.bands))
        bandsMovedTick = tickCount;   // the 300 ms count starts again (counted in ticks: rule 5)
    settings = s;
    settings.voicesPerNode = vpn;   // fixed by prepare()
    settings.bands = bands;
    exciter.setSettings (settings.exciter);
}

void SkySound::process (sky::SkyRuntime& runtime, float* left, float* right, int numSamples) noexcept
{
    outLeft = left;
    outRight = right;
    runtime.process (numSamples, this);
}

void SkySound::requestChord (const Chord& chord, bool urgent) noexcept
{
    pending = chord;
    pendingValid = true;
    pendingUrgent = urgent;
    pendingFromHarmony = false;
}

int SkySound::note (int bank, Layer layer, int voice) const noexcept
{
    return notes[static_cast<size_t> (bank)][static_cast<size_t> (layer)][static_cast<size_t> (voice)];
}

double SkySound::breathCycleAt (uint64_t tick) noexcept
{
    const double realS = static_cast<double> (tick) * Exciter::kTickSeconds;
    return 0.5 - 0.5 * std::cos (2.0 * kPi * kBreathHz * realS);
}

double SkySound::realSeconds() const noexcept
{
    return static_cast<double> (tickCount) * Exciter::kTickSeconds;
}

void SkySound::renderSamples (int offset, int numSamples) noexcept
{
    resonators.process (outLeft + offset, outRight + offset, numSamples);
    if (companion != nullptr)
        companion->renderSamples (outLeft + offset, outRight + offset, numSamples);
}

//==================================================================================================
void SkySound::skyTicked (const sky::SkyModel& model, std::span<const sky::SkyEvent> events, int tickSamples) noexcept
{
    const auto& sky = model.current();
    const auto& nd = sky.nodes;
    const auto& s = settings;
    const int V = numVoices, N = numNodes;
    const int vpn = s.voicesPerNode;
    constexpr double kTick = Exciter::kTickSeconds;

    ++tickCount;
    const double realS = realSeconds();

    // A seek since the last tick (T6.6): the sky jumped rather than moved. Seeks before the first
    // tick (a Journey's start, the render tool, the goldens) are the start, not a jump.
    const bool jumped = tickCount > 1 && model.seeks() != seeksSeen;
    seeksSeen = model.seeks();
    if (jumped && static_cast<int> (sky.phase) != lastPhase)
    {
        // Seeks emit no events: tell the harmony where it landed, so the progression moves there.
        const int minute = minuteOf (sky.modelTimeUtc);
        if (sky.phase == sky::SkyPhase::onset)
            harmonyEngine.onOnset (std::max (0.3f, sky.lastOnset.strength), minute);
        else
            harmonyEngine.onPhase (static_cast<Phase> (sky.phase), minute);
    }
    lastPhase = static_cast<int> (sky.phase);

    for (const auto& e : events)
    {
        if (e.type == sky::SkyEvent::Type::onset)
        {
            exciter.onset (realS, e.strength, e.node * vpn);
            harmonyEngine.onOnset (e.strength, minuteOf (e.modelTimeUtc));
        }
        else if (e.type == sky::SkyEvent::Type::phase && e.phase != sky::SkyPhase::onset)
        {
            harmonyEngine.onPhase (static_cast<Phase> (e.phase), minuteOf (e.modelTimeUtc));
        }
    }
    harmonyEngine.update (realS);
    if (! pendingValid && harmonyEngine.hasPending())
    {
        pending = harmonyEngine.pendingChord();
        pendingUrgent = harmonyEngine.pendingUrgent();
        pendingValid = true;
        pendingFromHarmony = true;
    }
    else if (pendingFromHarmony && pendingValid && harmonyEngine.hasPending())
    {
        // A key, Mood or Transpose change while the chord waits is heard when it lands.
        pending = harmonyEngine.pendingChord();
        pendingUrgent = harmonyEngine.pendingUrgent();
    }

    // A band change (T3.8), still for 300 ms: the chord is voiced again, through the crossfade, as a
    // key change by hand. A chord already waiting is voiced with the new bands when it lands.
    if (! sameVoicing (settings.bands, settled) && tickCount - bandsMovedTick >= static_cast<uint64_t> (kRevoiceStillTicks))
    {
        settled = settings.bands;
        revoiceWanted = true;
        if (! pendingValid)
        {
            pending = applied;
            pendingValid = true;
            pendingUrgent = true;
            pendingFromHarmony = false;
        }
    }

    // Chord changes: the old bank retires once silent (one T60 after the crossfade), then the next
    // may come. An urgent change doesn't wait: it ends the crossfade and damps the old bank over
    // kDampT60 instead (a re-voice over kRevoiceDampT60, so it is heard within a second).
    if (xf < 1.0)
    {
        xf = std::min (1.0, (realS - xfStart) / s.crossfadeSeconds);
        if (xf >= 1.0)
            xfEnd = realS;
    }
    const int other = 1 - cur;   // the sketch computes this before a change on this tick (see below)
    const double wantDamp = revoiceWanted ? kRevoiceDampT60 : kDampT60;
    if (pendingValid && (pendingUrgent || revoiceWanted) && resonators.isActive (other))
    {
        if (dampStart < 0.0)
        {
            if (xf < 1.0)
            {
                xf = 1.0;
                xfEnd = realS;
            }
            dampStart = realS;
            dampT60 = wantDamp;
        }
        else if (wantDamp < dampT60)
        {
            dampStart = realS;   // a re-voice shortens a damping under way
            dampT60 = wantDamp;
        }
    }
    const double ringOut = dampStart >= 0.0 ? dampT60 : std::max ({ s.t60[0], s.t60[1], s.t60[2] });
    const double since = dampStart >= 0.0 ? dampStart : xfEnd;
    if (resonators.isActive (other) && xf >= 1.0 && realS - since >= ringOut)
    {
        resonators.setActive (other, false);
        dampStart = -1.0;
    }
    if (pendingValid && xf >= 1.0 && ! resonators.isActive (other))
    {
        const bool fromHarmony = pendingFromHarmony;
        applyChord (realS);
        if (fromHarmony)
            harmonyEngine.take (realS);
    }

    // pan and presence slew toward the dome geometry (updated at 10 Hz)
    if (tickCount % 10 == 1 || ! panInit)
        geometry (model);
    const double panK = panInit ? 1.0 - std::exp (-kTick / kPanSlewSeconds) : 1.0;
    for (size_t v = 0; v < static_cast<size_t> (V); ++v)
    {
        const double x = pos[v];
        const auto i0 = static_cast<size_t> (x);
        const auto i1 = std::min (static_cast<size_t> (N - 1), i0 + 1);
        const double f = x - static_cast<double> (i0);
        const double pan = panNode[i0] + (panNode[i1] - panNode[i0]) * f;
        const double pres = presenceNode[i0] + (presenceNode[i1] - presenceNode[i0]) * f;
        const double th = ((pan + 1.0) * kPi) / 4.0;
        panL[v] += (std::cos (th) - panL[v]) * panK;
        panR[v] += (std::sin (th) - panR[v]) * panK;
        presence[v] += (pres - presence[v]) * panK;
    }
    panInit = true;

    // exciter
    for (size_t v = 0; v < static_cast<size_t> (V); ++v)
    {
        const double x = pos[v];
        const auto i0 = static_cast<size_t> (x);
        const auto i1 = std::min (static_cast<size_t> (N - 1), i0 + 1);
        const double f = x - static_cast<double> (i0);
        brightness[v] = nd[i0].brightness + (static_cast<double> (nd[i1].brightness) - nd[i0].brightness) * f;
    }
    if (jumped)
        exciter.rebase (brightness.data());
    exciter.tick (realS, sky.activity, brightness.data());

    const double gainK = 1.0 - std::exp (-kTick / s.gainSlewSeconds);
    breath01 = breathCycleAt (tickCount);   // the Breath source (T4.2: its routes move s.level)
    std::array<double, kNumLayers> radius {}, norm {};
    for (size_t l = 0; l < kNumLayers; ++l)
    {
        radius[l] = resonators.radius (s.t60[l]);
        norm[l] = std::sqrt (1.0 - radius[l] * radius[l]);
    }
    const double dampR = resonators.radius (dampT60);
    const double preGain = 2.0 / std::sqrt (3.0 * V);

    // `other` was computed before applyChord(): on the tick a chord is taken it names the new bank,
    // so the weights must use the fresh counterpart. With the stale index both writes land on the
    // new bank and it gets the whole bed for 10 ms out of a silent start (the T3.1 quirk, fixed in
    // T3.6: sketch and port together, golden re-exported).
    std::array<double, ResonatorBank::kBanks> w {};
    w[static_cast<size_t> (cur)] = std::sin (0.5 * kPi * xf);
    w[static_cast<size_t> (1 - cur)] = std::cos (0.5 * kPi * xf);

    for (int v = 0; v < V; ++v)
    {
        const auto vi = static_cast<size_t> (v);
        const double x = pos[vi];
        const auto i0 = static_cast<size_t> (x);
        const auto i1 = std::min (static_cast<size_t> (N - 1), i0 + 1);
        const double f = x - static_cast<double> (i0);
        const double y = nd[i0].displacementKm + (static_cast<double> (nd[i1].displacementKm) - nd[i0].displacementKm) * f;
        const double exc = exciter.continuous (v);
        const double strikeX = exciter.strike (v);   // the strike lands on the newest chord
        const double pl = panL[vi], pr = panR[vi];

        for (int l = 0; l < kNumLayers; ++l)
        {
            const auto li = static_cast<size_t> (l);
            const auto gi = static_cast<size_t> (l * V + v);
            const auto colour = [&nd, l] (size_t i) -> double
            {
                return l == 0 ? nd[i].red : l == 1 ? nd[i].green : nd[i].violet;
            };
            // √colour: a storm is brighter and wider, not 25 dB louder (auto-gain does the rest).
            // The level arrives modulated (T4.2: Breath's routes, Glow's on violet).
            const double target = s.level[li] * std::sqrt (colour (i0) + (colour (i1) - colour (i0)) * f) * presence[vi];
            gain[gi] += (target - gain[gi]) * gainK;
            const double g = gain[gi] * preGain;
            const double cents = s.detuneCents * (y / kDisplacementForFullDetuneKm)
                               + s.driftCents * sky::rng::noise1 (seed, 81u + static_cast<uint32_t> (l), realS / kDriftKnotSeconds + v * 3.7);

            for (int bk = 0; bk < ResonatorBank::kBanks; ++bk)
            {
                if (! resonators.isActive (bk))
                    continue;
                const int k = resonators.index (bk, l, v);
                const bool damped = bk != cur && dampStart >= 0.0;
                resonators.setInput (k, (exc * w[static_cast<size_t> (bk)] + (bk == cur ? strikeX : 0.0)) * norm[li]);
                resonators.tune (k, midiHz (notes[static_cast<size_t> (bk)][li][vi] + cents / 100.0),
                                 damped ? std::min (dampR, radius[li]) : radius[li]);
                resonators.ramp (k, g * pl, g * pr, tickSamples);
            }
        }
    }
    if (tickCount % 100 == 0)
        guardResets += resonators.guard();

    if (companion != nullptr)
        companion->skyTicked (model, events, *this, tickSamples);
}

/** Dome azimuth → pan (east left, like the dome); elevation → presence. 10 Hz. */
void SkySound::geometry (const sky::SkyModel& model) noexcept
{
    const auto& sky = model.current();
    const auto& obs = model.observer();
    const int last = std::max (0, sky.nodeCount - 1);
    for (int i = 0; i < numNodes; ++i)
    {
        const auto& n = sky.nodes[static_cast<size_t> (std::min (i, last))];
        const auto h = obs.azEl (obs.pointAt (n.mlatDeg, n.mlonDeg, kPanAltitudeKm));
        panNode[static_cast<size_t> (i)] = std::clamp (-std::sin ((h.azDeg * kPi) / 180.0) * settings.width, -1.0, 1.0);
        presenceNode[static_cast<size_t> (i)] = 0.45 + 0.55 * std::clamp (h.elDeg / 50.0, 0.0, 1.0);
    }
}

/** Voices a chord into `bank` with the settled bands (T3.8): from scratch (fromBank < 0), or led
    from `fromBank` (each voice keeps its slot and takes the nearest octave in its band). When the
    band itself moved, a voice keeps its place within the band (the old note mapped onto the new
    range), so a band that moves an octave keeps its voices spread rather than folded into one end. */
void SkySound::voiceBank (int bank, const Chord& chord, int fromBank) noexcept
{
    const auto b = static_cast<size_t> (bank);
    for (int l = 0; l < kNumLayers; ++l)
    {
        const auto li = static_cast<size_t> (l);
        const auto layer = static_cast<Layer> (l);
        const Slots next = bandSlots (chord, settled.tones[li], settled.shift[li]);
        const auto range = bandRange (settled.edges, layer);
        const auto was = fromBank < 0 ? range : bandRange (voicedWith[static_cast<size_t> (fromBank)].edges, layer);
        const bool moved = was.lo != range.lo || was.hi != range.hi;
        const double scale = was.hi > was.lo ? static_cast<double> (range.hi - range.lo) / (was.hi - was.lo) : 1.0;
        const Slots slots = fromBank < 0 ? next : leadSlots (slotPcs[static_cast<size_t> (fromBank)][li], next).slots;
        for (int v = 0; v < numVoices; ++v)
        {
            const auto vi = static_cast<size_t> (v);
            double near = 0.0;
            if (fromBank < 0)
                near = range.lo + sky::rng::rand01 (seed, 71u + static_cast<uint32_t> (l), static_cast<uint32_t> (v)) * (range.hi - range.lo);
            else
            {
                near = notes[static_cast<size_t> (fromBank)][li][vi];
                if (moved)
                    near = range.lo + (near - was.lo) * scale;
            }
            notes[b][li][vi] = static_cast<int16_t> (nearestNote (slots[static_cast<size_t> (slotOf[vi])], near, range.lo, range.hi));
        }
        slotPcs[b][li] = slots;
    }
    voicedWith[b] = settled;
}

void SkySound::applyChord (double realS) noexcept
{
    const int nb = 1 - cur;
    voiceBank (nb, pending, cur);
    resonators.clear (nb);
    resonators.setActive (nb, true);
    cur = nb;
    xf = 0.0;
    xfStart = realS;
    applied = pending;
    pendingValid = false;
    pendingFromHarmony = false;
    revoiceWanted = false;
    chordTick = tickCount;
    ++changes;
}

} // namespace worldtable::dsp
