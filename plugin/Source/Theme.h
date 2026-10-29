#pragma once

// Tape-Top — shared visual language.
//
// Cream silkscreen on charcoal; colour reserved for signal and state; one
// grotesque for labels, one monospace for every number. These tokens match
// ui/ui.css so the prototype and the plugin cannot drift apart.

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme {

inline const juce::Colour black   { 0xff0a0b09 };
inline const juce::Colour panel   { 0xff121410 };
inline const juce::Colour silk    { 0xffe9e4d2 };
inline const juce::Colour dim     { 0x66e9e4d2 };
inline const juce::Colour faint   { 0x2ee9e4d2 };
inline const juce::Colour line    { 0x24e9e4d2 };

inline const juce::Colour orange  { 0xffe2622a };
inline const juce::Colour cyan    { 0xff3fb6e8 };
inline const juce::Colour green   { 0xff57c46a };
inline const juce::Colour purple  { 0xff8a6be0 };
inline const juce::Colour red     { 0xffd8452e };
inline const juce::Colour ochre   { 0xffc9a227 };

inline const juce::Colour stockColours[4] { cyan, orange, green, purple };
inline const juce::Colour trackColours[8] { orange, ochre, green, cyan, purple, red, silk, green };

inline juce::Font ui (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions { }.withHeight (height)
                                              .withStyle (bold ? "Bold" : "Regular"));
}

inline juce::Font mono (float height)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                          height, juce::Font::plain));
}

// Letterspaced uppercase label: JUCE has no tracking, so labels are drawn one
// character at a time. Cheap, and it is most of the character of the panel.
inline void label (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                   juce::Colour colour, float size = 9.0f, float tracking = 1.6f)
{
    g.setColour (colour);
    g.setFont (ui (size, false));
    const auto upper = text.toUpperCase();
    float x = static_cast<float> (area.getX());
    const float y = static_cast<float> (area.getY());
    for (auto c : upper)
    {
        const juce::String s (juce::String::charToString (c));
        const auto w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), s);
        g.drawText (s, juce::Rectangle<float> (x, y, w + 2.0f, (float) area.getHeight()),
                    juce::Justification::centredLeft, false);
        x += w + tracking;
    }
}

inline void plate (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
{
    g.setColour (theme::line);
    g.drawRect (area, 1);
    g.setColour (theme::panel.brighter (0.04f));
    g.fillRect (area.reduced (1));
    label (g, title, area.withTrimmedLeft (9).withTrimmedTop (7).withHeight (12), theme::dim, 8.0f, 2.0f);
}

inline void chip (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text,
                  bool on, juce::Colour accent, bool hover = false)
{
    const auto f = r.toFloat().reduced (0.5f);
    g.setColour (on ? accent : (hover ? accent.withAlpha (0.55f) : theme::line));
    g.drawRoundedRectangle (f, 2.0f, 1.0f);
    if (on)
    {
        g.setColour (accent.withAlpha (0.10f));
        g.fillRoundedRectangle (f, 2.0f);
    }
    g.setColour (on ? theme::silk : theme::dim);
    g.setFont (ui (8.5f));
    g.drawText (text.toUpperCase(), r, juce::Justification::centred, false);
}

// Rotary: tick ring, value arc, pointer. `value` is 0..1.
inline void knob (juce::Graphics& g, juce::Rectangle<int> area, float value,
                  juce::Colour accent, bool hover = false)
{
    const auto centre = area.toFloat().getCentre();
    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.40f;
    constexpr float start = -juce::MathConstants<float>::pi * 1.25f;
    constexpr float sweep = juce::MathConstants<float>::pi * 1.5f;

    g.setColour (theme::line);
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);

    // ticks
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + sweep * (float) i / 10.0f;
        const auto p1 = centre.getPointOnCircumference (radius + 3.0f, a);
        const auto p2 = centre.getPointOnCircumference (radius + (i % 5 == 0 ? 6.5f : 5.0f), a);
        g.setColour (theme::faint);
        g.drawLine ({ p1, p2 }, 1.0f);
    }

    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius + 4.0f, radius + 4.0f, 0.0f, start,
                       start + sweep * value, true);
    g.setColour (hover ? accent.brighter (0.2f) : accent);
    g.strokePath (arc, juce::PathStrokeType (1.6f));

    const float a = start + sweep * value;
    const auto tip = centre.getPointOnCircumference (radius - 2.0f, a);
    g.setColour (theme::silk);
    g.drawLine ({ centre, tip }, 1.6f);
}

inline void meter (juce::Graphics& g, juce::Rectangle<int> area, float level)
{
    const auto f = area.toFloat();
    g.setColour (theme::faint);
    g.drawRect (f, 1.0f);
    const float h = f.getHeight() * juce::jlimit (0.0f, 1.0f, level);
    juce::ColourGradient grad (green, f.getX(), f.getBottom(),
                               red, f.getX(), f.getY(), false);
    grad.addColour (0.55, green);
    grad.addColour (0.82, ochre);
    g.setGradientFill (grad);
    g.fillRect (juce::Rectangle<float> (f.getX() + 1.0f, f.getBottom() - h, f.getWidth() - 2.0f, h));
}

} // namespace theme
