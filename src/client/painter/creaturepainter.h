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

#ifndef CREATUREPAINTER_H
#define CREATUREPAINTER_H

#include <client/thing/creature/creature.h>

class CreaturePainter
{
public:
    static void draw(const CreaturePtr& creature, const Point& dest, float scaleFactor, bool animate, const Highlight& highLight, int frameFlags, LightView* lightView);
    static void internalDrawOutfit(const CreaturePtr& creature, Point dest, float scaleFactor, bool animateWalk, bool useBlank, Otc::Direction direction);
    static void drawOutfit(const CreaturePtr& creature, const Rect& destRect, bool resize);
    static void drawInformation(const CreaturePtr& creature, const Rect& parentRect, const Point& dest, float scaleFactor,
                                const Point& drawOffset, float horizontalStretchFactor, float verticalStretchFactor, int drawFlags);
};

#endif