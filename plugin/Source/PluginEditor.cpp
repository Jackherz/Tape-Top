#include "PluginEditor.h"
#include "Theme.h"

#include <cmath>

namespace {
constexpr int kTracks = tt::Deck::kTracks;
constexpr float kDesignWidth = 1180.0f;
constexpr float kDesignHeight = 720.0f;
constexpr double kBarSeconds = 2.5;

const char* kTrickNames[8] = { "LOOP IN", "LOOP OUT", "LOOP", "BREAK",
                               "REVERSE", "CHOP", "LIFT", "DROP" };
} // namespace

TapeTopEditor::TapeTopEditor (TapeTopProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    setSize (static_cast<int> (kDesignWidth), static_cast<int> (kDesignHeight));
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (kDesignWidth / kDesignHeight);
    setResizeLimits (900, 549, 2360, 1440);
    setWantsKeyboardFocus (true);

    for (auto& g : trackGain) g = 0.75f;
    snap = proc.snapshot();
    layoutPanel();
    startTimerHz (30);
    lastFrame = juce::Time::getMillisecondCounterHiRes() * 0.001;
}

// ---------------------------------------------------------------------------
void TapeTopEditor::layoutPanel()
{
    const auto bounds = getLocalBounds();
    scale = bounds.getWidth() / kDesignWidth;
    const auto s = [this] (int v) { return juce::roundToInt ((float) v * scale); };

    const juce::Rectangle<int> full (0, 0, bounds.getWidth(), bounds.getHeight());

    // rails
    const int headerY = s (12), headerH = s (46);
    const int footerY = bounds.getHeight() - s (36);
    const int mixerY = footerY - s (84);
    const int deckTop = headerY + headerH + s (12);

    const juce::Rectangle<int> leftRail (s (12), deckTop, s (232), mixerY - deckTop - s (12));
    const juce::Rectangle<int> rightRail (bounds.getWidth() - s (274), deckTop, s (262),
                                          mixerY - deckTop - s (12));
    const juce::Rectangle<int> tapeArea (leftRail.getRight() + s (12), deckTop,
                                         rightRail.getX() - leftRail.getRight() - s (24),
                                         mixerY - deckTop - s (12));

    // ---- left rail: stocks, transport, spec -----------------------------
    const int chipH = s (30), chipGap = s (5);
    const int styleTop = leftRail.getY() + s (34);
    for (int i = 0; i < 4; ++i)
        styleRects[static_cast<std::size_t> (i)] = { leftRail.getX() + s (10),
                                                     styleTop + i * (chipH + chipGap),
                                                     leftRail.getWidth() - s (20), chipH };

    const int transportTop = styleTop + 4 * (chipH + chipGap) + s (34);
    const juce::Rectangle<int> reelArea (leftRail.getX() + s (10), transportTop,
                                         leftRail.getWidth() - s (20), s (64));
    tapeHeadRect = reelArea;

    const int keysTop = transportTop + s (142);
    const int keyW = (leftRail.getWidth() - s (20) - s (16)) / 5;
    for (int i = 0; i < 5; ++i)
        keyRects[static_cast<std::size_t> (i)] = { leftRail.getX() + s (10) + i * (keyW + s (4)),
                                                   keysTop, keyW, s (30) };

    specRect = { leftRail.getX(), keysTop + s (44), leftRail.getWidth(),
                 leftRail.getBottom() - (keysTop + s (44)) };

    // ---- right rail: master, scope, glue --------------------------------
    const int knobSize = s (52);
    const int knobTop = rightRail.getY() + s (34);
    const int knobGap = (rightRail.getWidth() - s (20) - 3 * knobSize) / 2;
    for (int i = 0; i < 3; ++i)
        knobRects[static_cast<std::size_t> (i)] = { rightRail.getX() + s (10) + i * (knobSize + knobGap),
                                                    knobTop, knobSize, knobSize };
    knobRects[3] = { rightRail.getX() + s (10), knobTop + s (74), knobSize, knobSize };

    scopeRect = { rightRail.getX() + s (10), knobTop + s (150), rightRail.getWidth() - s (20), s (74) };
    glueRect  = { rightRail.getX() + s (10), scopeRect.getBottom() + s (34),
                  rightRail.getWidth() - s (20), s (30) };

    // ---- tape area -------------------------------------------------------
    const int lanesTop = tapeArea.getY() + s (36);
    const int lanesBottom = tapeArea.getBottom() - s (96);
    const int laneH = (lanesBottom - lanesTop) / kTracks;
    for (int i = 0; i < kTracks; ++i)
        laneRects[static_cast<std::size_t> (i)] = { tapeArea.getX(), lanesTop + i * laneH,
                                                    tapeArea.getWidth(), laneH };

    const int trickW = (tapeArea.getWidth() - 7 * s (4)) / 8;
    for (int i = 0; i < 8; ++i)
        trickRects[static_cast<std::size_t> (i)] = { tapeArea.getX() + i * (trickW + s (4)),
                                                     lanesBottom + s (18), trickW, s (48) };

    setInRect    = { tapeArea.getX(), trickRects[0].getBottom() + s (6), s (74), s (20) };
    setOutRect   = { setInRect.getRight() + s (4), setInRect.getY(), s (78), s (20) };
    loopToggleRect = { setOutRect.getRight() + s (4), setInRect.getY(), s (58), s (20) };

    // ---- mixer -----------------------------------------------------------
    const int stripW = (bounds.getWidth() - s (24) - s (86) - 7 * s (6)) / 8;
    for (int i = 0; i < kTracks; ++i)
        stripRects[static_cast<std::size_t> (i)] = { s (12) + s (86) + i * (stripW + s (6)),
                                                     mixerY + s (12), stripW, s (60) };
}

