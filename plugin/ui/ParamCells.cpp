/*
    BLOCKWAVE — square-wave-only synthesizer
    Copyright (C) 2026 Kirill Boyko

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "ParamCells.h"
#include <cmath>

namespace blockwave::ui
{

// ---- value formatting -------------------------------------------------------

static juce::String signedInt (float v)
{
    const int n = static_cast<int> (std::lround (v));
    return (n > 0 ? "+" : "") + juce::String (n);
}

static juce::String timeText (float seconds)
{
    if (seconds >= 0.9995f)
        return juce::String (seconds, 2) + "S";
    return juce::String (static_cast<int> (std::lround (seconds * 1000.0f))) + "MS";
}

// Nearest musical division for a synced LFO rate stored in whole notes.
static juce::String divisionText (float wholeNotes)
{
    struct Div { float wn; const char* txt; };
    static constexpr Div divs[] =
    {
        { 8.0f,     "8/1"   }, { 4.0f,    "4/1"   }, { 2.0f,     "2/1"   },
        { 1.0f,     "1/1"   }, { 0.75f,   "1/2D"  }, { 0.5f,     "1/2"   },
        { 0.375f,   "1/4D"  }, { 0.25f,   "1/4"   }, { 0.1875f,  "1/8D"  },
        { 0.125f,   "1/8"   }, { 0.09375f, "1/16D" }, { 0.0625f, "1/16"  },
        { 0.03125f, "1/32"  },
    };
    const Div* best = &divs[0];
    float bestErr = 1.0e9f;
    for (const auto& d : divs)
    {
        const float err = std::abs (std::log (juce::jmax (wholeNotes, 1.0e-4f) / d.wn));
        if (err < bestErr) { bestErr = err; best = &d; }
    }
    return best->txt;
}

static juce::String hzText (float v)
{
    return (v < 10.0f ? juce::String (v, 2) : juce::String (v, 1)) + "HZ";
}

juce::String formatParamValue (PId id, float v,
                               const juce::AudioProcessorValueTreeState& state)
{
    const auto& d = paramDef (id);

    switch (id)
    {
        case PId::oscA_oct:  case PId::oscB_oct:
        case PId::oscA_semi: case PId::oscB_semi:
            return signedInt (v);

        case PId::oscA_fine: case PId::oscB_fine:
            return signedInt (v) + "C";

        case PId::oscA_pw: case PId::oscB_pw:
            return juce::String (static_cast<int> (std::lround (v))) + "%";

        case PId::uni_detune:
            return juce::String (static_cast<int> (std::lround (v))) + "C";

        case PId::glide_time:
        case PId::env1_a: case PId::env1_d: case PId::env1_r:
        case PId::env2_a: case PId::env2_d: case PId::env2_r:
            return timeText (v);

        case PId::filt_cutoff:
            return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + "K"
                                : juce::String (static_cast<int> (std::lround (v))) + "HZ";

        case PId::env2_pitch:
            return signedInt (v) + "ST";

        case PId::lfo1_rate:
        {
            const auto* sync = state.getRawParameterValue ("lfo1_sync");
            return (sync != nullptr && sync->load() >= 0.5f) ? divisionText (v) : hzText (v);
        }
        case PId::lfo2_rate:
        {
            const auto* sync = state.getRawParameterValue ("lfo2_sync");
            return (sync != nullptr && sync->load() >= 0.5f) ? divisionText (v) : hzText (v);
        }

        case PId::crush_bits:
            return juce::String (static_cast<int> (std::lround (v))) + "BIT";

        case PId::crush_down:
            return juce::String (static_cast<int> (std::lround (v))) + "X";

        case PId::master_gain:
            return (v > 0.0f ? "+" : "") + juce::String (v, 1) + "DB";

        default:
            break;
    }

    switch (d.kind)
    {
        case PKind::boolean:  return v >= 0.5f ? "ON" : "OFF";
        case PKind::integer:  return juce::String (static_cast<int> (std::lround (v)));
        case PKind::choice:
        {
            const int idx = juce::jlimit (0, d.numChoices - 1,
                                          static_cast<int> (std::lround (v)));
            return d.choices[idx];
        }
        case PKind::floating:
        default:              return juce::String (v, 2);
    }
}

// ---- PixelSlider ------------------------------------------------------------

bool PixelSlider::keyPressed (const juce::KeyPress& k)
{
    const bool up   = k.isKeyCode (juce::KeyPress::upKey)
                   || k.isKeyCode (juce::KeyPress::rightKey);
    const bool down = k.isKeyCode (juce::KeyPress::downKey)
                   || k.isKeyCode (juce::KeyPress::leftKey);
    if (! up && ! down)
        return false;

    const double range = getMaximum() - getMinimum();
    double step = getInterval() > 0.0 ? getInterval() : range / 32.0;
    if (k.getModifiers().isShiftDown() && getInterval() <= 0.0)
        step = range / 256.0;

    setValue (getValue() + (up ? step : -step), juce::sendNotificationSync);
    return true;
}

// ---- PixelChoice ------------------------------------------------------------

PixelChoice::PixelChoice (juce::RangedAudioParameter& param, const ParamDef& d)
    : def (d),
      attachment (param,
                  [this] (float v)
                  {
                      index = juce::jlimit (0, def.numChoices - 1,
                                            static_cast<int> (std::lround (v)));
                      repaint();
                  },
                  nullptr)
{
    setWantsKeyboardFocus (true);
    attachment.sendInitialUpdate();
}

void PixelChoice::cycle (int delta)
{
    const int n = juce::jmax (1, def.numChoices);
    const int next = (index + delta % n + n) % n;
    attachment.setValueAsCompleteGesture (static_cast<float> (next));
}

void PixelChoice::paint (juce::Graphics& g)
{
    using namespace colours;
    const auto r = getLocalBounds();
    drawBevelBox (g, r, chip, panelLight, panelDark, outline, true);
    drawPixelTextCentred (g, def.choices[index], r, 1, ice);
    if (hasKeyboardFocus (true))
        drawFocusTicks (g, r);
}

void PixelChoice::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    cycle (e.mods.isPopupMenu() ? -1 : 1);
}

void PixelChoice::mouseWheelMove (const juce::MouseEvent&,
                                  const juce::MouseWheelDetails& w)
{
    if (w.deltaY != 0.0f)
        cycle (w.deltaY > 0.0f ? 1 : -1);
}

bool PixelChoice::keyPressed (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::upKey)
     || k.isKeyCode (juce::KeyPress::rightKey)
     || k.isKeyCode (juce::KeyPress::spaceKey))
    {
        cycle (1);
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::downKey)
     || k.isKeyCode (juce::KeyPress::leftKey))
    {
        cycle (-1);
        return true;
    }
    return false;
}

// ---- ParamCell --------------------------------------------------------------

ParamCell::ParamCell (juce::AudioProcessorValueTreeState& s, PId id,
                      const juce::String& label, const juce::String& tooltip)
    : state (s), pid (id), labelText (label)
{
    const auto& d = paramDef (pid);
    auto* param = state.getParameter (d.id);
    jassert (param != nullptr);

    switch (d.kind)
    {
        case PKind::boolean:
        {
            toggle = std::make_unique<juce::ToggleButton> (juce::String());
            toggle->setTooltip (tooltip);
            toggle->onStateChange = [this] { refreshValue(); };
            addAndMakeVisible (*toggle);
            toggleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                state, d.id, *toggle);
            break;
        }
        case PKind::choice:
        {
            choice = std::make_unique<PixelChoice> (*param, d);
            choice->setTooltip (tooltip);
            addAndMakeVisible (*choice);
            hasValueChip = false;
            break;
        }
        case PKind::integer:
        case PKind::floating:
        default:
        {
            slider = std::make_unique<PixelSlider> (
                juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox);
            slider->setTooltip (tooltip);
            slider->setWantsKeyboardFocus (true);
            slider->setDoubleClickReturnValue (true, d.defaultValue);
            slider->onValueChange = [this] { refreshValue(); };
            addAndMakeVisible (*slider);
            sliderAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                state, d.id, *slider);
            break;
        }
    }

    setSize (cellW, cellH);
    refreshValue();
}

void ParamCell::refreshValue()
{
    const auto* raw = state.getRawParameterValue (paramDef (pid).id);
    if (raw == nullptr)
        return;
    const auto next = formatParamValue (pid, raw->load(), state);
    if (next != valueText)
    {
        valueText = next;
        repaint (0, 40, getWidth(), getHeight() - 40);   // dirty chip only
    }
}

void ParamCell::paint (juce::Graphics& g)
{
    using namespace colours;
    drawPixelTextCentred (g, labelText, { 0, 0, getWidth(), 8 }, 1, label);
    if (hasValueChip)
    {
        const juce::Rectangle<int> chipR (2, 42, getWidth() - 4, 12);
        drawBevelBox (g, chipR, chip, panelLight, panelDark, outline, true, 1);
        drawPixelTextCentred (g, valueText, chipR, 1, ice);
    }
}

void ParamCell::resized()
{
    if (slider != nullptr)
        slider->setBounds (0, 8, getWidth(), 32);
    if (toggle != nullptr)
        toggle->setBounds ((getWidth() - 24) / 2, 16, 24, 16);
    if (choice != nullptr)
        choice->setBounds (2, 16, getWidth() - 4, 16);
}

} // namespace blockwave::ui
