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

#include <client/thing/type/thingtype.h>
#include <client/game.h>
#include <client/map/lightview.h>
#include <client/map/map.h>
#include <client/manager/spritemanager.h>

#include <framework/core/eventdispatcher.h>
#include <framework/core/filestream.h>
#include <framework/graphics/graphics.h>
#include <framework/graphics/image.h>
#include <framework/graphics/texture.h>
#include <framework/graphics/texturemanager.h>
#include <framework/otml/otml.h>

void ThingType::serialize(const FileStreamPtr& fin)
{
    for(int i = 0; i < ThingLastAttr; ++i) {
        if(!hasAttr(static_cast<ThingAttr>(i)))
            continue;

        int attr = i;
        if(attr == ThingAttrNoMoveAnimation)
            attr = 16;
        else if(attr >= ThingAttrPickupable)
            attr += 1;

        fin->addU8(attr);
        switch(attr) {
        case ThingAttrDisplacement:
        {
            fin->addU16(m_displacement.x);
            fin->addU16(m_displacement.y);
            break;
        }
        case ThingAttrLight:
        {
            const Light light = m_attribs.get<Light>(attr);
            fin->addU16(light.intensity);
            fin->addU16(light.color);
            break;
        }
        case ThingAttrMarket:
        {
            auto market = m_attribs.get<MarketData>(attr);
            fin->addU16(market.category);
            fin->addU16(market.tradeAs);
            fin->addU16(market.showAs);
            fin->addString(market.name);
            fin->addU16(market.restrictVocation);
            fin->addU16(market.requiredLevel);
            break;
        }
        case ThingAttrUsable:
        case ThingAttrElevation:
        case ThingAttrGround:
        case ThingAttrWritable:
        case ThingAttrWritableOnce:
        case ThingAttrMinimapColor:
        case ThingAttrCloth:
        case ThingAttrLensHelp:
            fin->addU16(m_attribs.get<uint16>(attr));
            break;
        default:
            break;
        }
    }
    fin->addU8(ThingLastAttr);

    fin->addU8(m_size.width());
    fin->addU8(m_size.height());

    if(m_size.width() > 1 || m_size.height() > 1)
        fin->addU8(m_realSize);

    fin->addU8(m_layers);
    fin->addU8(m_numPatternX);
    fin->addU8(m_numPatternY);
    fin->addU8(m_numPatternZ);
    fin->addU8(m_animationPhases);

    if(m_animationPhases > 1 && m_animator != nullptr) {
        m_animator->serialize(fin);
    }

    for(int i : m_spritesIndex) {
        fin->addU32(i);
    }
}