void TapeTopEditor::resized()
{
    layoutPanel();
    buildBackground();
}

// ---------------------------------------------------------------------------
// The static layer is cached: dot matrix, panel outlines, screws, header.
// ---------------------------------------------------------------------------
void TapeTopEditor::buildBackground()
{
    const auto w = getWidth(), h = getHeight();
    background = juce::Image (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);
    juce::Graphics g (background);

    g.fillAll (theme::black);
    juce::ColourGradient bgGrad (theme::panel.brighter (0.06f), 0.0f, 0.0f,
                                 theme::black, 0.0f, (float) h, false);
    g.setGradientFill (bgGrad);
    g.fillRect (0, 0, w, h);

    // dot matrix
    g.setColour (theme::silk.withAlpha (0.05f));
    const int dot = juce::jmax (1, juce::roundToInt (1.0f * scale));
    for (int y = juce::roundToInt (8 * scale); y < h; y += juce::roundToInt (7 * scale))
        for (int x = juce::roundToInt (8 * scale); x < w; x += juce::roundToInt (7 * scale))
            g.fillRect (x, y, dot, dot);

    // outer frame + screws
    g.setColour (theme::silk.withAlpha (0.22f));
    g.drawRect (juce::Rectangle<int> (0, 0, w, h), 1);
    g.setColour (theme::silk.withAlpha (0.30f));
    for (auto pt : { juce::Point<int> (7, 7), juce::Point<int> (w - 7, 7),
                     juce::Point<int> (7, h - 7), juce::Point<int> (w - 7, h - 7) })
        g.fillEllipse ((float) pt.x - 2.0f, (float) pt.y - 2.0f, 4.0f, 4.0f);
}

