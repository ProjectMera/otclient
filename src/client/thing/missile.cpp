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

#include <client/map/map.h>
#include <client/map/tile.h>
#include <client/thing/missile.h>
#include <client/manager/thingtypemanager.h>

#include <framework/core/clock.h>
#include <framework/core/eventdispatcher.h>

void Missile::setPath(const Position& fromPosition, const Position& toPosition)
{
    m_position = fromPosition;
    m_delta = Point(toPosition.x - fromPosition.x, toPosition.y - fromPosition.y);

    const float deltaLength = m_delta.length();
    if(deltaLength == 0) {
        g_map.removeThing(this);
        return;
    }

    m_direction = fromPosition.getDirectionFromPosition(toPosition);

    m_duration = (Otc::MISSILE_TICKS_PER_FRAME * 2) * std::sqrt(deltaLength);
    m_delta *= Otc::TILE_PIXELS;
    m_animationTimer.restart();
    m_distance = fromPosition.distance(toPosition);

    // schedule removal
    const auto self = asMissile();
    g_dispatcher.scheduleEvent([self]() { g_map.removeThing(self); }, m_duration);
}

void Missile::setId(uint32 id)
{
    if(!g_things.isValidDatId(id, ThingCategoryMissile))
        id = 0;
    m_id = id;
}

const ThingTypePtr& Missile::getThingType()
{
    return g_things.getThingType(m_id, ThingCategoryMissile);
}

ThingType* Missile::rawGetThingType()
{
    return g_things.rawGetThingType(m_id, ThingCategoryMissile);
}