void ThingType::unserialize(uint16 clientId, ThingCategory category, const FileStreamPtr& fin)
{
    m_null = false;
    m_id = clientId;
    m_category = category;

    int count = 0, attr = -1;
    bool done = false;
    for(int i = 0; i < ThingLastAttr; ++i) {
        ++count;
        attr = fin->getU8();
        if(attr == ThingLastAttr) {
            done = true;
            break;
        }

        if(attr == 16)
            attr = ThingAttrNoMoveAnimation;
        else if(attr == 254) { // Usable
            m_attribs.set(ThingAttrUsable, true);
            continue;
        } else if(attr == 35) { // Default Action
            m_attribs.set(ThingAttrDefaultAction, fin->getU16());
            continue;
        } else if(attr > 16)
            attr -= 1;

        switch(attr) {
        case ThingAttrDisplacement:
        {
            m_displacement.x = fin->getU16();
            m_displacement.y = fin->getU16();
            m_attribs.set(attr, true);
            break;
        }
        case ThingAttrLight:
        {
            Light light;
            light.intensity = fin->getU16();
            light.color = fin->getU16();
            m_attribs.set(attr, light);
            break;
        }
        case ThingAttrMarket:
        {
            MarketData market;
            market.category = fin->getU16();
            market.tradeAs = fin->getU16();
            market.showAs = fin->getU16();
            market.name = fin->getString();
            market.restrictVocation = fin->getU16();
            market.requiredLevel = fin->getU16();
            m_attribs.set(attr, market);
            break;
        }
        case ThingAttrElevation:
        {
            m_elevation = fin->getU16();
            m_attribs.set(attr, m_elevation);
            break;
        }
        case ThingAttrUsable:
        case ThingAttrGround:
        case ThingAttrWritable:
        case ThingAttrWritableOnce:
        case ThingAttrMinimapColor:
        case ThingAttrCloth:
        case ThingAttrLensHelp:
            m_attribs.set(attr, fin->getU16());
            break;
        default:
            m_attribs.set(attr, true);
            break;
        }
    }

    if(!done)
        stdext::throw_exception(stdext::format("corrupt data (id: %d, category: %d, count: %d, lastAttr: %d)",
                                               m_id, m_category, count, attr));

    const bool hasFrameGroups = category == ThingCategoryCreature;
    const uint8 groupCount = hasFrameGroups ? fin->getU8() : 1;

    m_animationPhases = 0;
    int totalSpritesCount = 0;

    std::vector<Size> sizes;
    std::vector<int> total_sprites;

    for(int i = 0; i < groupCount; ++i) {
        uint8 frameGroupType = FrameGroupDefault;
        if(hasFrameGroups)
            frameGroupType = fin->getU8();

        const uint8 width = fin->getU8();
        const uint8 height = fin->getU8();
        m_size = Size(width, height);
        sizes.push_back(m_size);
        if(width > 1 || height > 1) {
            m_realSize = fin->getU8();
            m_exactSize = std::min<int>(m_realSize, std::max<int>(width * SPRITE_SIZE, height * SPRITE_SIZE));
        } else
            m_exactSize = SPRITE_SIZE;

        m_layers = fin->getU8();
        m_numPatternX = fin->getU8();
        m_numPatternY = fin->getU8();
        m_numPatternZ = fin->getU8();

        const int groupAnimationsPhases = fin->getU8();
        m_animationPhases += groupAnimationsPhases;

        if(groupAnimationsPhases > 1) {
            auto animator = AnimatorPtr(new Animator);
            animator->unserialize(groupAnimationsPhases, fin);

            if(groupCount == 1 || frameGroupType == FrameGroupMoving)
                m_animator = animator;
            else m_idleAnimator = animator;
        }

        const int totalSprites = m_size.area() * m_layers * m_numPatternX * m_numPatternY * m_numPatternZ * groupAnimationsPhases;
        total_sprites.push_back(totalSprites);

        if(totalSpritesCount + totalSprites > 4096)
            stdext::throw_exception("a thing type has more than 4096 sprites");

        m_spritesIndex.resize(totalSpritesCount + totalSprites);
        for(int j = totalSpritesCount; j < (totalSpritesCount + totalSprites); ++j)
            m_spritesIndex[j] = fin->getU32();

        totalSpritesCount += totalSprites;
    }

    if(sizes.size() > 1) {
        // correction for some sprites
        for(auto& s : sizes) {
            m_size.setWidth(std::max<int>(m_size.width(), s.width()));
            m_size.setHeight(std::max<int>(m_size.height(), s.height()));
        }
        size_t expectedSize = m_size.area() * m_layers * m_numPatternX * m_numPatternY * m_numPatternZ * m_animationPhases;
        if(expectedSize != m_spritesIndex.size()) {
            std::vector<int> sprites(std::move(m_spritesIndex));
            m_spritesIndex.clear();
            m_spritesIndex.reserve(expectedSize);
            for(size_t i = 0, idx = 0; i < sizes.size(); ++i) {
                int totalSprites = total_sprites[i];
                if(m_size == sizes[i]) {
                    for(int j = 0; j < totalSprites; ++j) {
                        m_spritesIndex.push_back(sprites[idx++]);
                    }
                    continue;
                }
                size_t patterns = (totalSprites / sizes[i].area());
                for(size_t p = 0; p < patterns; ++p) {
                    for(int x = 0; x < m_size.width(); ++x) {
                        for(int y = 0; y < m_size.height(); ++y) {
                            if(x < sizes[i].width() && y < sizes[i].height()) {
                                m_spritesIndex.push_back(sprites[idx++]);
                                continue;
                            }
                            m_spritesIndex.push_back(0);
                        }
                    }
                }
            }
        }
    }

    m_textures.resize(m_animationPhases);
    m_blankTextures.resize(m_animationPhases);
    m_texturesFramesRects.resize(m_animationPhases);
    m_texturesFramesOriginRects.resize(m_animationPhases);
    m_texturesFramesOffsets.resize(m_animationPhases);
}