// ---------------------------------------------------------------------------
void TapeTopEditor::paint (juce::Graphics& g)
{
    if (! background.isValid()) buildBackground();
    g.drawImageAt (background, 0, 0);

    const auto accent = theme::stockColours[juce::jlimit (0, 3, snap.styleIndex)];
    const auto s = [this] (int v) { return juce::roundToInt ((float) v * scale); };

    // ---- header ---------------------------------------------------------
    const juce::Rectangle<int> header (s (12), s (12), getWidth() - s (24), s (46));
    g.setColour (theme::silk.withAlpha (0.14f));
    g.drawLine (0.0f, (float) header.getBottom(), (float) getWidth(), (float) header.getBottom(), 1.0f);

    g.setColour (theme::silk);
    g.setFont (theme::ui (s (19), true));
    g.drawText ("TAPE-TOP", header.getX(), header.getY() + s (4), s (200), s (20),
                juce::Justification::centredLeft, false);
    theme::label (g, "eight track tape instrument  ·  model TT-08",
                  header.withTrimmedTop (s (26)).withHeight (s (12)), theme::dim, s (8), 1.5f * scale);

    // serial block, right aligned
    const int serialX = header.getRight() - s (250);
    g.setColour (theme::silk.withAlpha (0.45f));
    for (int i = 0; i < 42; ++i)
    {
        const int bx = serialX + s (i * 3);
        const int bh = s (6 + ((i * 7) % 9));
        g.fillRect (bx, header.getY() + s (4), juce::jmax (1, s (1)), bh);
    }
    g.setFont (theme::mono (s (8)));
    g.setColour (theme::dim);
    g.drawText ("SN 0042-TT", header.getX() + s (196), header.getY() + s (4), s (120), s (10),
                juce::Justification::centredLeft, false);
    g.drawText ("32 BIT / 96 K", header.getX() + s (196), header.getY() + s (16), s (140), s (10),
                juce::Justification::centredLeft, false);

    // ---- left rail -------------------------------------------------------
    const juce::Rectangle<int> leftRail (s (12), s (70), s (232), specRect.getBottom() - s (70));
    theme::plate (g, leftRail, "TAPE STOCK");

    const char* stockNames[4] = { "STUDIO", "VINTAGE", "PORTA", "DISK MINI" };
    const char* stockSub[4] = { "15 IPS", "7.5 IPS", "3.75 IPS", "MO DISC" };
    for (int i = 0; i < 4; ++i)
    {
        const bool on = snap.styleIndex == i;
        const auto r = styleRects[static_cast<std::size_t> (i)];
        const auto c = theme::stockColours[i];
        g.setColour (on ? c : theme::line);
        g.drawRoundedRectangle (r.toFloat(), 2.0f, 1.0f);
        if (on) { g.setColour (c.withAlpha (0.10f)); g.fillRoundedRectangle (r.toFloat(), 2.0f); }
        g.setColour (on ? c : theme::faint);
        g.fillEllipse ((float) r.getX() + s (7), (float) r.getCentreY() - s (3), s (6), s (6));
        g.setColour (on ? theme::silk : theme::dim);
        g.setFont (theme::ui (s (9.5f)));
        g.drawText (stockNames[i], r.withTrimmedLeft (s (20)), juce::Justification::centredLeft, false);
        g.setFont (theme::mono (s (8)));
        g.setColour (theme::faint);
        g.drawText (stockSub[i], r.withTrimmedRight (s (8)), juce::Justification::centredRight, false);
    }

    // transport plate
    const juce::Rectangle<int> transportPlate (s (12), styleRects[3].getBottom() + s (10), s (232),
                                               juce::jmax (0, specRect.getY() - s (8)
                                                                  - (styleRects[3].getBottom() + s (10))));
    theme::plate (g, transportPlate, "TRANSPORT");

    drawReels (g, tapeHeadRect);

    // counter
    const juce::Rectangle<int> counter (s (12), tapeHeadRect.getBottom() + s (6), s (232), s (44));
    g.setColour (theme::silk);
    g.setFont (theme::mono (s (23)));
    g.drawText (timecode (snap.position), counter, juce::Justification::centred, false);
    g.setFont (theme::mono (s (8)));
    g.setColour (theme::dim);
    const int bar = (int) (snap.position / kBarSeconds) + 1;
    const int beat = (int) (((snap.position / kBarSeconds) - (bar - 1)) * 4.0) + 1;
    const juce::String state = snap.recording ? "RECORDING"
                             : snap.reversed ? "REVERSE"
                             : snap.playing ? "ROLLING" : "STOPPED";
    g.drawText (juce::String (bar).paddedLeft ('0', 3) + " . " + juce::String (beat)
                    + "      " + state,
                counter.withTrimmedTop (s (28)).withHeight (s (12)),
                juce::Justification::centred, false);

    for (int i = 0; i < 5; ++i)
        drawKey (g, keyRects[static_cast<std::size_t> (i)], i,
                 (i == 2 && snap.playing) || (i == 3 && snap.recording));

    // tape speed readout
    const juce::Rectangle<int> speedRow (s (22), keyRects[0].getBottom() + s (12), s (212), s (20));
    g.setFont (theme::mono (s (10)));
    g.setColour (theme::silk);
    g.drawText (juce::String (proc.speedParam->get(), 2) + " x", speedRow,
                juce::Justification::centredRight, false);
    theme::label (g, "tape speed", speedRow, theme::dim, s (8), 1.4f * scale);

    // spec panel
    theme::plate (g, specRect, "STOCK SPEC");
    const juce::String spec[6][2] = {
        { "SPEED",   juce::String (snap.styleIndex == 0 ? 15.0 : snap.styleIndex == 1 ? 7.5
                                    : snap.styleIndex == 2 ? 3.75 : 7.5, 2) + " IPS" },
        { "GAP",     juce::String (snap.styleIndex == 2 ? 3.6 : 2.2, 1) + " um" },
        { "BUMP",    juce::String (snap.styleIndex == 2 ? 92 : 62) + " Hz" },
        { "HISS",    juce::String (snap.styleIndex == 2 ? -52 : -68) + " dB" },
        { "TAKES",   juce::String (snap.takeCount) },
        { "TAPE",    juce::String (snap.duration, 1) + " s" },
    };
    int y = specRect.getY() + s (26);
    for (auto& row : spec)
    {
        theme::label (g, row[0], { specRect.getX() + s (10), y, s (90), s (11) }, theme::faint, s (7.5f), 1.3f * scale);
        g.setFont (theme::mono (s (9)));
        g.setColour (theme::silk);
        g.drawText (row[1], { specRect.getX() + s (100), y, s (120), s (11) },
                    juce::Justification::centredRight, false);
        y += s (17);
    }

    // ---- tape ------------------------------------------------------------
    drawLanes (g);

    for (int i = 0; i < 8; ++i)
    {
        bool on = false;
        switch (i)
        {
            case 0: case 1: on = snap.loopIn != snap.loopOut; break;
            case 2: on = snap.looping || snap.chopping; break;
            case 4: on = snap.reversed; break;
            case 5: on = snap.chopping; break;
            default: break;
        }
        theme::chip (g, trickRects[static_cast<std::size_t> (i)], kTrickNames[i], on, accent,
                     hoverIndex == 100 + i);
    }

    theme::chip (g, setInRect, "LOOP IN", false, accent, hoverIndex == 200);
    theme::chip (g, setOutRect, "LOOP OUT", false, accent, hoverIndex == 201);
    theme::chip (g, loopToggleRect, "LOOP", snap.looping, accent, hoverIndex == 202);

    g.setFont (theme::mono (s (8)));
    g.setColour (theme::dim);
    g.drawText (snap.loopIn != snap.loopOut
                    ? juce::String (snap.loopIn, 2) + " -> " + juce::String (snap.loopOut, 2) + " s"
                    : juce::String ("-"),
                { loopToggleRect.getRight() + s (10), loopToggleRect.getY(), s (150), s (20) },
                juce::Justification::centredLeft, false);

    // ---- right rail -------------------------------------------------------
    const juce::Rectangle<int> masterPlate (s (906), s (70), s (262), s (150));
    theme::plate (g, masterPlate, "MASTER");
    const char* eqNames[3] = { "LOW", "MID", "HIGH" };
    for (int i = 0; i < 3; ++i)
    {
        const auto r = knobRects[static_cast<std::size_t> (i)];
        theme::knob (g, r, eqValues[i], accent, dragIndex == i);
        theme::label (g, eqNames[i], { r.getX() - s (6), r.getBottom() + s (2), r.getWidth() + s (12), s (10) },
                      theme::dim, s (7), 1.3f * scale);
        g.setFont (theme::mono (s (7.5f)));
        g.setColour (theme::silk);
        g.drawText (juce::String ((eqValues[i] - 0.5f) * 24.0f, 1),
                    { r.getX() - s (6), r.getBottom() + s (12), r.getWidth() + s (12), s (10) },
                    juce::Justification::centred, false);
    }

    const auto outR = knobRects[3];
    theme::knob (g, outR, (proc.masterParam->get() + 24.0f) / 36.0f, accent, dragIndex == 3);
    theme::label (g, "OUTPUT", { outR.getX(), outR.getY() - s (10), outR.getWidth(), s (10) },
                  theme::dim, s (7), 1.3f * scale);

    // output meters
    for (int ch = 0; ch < 2; ++ch)
    {
        const float lvl = juce::jlimit (0.0f, 1.0f,
                                       ch == 0 ? snap.levels[0] * 3.2f : snap.levels[1] * 3.2f);
        theme::meter (g, { masterPlate.getRight() - s (20) + ch * s (9), masterPlate.getY() + s (28),
                           s (6), s (110) }, lvl);
    }

    const juce::Rectangle<int> scopePlate (s (906), scopeRect.getY() - s (30), s (262),
                                           scopeRect.getHeight() + s (40));
    theme::plate (g, scopePlate, "SCOPE");
    drawScope (g, scopeRect);

    const juce::Rectangle<int> gluePlate (s (906), glueRect.getY() - s (30), s (262), s (74));
    theme::plate (g, gluePlate, "GLUE");
    {
        float sum = 0;
        for (auto l : snap.levels) sum += l;
        const float glue = juce::jlimit (0.0f, 1.0f, sum * 0.45f);
        const auto bar = glueRect.withHeight (s (7)).toFloat();
        g.setColour (theme::faint);
        g.drawRect (bar, 1.0f);
        g.setColour (accent.withAlpha (0.85f));
        g.fillRect (bar.withWidth (bar.getWidth() * glue).reduced (1.0f));
    }

    // ---- mixer ------------------------------------------------------------
    const juce::Rectangle<int> mixerBar (0, stripRects[0].getY() - s (12), getWidth(), s (84));
    g.setColour (theme::silk.withAlpha (0.12f));
    g.drawLine (0.0f, (float) mixerBar.getY(), (float) getWidth(), (float) mixerBar.getY(), 1.0f);
    theme::label (g, "strips", { s (12), mixerBar.getY() + s (12), s (80), s (11) }, theme::dim, s (8), 1.6f * scale);

    for (int i = 0; i < kTracks; ++i)
    {
        const auto r = stripRects[static_cast<std::size_t> (i)];
        const auto col = theme::trackColours[i];
        g.setColour (i == selectedTrack ? col.withAlpha (0.85f) : theme::line);
        g.drawRoundedRectangle (r.toFloat(), 2.0f, 1.0f);

        g.setFont (theme::mono (s (8)));
        g.setColour (snap.armed[static_cast<std::size_t> (i)] ? theme::red : theme::dim);
        g.drawText ("T" + juce::String (i + 1), r.withTrimmedLeft (s (5)).withHeight (s (12)).translated (0, s (4)),
                    juce::Justification::centredLeft, false);

        const juce::Rectangle<int> knobArea (r.getX() + s (4), r.getY() + s (16), s (34), s (34));
        theme::knob (g, knobArea, trackGain[i], col, dragIndex == 10 + i);

        theme::meter (g, { r.getRight() - s (11), r.getY() + s (12), s (5), s (40) },
                      juce::jlimit (0.0f, 1.0f, snap.levels[static_cast<std::size_t> (i)] * 3.0f));
    }

    // ---- footer ------------------------------------------------------------
    g.setColour (theme::silk.withAlpha (0.10f));
    g.drawLine (0.0f, (float) (getHeight() - s (30)), (float) getWidth(), (float) (getHeight() - s (30)), 1.0f);
    theme::label (g, "independent instrument - not affiliated with teenage engineering",
                  { s (12), getHeight() - s (24), s (620), s (14) }, theme::faint, s (7), 1.2f * scale);
    theme::label (g, "space play/stop   1-8 tricks   drag knobs",
                  { getWidth() - s (400), getHeight() - s (24), s (388), s (14) },
                  theme::faint, s (7), 1.2f * scale);
}

