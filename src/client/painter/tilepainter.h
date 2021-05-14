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

#ifndef TILEPAINTER_H
#define TILEPAINTER_H

#include <client/map/tile.h>

class TilePainter
{
public:
    static void drawCreature(const TilePtr& tile, const Point& dest, float scaleFactor, int frameFlags, LightView* lightView = nullptr);
    static void drawStart(const TilePtr& tile, const MapViewPtr& mapView);
    static void drawEnd(const TilePtr& tile, const MapViewPtr& mapView);

    static void draw(const TilePtr& tile, const Point& dest, float scaleFactor, int frameFlags, LightView* lightView = nullptr);
    static void drawGround(const TilePtr& tile, const Point& dest, float scaleFactor, int frameFlags, LightView* lightView = nullptr);
    static void drawBottom(const TilePtr& tile, const Point& dest, float scaleFactor, int frameFlags, LightView* lightView = nullptr);
    static void drawTop(const TilePtr& tile, const Point& dest, float scaleFactor, int frameFlags, LightView* lightView = nullptr);
    static void drawThing(const TilePtr& tile, const ThingPtr& thing, const Point& dest, float scaleFactor, bool animate, int frameFlag, LightView* lightView);
};

#endif