void ThingType::exportImage(const std::string& fileName)
{
    if(m_null)
        stdext::throw_exception("cannot export null thingtype");

    if(m_spritesIndex.empty())
        stdext::throw_exception("cannot export thingtype without sprites");

    ImagePtr image(new Image(Size(SPRITE_SIZE * m_size.width() * m_layers * m_numPatternX, SPRITE_SIZE * m_size.height() * m_animationPhases * m_numPatternY * m_numPatternZ)));
    for(int z = 0; z < m_numPatternZ; ++z) {
        for(int y = 0; y < m_numPatternY; ++y) {
            for(int x = 0; x < m_numPatternX; ++x) {
                for(int l = 0; l < m_layers; ++l) {
                    for(int a = 0; a < m_animationPhases; ++a) {
                        for(int w = 0; w < m_size.width(); ++w) {
                            for(int h = 0; h < m_size.height(); ++h) {
                                image->blit(Point(SPRITE_SIZE * (m_size.width() - w - 1 + m_size.width() * x + m_size.width() * m_numPatternX * l),
                                                  SPRITE_SIZE * (m_size.height() - h - 1 + m_size.height() * y + m_size.height() * m_numPatternY * a + m_size.height() * m_numPatternY * m_animationPhases * z)),
                                            g_sprites.getSpriteImage(m_spritesIndex[getSpriteIndex(w, h, l, x, y, z, a)]));
                            }
                        }
                    }
                }
            }
        }
    }

    image->savePNG(fileName);
}

void ThingType::unserializeOtml(const OTMLNodePtr& node)
{
    for(const OTMLNodePtr& node2 : node->children()) {
        if(node2->tag() == "opacity")
            m_opacity = node2->value<float>();
        else if(node2->tag() == "notprewalkable")
            m_attribs.set(ThingAttrNotPreWalkable, node2->value<bool>());
        else if(node2->tag() == "image")
            m_customImage = node2->value();
        else if(node2->tag() == "full-ground") {
            if(node2->value<bool>())
                m_attribs.set(ThingAttrFullGround, true);
            else
                m_attribs.remove(ThingAttrFullGround);
        }
    }
}

const TexturePtr& ThingType::getTexture(int animationPhase, bool allBlank)
{
    TexturePtr& animationPhaseTexture = (allBlank ? m_blankTextures : m_textures)[animationPhase];
    if(animationPhaseTexture) return animationPhaseTexture;

    bool useCustomImage = false;
    if(animationPhase == 0 && !m_customImage.empty())
        useCustomImage = true;

    // we don't need layers in common items, they will be pre-drawn
    int textureLayers = 1;
    int numLayers = m_layers;
    if(m_category == ThingCategoryCreature && numLayers >= 2) {
        // 5 layers: outfit base, red mask, green mask, blue mask, yellow mask
        textureLayers = 5;
        numLayers = 5;
    }

    const int indexSize = textureLayers * m_numPatternX * m_numPatternY * m_numPatternZ;
    const Size textureSize = getBestTextureDimension(m_size.width(), m_size.height(), indexSize);
    const ImagePtr fullImage = useCustomImage ? Image::load(m_customImage) : ImagePtr(new Image(textureSize * SPRITE_SIZE));

    m_texturesFramesRects[animationPhase].resize(indexSize);
    m_texturesFramesOriginRects[animationPhase].resize(indexSize);
    m_texturesFramesOffsets[animationPhase].resize(indexSize);
    for(int z = 0; z < m_numPatternZ; ++z) {
        for(int y = 0; y < m_numPatternY; ++y) {
            for(int x = 0; x < m_numPatternX; ++x) {
                for(int l = 0; l < numLayers; ++l) {
                    const bool spriteMask = m_category == ThingCategoryCreature && l > 0;
                    const int frameIndex = getTextureIndex(l % textureLayers, x, y, z);

                    Point framePos = Point(frameIndex % (textureSize.width() / m_size.width()) * m_size.width(),
                                           frameIndex / (textureSize.width() / m_size.width()) * m_size.height()) * SPRITE_SIZE;

                    if(!useCustomImage) {
                        for(int h = 0; h < m_size.height(); ++h) {
                            for(int w = 0; w < m_size.width(); ++w) {
                                const uint spriteIndex = getSpriteIndex(w, h, spriteMask ? 1 : l, x, y, z, animationPhase);
                                ImagePtr spriteImage = g_sprites.getSpriteImage(m_spritesIndex[spriteIndex]);
                                if(!spriteImage) fullImage->setTransparentPixel(true);
                                else {
                                    if(spriteIndex == 0) {
                                        if(spriteImage->hasTransparentPixel() || hasDisplacement()) {
                                            fullImage->setTransparentPixel(true);
                                        }
                                    }

                                    if(allBlank) {
                                        spriteImage->overwrite(Color::white);
                                    } else if(spriteMask) {
                                        static Color maskColors[] = { Color::red, Color::green, Color::blue, Color::yellow };
                                        spriteImage->overwriteMask(maskColors[l - 1]);
                                    }
                                    Point spritePos = Point(m_size.width() - w - 1,
                                                            m_size.height() - h - 1) * SPRITE_SIZE;

                                    fullImage->blit(framePos + spritePos, spriteImage);
                                }
                            }
                        }
                    }

                    Rect drawRect(framePos + Point(m_size.width(), m_size.height()) * SPRITE_SIZE - Point(1), framePos);
                    for(int fx = framePos.x; fx < framePos.x + m_size.width() * SPRITE_SIZE; ++fx) {
                        for(int fy = framePos.y; fy < framePos.y + m_size.height() * SPRITE_SIZE; ++fy) {
                            uint8* p = fullImage->getPixel(fx, fy);
                            if(p[3] != 0x00) {
                                drawRect.setTop(std::min<int>(fy, drawRect.top()));
                                drawRect.setLeft(std::min<int>(fx, drawRect.left()));
                                drawRect.setBottom(std::max<int>(fy, drawRect.bottom()));
                                drawRect.setRight(std::max<int>(fx, drawRect.right()));
                            }
                        }
                    }

                    m_texturesFramesRects[animationPhase][frameIndex] = drawRect;
                    m_texturesFramesOriginRects[animationPhase][frameIndex] = Rect(framePos, Size(m_size.width(), m_size.height()) * SPRITE_SIZE);
                    m_texturesFramesOffsets[animationPhase][frameIndex] = drawRect.topLeft() - framePos;
                }
            }
        }
    }

    animationPhaseTexture = TexturePtr(new Texture(fullImage, true));
    return animationPhaseTexture;
}