// ---------------------------------------------------------------------------
void TapeTopEditor::drawReels (juce::Graphics& g, juce::Rectangle<int> area)
{
    const float r = (float) area.getHeight() * 0.42f;
    for (int i = 0; i < 2; ++i)
    {
        const auto c = juce::Point<float> (i == 0 ? (float) area.getX() + r + 4.0f
                                                  : (float) area.getRight() - r - 4.0f,
                                           (float) area.getCentreY());
        g.setColour (theme::silk.withAlpha (0.30f));
        g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.0f);
        g.setColour (theme::silk.withAlpha (0.45f));
        g.drawEllipse (c.x - r * 0.38f, c.y - r * 0.38f, r * 0.76f, r * 0.76f, 1.0f);

        g.setColour (theme::silk.withAlpha (0.40f));
        for (int k = 0; k < 5; ++k)
        {
            const float a = (float) reelPhase + k * juce::MathConstants<float>::twoPi / 5.0f;
            const auto p1 = c.getPointOnCircumference (r * 0.38f, a);
            const auto p2 = c.getPointOnCircumference (r * 0.92f, a);
            g.drawLine ({ p1, p2 }, 1.4f);
        }
    }

    // tape path between the reels
    g.setColour (theme::silk.withAlpha (0.16f));
    g.drawLine ((float) area.getX() + r * 2.0f + 6.0f, (float) area.getCentreY(),
                (float) area.getRight() - r * 2.0f - 6.0f, (float) area.getCentreY(), 1.0f);
    const auto accent = theme::stockColours[juce::jlimit (0, 3, snap.styleIndex)];
    g.setColour (accent);
    g.fillRect ((float) area.getCentreX() - 3.0f, (float) area.getCentreY() - 9.0f, 6.0f, 18.0f);
}

