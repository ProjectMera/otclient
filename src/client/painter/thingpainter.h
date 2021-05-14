/*
 * Copyright (c) 2010-2020 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the <client/thing/Software"), to deal
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

#ifndef THINGPAINTER_H
#define THINGPAINTER_H

#include <framework/core/declarations.h>
#include <client/declarations.h>

class ThingPainter
{
public:
    static void drawText(const StaticTextPtr& text, const Point& dest, const Rect& parentRect);
    static void drawText(const AnimatedTextPtr& text, const Point& dest, const Rect& parentRect);

    static void draw(const ItemPtr& item, const Point& dest, float scaleFactor, bool animate, const Highlight& highLight, int frameFlag = Otc::FUpdateThing, LightView* lightView = nullptr);
    static void draw(const EffectPtr& effect, const Point& dest, float scaleFactor, int frameFlag, LightView* lightView);
    static void draw(const MissilePtr& missile, const Point& dest, float scaleFactor, int frameFlag, LightView* lightView);
    static void draw(const ThingTypePtr& thingType, const Point& dest, float scaleFactor, int layer, int xPattern, int yPattern, int zPattern, int animationPhase, bool useBlankTexture, int frameFlags = Otc::FUpdateThing, LightView* lightView = nullptr);
};

#endif