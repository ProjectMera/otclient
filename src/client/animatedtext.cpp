/*
 * Copyright (c) 2010-2020 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "animatedtext.h"
#include <framework/core/clock.h>
#include <framework/core/eventdispatcher.h>
#include <framework/graphics/graphics.h>
#include "game.h"
#include "map.h"

AnimatedText::AnimatedText()
{
    m_cachedText.setFont(g_fonts.getFont("verdana-11px-rounded"));
    m_cachedText.setAlign(Fw::AlignLeft);
}

void AnimatedText::onAppear()
{
    m_animationTimer.restart();

    // schedule removal
    auto self = asAnimatedText();
    g_dispatcher.scheduleEvent([self]() { g_map.removeThing(self); }, Otc::ANIMATED_TEXT_DURATION);
}

void AnimatedText::setColor(uint8 color)
{
    m_color = Color::from8bit(color);
}

void AnimatedText::setText(const std::string& text)
{
    m_cachedText.setText(text);
}

bool AnimatedText::merge(const AnimatedTextPtr& other)
{
    if(other->getColor() != m_color)
        return false;

    if(other->getCachedText().getFont() != m_cachedText.getFont())
        return false;

    if(m_animationTimer.ticksElapsed() > Otc::ANIMATED_TEXT_DURATION / 2.5)
        return false;

    try {
        const int number = stdext::safe_cast<int>(m_cachedText.getText());
        const int otherNumber = stdext::safe_cast<int>(other->getCachedText().getText());

        const std::string text = stdext::format("%d", number + otherNumber);
        m_cachedText.setText(text);
        return true;
    } catch(...) {
        return false;
    }
}