void TapeTopEditor::drawKey (juce::Graphics& g, juce::Rectangle<int> r, int index, bool active)
{
    const auto accent = index == 3 ? theme::red : theme::green;
    g.setColour (active ? accent : theme::silk.withAlpha (0.18f));
    g.drawRoundedRectangle (r.toFloat(), 2.0f, 1.0f);
    if (active) { g.setColour (accent.withAlpha (0.10f)); g.fillRoundedRectangle (r.toFloat(), 2.0f); }

    g.setColour (active ? accent : theme::dim);
    const auto c = r.toFloat().getCentre();
    const float u = (float) r.getHeight() * 0.26f;
    juce::Path p;
    switch (index)
    {
        case 0: // rewind
            p.addTriangle (c.x + u, c.y - u, c.x - u, c.y, c.x + u, c.y + u);
            g.fillPath (p);
            g.fillRect (c.x + u * 0.75f, c.y - u, u * 0.35f, u * 2.0f);
            break;
        case 1: // stop
            g.fillRect (c.x - u * 0.7f, c.y - u * 0.7f, u * 1.4f, u * 1.4f);
            break;
        case 2: // play
            p.addTriangle (c.x - u * 0.65f, c.y - u, c.x + u, c.y, c.x - u * 0.65f, c.y + u);
            g.fillPath (p);
            break;
        case 3: // record
            g.fillEllipse (c.x - u * 0.8f, c.y - u * 0.8f, u * 1.6f, u * 1.6f);
            break;
        default: // fast forward
            p.addTriangle (c.x - u, c.y - u, c.x + u, c.y, c.x - u, c.y + u);
            g.fillPath (p);
            g.fillRect (c.x - u * 1.1f, c.y - u, u * 0.35f, u * 2.0f);
            break;
    }
}

