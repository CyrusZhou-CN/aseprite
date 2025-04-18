// Aseprite Document Library
// Copyright (c) 2023-2024  Igara Studio S.A.
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <gtest/gtest.h>

#include "doc/render_plan.h"

#include "doc/cel.h"
#include "doc/document.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/sprite.h"

#include <memory>

using namespace doc;

#define HELPER_LOG(a, b)       a->layer()->name() << " instead of " << b->layer()->name()

#define HELPER_LOG_LAYER(a, b) a->name() << " instead of " << b->name()

#define EXPECT_PLAN(a, b, c, d)                                                                    \
  {                                                                                                \
    RenderPlan plan;                                                                               \
    plan.addLayer(spr->root(), 0);                                                                 \
    const auto items = plan.items();                                                               \
    EXPECT_EQ(a, items[0].cel) << HELPER_LOG(items[0].cel, a);                                     \
    EXPECT_EQ(b, items[1].cel) << HELPER_LOG(items[1].cel, b);                                     \
    EXPECT_EQ(c, items[2].cel) << HELPER_LOG(items[2].cel, c);                                     \
    EXPECT_EQ(d, items[3].cel) << HELPER_LOG(items[3].cel, d);                                     \
  }

TEST(RenderPlan, ZIndex)
{
  auto doc = std::make_shared<Document>();
  ImageSpec spec(ColorMode::INDEXED, 2, 2);
  Sprite* spr;
  doc->sprites().add(spr = Sprite::MakeStdSprite(spec));

  LayerImage *lay0 = static_cast<LayerImage*>(spr->root()->firstLayer()),
             *lay1 = new LayerImage(spr), *lay2 = new LayerImage(spr), *lay3 = new LayerImage(spr);

  lay0->setName("a");
  lay1->setName("b");
  lay2->setName("c");
  lay3->setName("d");

  Cel *a = lay0->cel(0), *b, *c, *d;
  lay1->addCel(b = new Cel(0, ImageRef(Image::create(spec))));
  lay2->addCel(c = new Cel(0, ImageRef(Image::create(spec))));
  lay3->addCel(d = new Cel(0, ImageRef(Image::create(spec))));

  spr->root()->insertLayer(lay1, lay0);
  spr->root()->insertLayer(lay2, lay1);
  spr->root()->insertLayer(lay3, lay2);

  a->setZIndex(0);
  EXPECT_PLAN(a, b, c, d);
  a->setZIndex(1);
  EXPECT_PLAN(b, a, c, d);
  a->setZIndex(2);
  EXPECT_PLAN(b, c, a, d);
  a->setZIndex(3);
  EXPECT_PLAN(b, c, d, a);
  a->setZIndex(4);
  EXPECT_PLAN(b, c, d, a);
  a->setZIndex(1000);
  EXPECT_PLAN(b, c, d, a);
  a->setZIndex(0);
  EXPECT_PLAN(a, b, c, d); // Back no normal

  b->setZIndex(-1);
  EXPECT_PLAN(b, a, c, d);
  b->setZIndex(-2);
  EXPECT_PLAN(b, a, c, d);
  b->setZIndex(-3);
  EXPECT_PLAN(b, a, c, d);
  b->setZIndex(-1000);
  EXPECT_PLAN(b, a, c, d);
  b->setZIndex(0);
  EXPECT_PLAN(a, b, c, d); // Back no normal

  a->setZIndex(-1);
  b->setZIndex(-1);
  c->setZIndex(-1);
  d->setZIndex(-1);
  EXPECT_PLAN(a, b, c, d);

  a->setZIndex(2);
  b->setZIndex(-1);
  c->setZIndex(0);
  d->setZIndex(-1);
  EXPECT_PLAN(b, d, c, a);
}