Size ThingType::getBestTextureDimension(int w, int h, int count)
{
    const int MAX = SPRITE_SIZE;

    int k = 1;
    while(k < w)
        k <<= 1;
    w = k;

    k = 1;
    while(k < h)
        k <<= 1;
    h = k;

    const int numSprites = w * h * count;
    assert(numSprites <= MAX * MAX);
    assert(w <= MAX);
    assert(h <= MAX);

    auto bestDimension = Size(MAX, MAX);
    for(int i = w; i <= MAX; i <<= 1) {
        for(int j = h; j <= MAX; j <<= 1) {
            auto candidateDimension = Size(i, j);
            if(candidateDimension.area() < numSprites)
                continue;
            if((candidateDimension.area() < bestDimension.area()) ||
               (candidateDimension.area() == bestDimension.area() && candidateDimension.width() + candidateDimension.height() < bestDimension.width() + bestDimension.height()))
                bestDimension = candidateDimension;
        }
    }

    return bestDimension;
}

uint ThingType::getSpriteIndex(int w, int h, int l, int x, int y, int z, int a)
{
    const uint index =
        ((((((a % m_animationPhases)
             * m_numPatternZ + z)
            * m_numPatternY + y)
           * m_numPatternX + x)
          * m_layers + l)
         * m_size.height() + h)
        * m_size.width() + w;
    assert(index < m_spritesIndex.size());
    return index;
}

uint ThingType::getTextureIndex(int l, int x, int y, int z)
{
    return ((l * m_numPatternZ + z)
            * m_numPatternY + y)
        * m_numPatternX + x;
}

int ThingType::getExactSize(int layer, int xPattern, int yPattern, int zPattern, int animationPhase)
{
    if(m_null)
        return 0;

    getTexture(animationPhase); // we must calculate it anyway.
    const int frameIndex = getTextureIndex(layer, xPattern, yPattern, zPattern);
    const Size size = m_texturesFramesOriginRects[animationPhase][frameIndex].size() - m_texturesFramesOffsets[animationPhase][frameIndex].toSize();
    return std::max<int>(size.width(), size.height());
}

void ThingType::setPathable(bool var)
{
    if(var == true)
        m_attribs.remove(ThingAttrNotPathable);
    else
        m_attribs.set(ThingAttrNotPathable, true);
}

int ThingType::getAnimationPhases()
{
    if(m_animator) return m_animator->getAnimationPhases();

    if(m_category == ThingCategoryCreature) return m_animationPhases - 1;

    return m_animationPhases;
}

int ThingType::getExactHeight()
{
    if(m_null)
        return 0;

    if(m_exactHeight != -1)
        return m_exactHeight;

    getTexture(0);
    const int frameIndex = getTextureIndex(0, 0, 0, 0);
    const Size size = m_texturesFramesOriginRects[0][frameIndex].size() - m_texturesFramesOffsets[0][frameIndex].toSize();

    return m_exactHeight = size.height();
}