// ---------------------------------------------------------------------------
void TapeTopEditor::drawLanes (juce::Graphics& g)
{
    const auto accent = theme::stockColours[juce::jlimit (0, 3, snap.styleIndex)];
    const double duration = juce::jmax (1.0, snap.duration);

    // header row
    const auto first = laneRects[0];
    theme::label (g, "tracks", { first.getX(), first.getY() - s (24), s (60), s (14) },
                  theme::dim, s (8), 1.6f * scale);
    for (int i = 0; i < 3; ++i)
    {
        const char* names[3] = { "BAR", "BEAT", "STEP" };
        juce::Rectangle<int> r (first.getX() + s (62) + i * s (46), first.getY() - s (26), s (42), s (18));
        theme::chip (g, r, names[i], i == 0, accent);
    }

    for (int i = 0; i < kTracks; ++i)
    {
        const auto lane = laneRects[static_cast<std::size_t> (i)];
        const auto idArea = lane.removeFromLeft (s (54));
        const auto col = theme::trackColours[i];

        if (i > 0)
        {
            g.setColour (theme::silk.withAlpha (0.06f));
            g.drawLine ((float) lane.getX() - s (54), (float) lane.getY(), (float) lane.getRight(),
                        (float) lane.getY(), 1.0f);
        }

        // track label + arm dot
        g.setFont (theme::mono (s (9)));
        g.setColour (snap.armed[static_cast<std::size_t> (i)] ? theme::red : theme::dim);
        g.drawText ("T" + juce::String (i + 1), idArea.withTrimmedLeft (s (4)).withHeight (s (14))
                                                   .translated (0, (idArea.getHeight() - s (14)) / 2),
                    juce::Justification::centredLeft, false);
        g.setColour (snap.armed[static_cast<std::size_t> (i)] ? theme::red : theme::faint);
        g.fillEllipse ((float) idArea.getX() + s (24), (float) idArea.getCentreY() - s (3), s (6), s (6));

        // grid
        g.setColour (theme::silk.withAlpha (0.07f));
        for (double t = 0.0; t <= duration; t += kBarSeconds)
        {
            const float x = (float) lane.getX() + (float) (t / duration) * (float) lane.getWidth();
            g.drawLine (x, (float) lane.getY(), x, (float) lane.getBottom(), 1.0f);
        }

        // loop shading
        if (snap.loopOut > snap.loopIn)
        {
            const float x0 = (float) lane.getX() + (float) (snap.loopIn / duration) * (float) lane.getWidth();
            const float x1 = (float) lane.getX() + (float) (snap.loopOut / duration) * (float) lane.getWidth();
            g.setColour (accent.withAlpha (snap.looping ? 0.10f : 0.04f));
            g.fillRect (juce::Rectangle<float> (x0, (float) lane.getY(), x1 - x0, (float) lane.getHeight()));
            g.setColour (accent.withAlpha (snap.looping ? 0.75f : 0.30f));
            g.drawLine (x0, (float) lane.getY(), x0, (float) lane.getBottom(), 1.0f);
            g.drawLine (x1, (float) lane.getY(), x1, (float) lane.getBottom(), 1.0f);
        }

        // takes
        for (const auto& take : takes)
        {
            if (take.track != i) continue;
            const float x0 = (float) lane.getX() + (float) (take.start / duration) * (float) lane.getWidth();
            const float x1 = (float) lane.getX() + (float) (take.end / duration) * (float) lane.getWidth();
            juce::Rectangle<float> block (x0, (float) lane.getY() + 4.0f,
                                          juce::jmax (2.0f, x1 - x0), (float) lane.getHeight() - 8.0f);
            g.setColour (col.withAlpha (0.06f));
            g.fillRect (block);
            g.setColour (col.withAlpha (0.65f));
            g.drawRect (block, 1.0f);

            // hatch: stand-in for a waveform until the medium exposes one
            g.setColour (col.withAlpha (0.35f));
            const float mid = block.getCentreY();
            for (float x = block.getX() + 3.0f; x < block.getRight() - 2.0f; x += 3.0f * scale)
            {
                const float h = (block.getHeight() * 0.36f)
                              * std::abs (std::sin (x * 0.21f) * std::cos (x * 0.07f));
                g.drawLine (x, mid - h, x, mid + h, 1.0f);
            }
        }

        // playhead
        const float px = (float) lane.getX() + (float) (snap.position / duration) * (float) lane.getWidth();
        g.setColour (theme::silk.withAlpha (0.85f));
        g.drawLine (px, (float) lane.getY(), px, (float) lane.getBottom(), 1.0f);

        // live level
        const float lvl = juce::jlimit (0.0f, 1.0f, snap.levels[static_cast<std::size_t> (i)] * 3.0f);
        if (lvl > 0.02f)
        {
            g.setColour (col.withAlpha (0.25f * lvl));
            g.fillRect (juce::Rectangle<float> (px - 16.0f * scale, (float) lane.getY(),
                                                32.0f * scale, (float) lane.getHeight()));
        }
    }
}