TEST(RenderPlan, ZIndexBugWithEmptyCels)
{
#undef EXPECT_PLAN
#define EXPECT_PLAN(a, b, c)                                                                       \
  {                                                                                                \
    RenderPlan plan;                                                                               \
    plan.addLayer(spr->root(), 0);                                                                 \
    const auto& items = plan.items();                                                              \
    EXPECT_EQ(a, items[0].cel) << HELPER_LOG(items[0].cel, a);                                     \
    EXPECT_EQ(b, items[1].cel) << HELPER_LOG(items[1].cel, b);                                     \
    EXPECT_EQ(c, items[2].cel) << HELPER_LOG(items[2].cel, c);                                     \
  }

  auto doc = std::make_shared<Document>();
  ImageSpec spec(ColorMode::INDEXED, 2, 2);
  Sprite* spr;
  doc->sprites().add(spr = Sprite::MakeStdSprite(spec));

  LayerImage *lay0 = static_cast<LayerImage*>(spr->root()->firstLayer()),
             *lay1 = new LayerImage(spr), *lay2 = new LayerImage(spr), *lay3 = new LayerImage(spr);

  lay0->setName("a");
  lay1->setName("b");
  lay2->setName("c");
  lay3->setName("d");

  Cel *a = lay0->cel(0), *b, *d;
  lay1->addCel(b = new Cel(0, ImageRef(Image::create(spec))));
  // lay2 has an empty cel
  lay3->addCel(d = new Cel(0, ImageRef(Image::create(spec))));

  spr->root()->insertLayer(lay1, lay0);
  spr->root()->insertLayer(lay2, lay1);
  spr->root()->insertLayer(lay3, lay2);

  d->setZIndex(-1);
  EXPECT_PLAN(a, b, d); // -1 is not enough to pass through lay2
  d->setZIndex(-2);
  EXPECT_PLAN(a, d, b);
  d->setZIndex(-3);
  EXPECT_PLAN(d, a, b);
}

TEST(RenderPlan, DontAddChildrenOnComposeGroupFlag)
{
#undef EXPECT_PLAN
#define EXPECT_PLAN(a, b, c, d)                                                                    \
  {                                                                                                \
    RenderPlan plan(true), subplan(true);                                                          \
    plan.addLayer(spr->root(), 0);                                                                 \
    const auto& items = plan.items();                                                              \
    EXPECT_EQ(spr->root(), items[0].layer) << HELPER_LOG_LAYER(items[0].layer, spr->root());       \
    EXPECT_EQ(items.size(), 1);                                                                    \
    for (const Layer* child : spr->root()->layers())                                               \
      if (child->isVisible())                                                                      \
        subplan.addLayer(child, 0);                                                                \
    const auto& subItems = subplan.items();                                                        \
    EXPECT_EQ(subItems.size(), 4);                                                                 \
    EXPECT_EQ(a, subItems[0].layer) << HELPER_LOG_LAYER(subItems[0].layer, a);                     \
    EXPECT_EQ(b, subItems[1].layer) << HELPER_LOG_LAYER(subItems[1].layer, b);                     \
    EXPECT_EQ(c, subItems[2].layer) << HELPER_LOG_LAYER(subItems[2].layer, c);                     \
    EXPECT_EQ(d, subItems[3].layer) << HELPER_LOG_LAYER(subItems[2].layer, d);                     \
  }

  auto doc = std::make_shared<Document>();
  ImageSpec spec(ColorMode::INDEXED, 2, 2);
  Sprite* spr;
  doc->sprites().add(spr = Sprite::MakeStdSprite(spec));

  LayerImage *lay0 = static_cast<LayerImage*>(spr->root()->firstLayer()),
             *lay1 = new LayerImage(spr), *lay2 = new LayerImage(spr);

  LayerGroup *group0 = new LayerGroup(spr), *group1 = new LayerGroup(spr);

  lay0->setName("a");
  lay1->setName("b");
  lay2->setName("c");
  group0->setName("g0");
  group1->setName("g1");

  group0->addLayer(lay1);

  Cel *a, *b;
  lay1->addCel(a = new Cel(0, ImageRef(Image::create(spec))));
  lay2->addCel(b = new Cel(0, ImageRef(Image::create(spec))));

  spr->root()->insertLayer(group0, lay0);
  spr->root()->insertLayer(group1, group0);
  spr->root()->insertLayer(lay2, group1);

  EXPECT_PLAN(lay0, group0, group1, lay2);
}

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