void TapeTopEditor::drawScope (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (theme::silk.withAlpha (0.08f));
    for (int y = area.getY() + 12; y < area.getBottom(); y += 16)
        g.drawLine ((float) area.getX(), (float) y, (float) area.getRight(), (float) y, 1.0f);

    juce::Path p;
    const auto accent = theme::stockColours[juce::jlimit (0, 3, snap.styleIndex)];
    for (int i = 0; i < 96; ++i)
    {
        const float x = (float) area.getX() + (float) i / 95.0f * (float) area.getWidth();
        const float level = (snap.levels[(std::size_t) ((i / 12) % 8)] * 1.6f)
                          + 0.06f * (float) std::sin (i * 0.55f + reelPhase * 3.0);
        const float y = (float) area.getBottom() - juce::jlimit (0.0f, 1.0f, level) * (float) area.getHeight();
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (accent);
    g.strokePath (p, juce::PathStrokeType (1.3f));
}

// ---------------------------------------------------------------------------
juce::String TapeTopEditor::timecode (double seconds) const
{
    const int total = (int) juce::jmax (0.0, seconds);
    const int frames = (int) ((seconds - total) * 25.0);
    return juce::String (total / 60).paddedLeft ('0', 2) + ":"
         + juce::String (total % 60).paddedLeft ('0', 2) + ":"
         + juce::String (frames).paddedLeft ('0', 2);
}

void TapeTopEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = juce::jlimit (0.0, 0.2, now - lastFrame);
    lastFrame = now;

    snap = proc.snapshot();
    proc.copyTakes (takes);

    if (snap.playing)
        reelPhase += dt * juce::jmax (0.05, (double) proc.speedParam->get()) * 2.2
                   * (snap.reversed ? -1.0 : 1.0);

    for (int i = 0; i < 8; ++i)
        levelPeak[i] = juce::jmax (snap.levels[(std::size_t) i], levelPeak[i] * 0.94f);

    repaint();
}

// ---------------------------------------------------------------------------
void TapeTopEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();

    for (int i = 0; i < 4; ++i)
        if (styleRects[static_cast<std::size_t> (i)].contains (p)) { proc.setStyle (i); return; }

    for (int i = 0; i < 5; ++i)
        if (keyRects[static_cast<std::size_t> (i)].contains (p))
        {
            switch (i)
            {
                case 0: proc.seek (snap.position - 2.0); break;
                case 1: proc.stop(); proc.seek (0.0); break;
                case 2: snap.playing ? proc.stop() : proc.play(); break;
                case 3: proc.armTrack (selectedTrack, ! proc.isArmed (selectedTrack));
                        proc.setRecording (! snap.recording); break;
                default: proc.seek (snap.position + 2.0); break;
            }
            return;
        }

    if (setInRect.contains (p))  { proc.setLoopIn(); return; }
    if (setOutRect.contains (p)) { proc.setLoopOut(); return; }
    if (loopToggleRect.contains (p)) { proc.setLoop (! snap.looping); return; }

    for (int i = 0; i < 8; ++i)
        if (trickRects[static_cast<std::size_t> (i)].contains (p))
        {
            handleTrick (i);
            return;
        }

    for (int i = 0; i < kTracks; ++i)
    {
        const auto lane = laneRects[static_cast<std::size_t> (i)];
        if (lane.contains (p))
        {
            if (p.getX() < lane.getX() + juce::roundToInt (54 * scale))
                proc.armTrack (i, ! proc.isArmed (i));
            else
            {
                selectedTrack = i;
                const double frac = (double) (p.getX() - lane.getX() - (int) (54 * scale))
                                  / (double) juce::jmax (1, lane.getWidth() - (int) (54 * scale));
                proc.seek (juce::jlimit (0.0, snap.duration, frac * snap.duration));
            }
            return;
        }
    }

    // knobs and strips -> begin a drag
    for (int i = 0; i < 3; ++i)
        if (knobRects[static_cast<std::size_t> (i)].expanded (4).contains (p))
        {
            dragIndex = i;
            dragStartValue = eqValues[i];
            dragStartY = p.y;
            return;
        }
    if (knobRects[3].expanded (4).contains (p))
    {
        dragIndex = 3;
        dragStartValue = (proc.masterParam->get() + 24.0f) / 36.0f;
        dragStartY = p.y;
        return;
    }
    for (int i = 0; i < kTracks; ++i)
        if (stripRects[static_cast<std::size_t> (i)].contains (p))
        {
            if (p.getX() < stripRects[static_cast<std::size_t> (i)].getX() + juce::roundToInt (40 * scale))
            {
                dragIndex = 10 + i;
                dragStartValue = trackGain[i];
                dragStartY = p.y;
            }
            selectedTrack = i;
            return;
        }
}

void TapeTopEditor::handleTrick (int index)
{
    switch (index)
    {
        case 0: proc.setLoopIn(); break;
        case 1: proc.setLoopOut(); break;
        case 2: proc.setLoop (! snap.looping); break;
        case 3: proc.setBreak (true); break;          // momentary; released in mouseUp
        case 4: proc.toggleReverse(); break;
        case 5: proc.toggleChop(); break;
        case 6: proc.lift (selectedTrack); break;
        case 7: proc.drop (selectedTrack); break;
        default: break;
    }
}

void TapeTopEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIndex < 0) return;
    const float delta = (float) (dragStartY - e.getPosition().y) / (140.0f * juce::jmax (0.5f, scale));
    const float value = juce::jlimit (0.0f, 1.0f, dragStartValue + delta);

    if (dragIndex < 3)
    {
        eqValues[dragIndex] = value;
        proc.setMasterEq ((eqValues[0] - 0.5f) * 24.0f, (eqValues[1] - 0.5f) * 24.0f,
                          (eqValues[2] - 0.5f) * 24.0f);
    }
    else if (dragIndex == 3)
    {
        const float db = value * 36.0f - 24.0f;
        proc.masterParam->setValueNotifyingHost (proc.masterParam->convertTo0to1 (db));
    }
    else if (dragIndex >= 10)
    {
        const int track = dragIndex - 10;
        trackGain[track] = value;
        proc.setTrackGainDb (track, value * 24.0f - 12.0f);
    }
}

void TapeTopEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragIndex == -1)
        proc.setBreak (false);        // momentary: released
    dragIndex = -1;
}

void TapeTopEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    int hover = -1;
    for (int i = 0; i < 8; ++i)
        if (trickRects[static_cast<std::size_t> (i)].contains (p)) hover = 100 + i;
    if (setInRect.contains (p)) hover = 200;
    if (setOutRect.contains (p)) hover = 201;
    if (loopToggleRect.contains (p)) hover = 202;
    if (hover != hoverIndex) { hoverIndex = hover; repaint(); }
}

bool TapeTopEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey) { snap.playing ? proc.stop() : proc.play(); return true; }

    const int number = key.getTextCharacter() - '0';
    if (number >= 1 && number <= 8)
    {
        handleTrick (number - 1);
        return true;
    }
    return false;
}
