// Aseprite UI Library
// Copyright (C) 2019-2025  Igara Studio S.A.
// Copyright (C) 2001-2018  David Capello
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "ui/theme.h"

#include "base/utf8_decode.h"
#include "gfx/point.h"
#include "gfx/size.h"
#include "os/surface.h"
#include "os/system.h"
#include "text/font.h"
#include "text/font_metrics.h"
#include "ui/intern.h"
#include "ui/manager.h"
#include "ui/paint_event.h"
#include "ui/scale.h"
#include "ui/size_hint_event.h"
#include "ui/style.h"
#include "ui/system.h"
#include "ui/view.h"
#include "ui/widget.h"
#include "ui/window.h"

#include <algorithm>
#include <cstring>

namespace ui {

namespace {

// Colors for a simple default theme.
constexpr const gfx::Color kBgColor = gfx::rgba(32, 32, 32, 255);
constexpr const gfx::Color kFgColor = gfx::rgba(255, 255, 200, 255);

int current_ui_scale = 1;       // Global UI Screen Scaling factor
int old_ui_scale = 1;           // Add this field in InitThemeEvent
Theme* current_theme = nullptr; // Global active theme

int compare_layer_flags(int a, int b)
{
  // TODO improve this
  return a - b;
}

void for_each_layer(const int flags,
                    const Style* style,
                    std::function<void(const Style::Layer&)> callback)
{
  ASSERT(style);
  if (!style)
    return;

  const Style::Layer* bestLayer = nullptr;

  for (const auto& layer : style->layers()) {
    if (bestLayer && bestLayer->type() != layer.type()) {
      callback(*bestLayer);
      bestLayer = nullptr;
    }

    if ((!layer.flags() || (layer.flags() & flags) == layer.flags()) &&
        (!bestLayer ||
         (bestLayer && compare_layer_flags(bestLayer->flags(), layer.flags()) <= 0))) {
      bestLayer = &layer;
    }
  }

  if (bestLayer)
    callback(*bestLayer);
}

void for_each_layer(const Widget* widget,
                    const Style* style,
                    std::function<void(const Style::Layer&)> callback)
{
  for_each_layer(PaintWidgetPartInfo::getStyleFlagsForWidget(widget), style, callback);
}

std::function<void(int srcx, int srcy, int dstx, int dsty, int w, int h)>
getDrawSurfaceFunction(Graphics* g, os::Surface* sheet, gfx::Color color)
{
  if (color != gfx::ColorNone)
    return [g, sheet, color](int srcx, int srcy, int dstx, int dsty, int w, int h) {
      g->drawColoredRgbaSurface(sheet, color, srcx, srcy, dstx, dsty, w, h);
    };
  else
    return [g, sheet](int srcx, int srcy, int dstx, int dsty, int w, int h) {
      g->drawRgbaSurface(sheet, srcx, srcy, dstx, dsty, w, h);
    };
}

} // anonymous namespace

PaintWidgetPartInfo::PaintWidgetPartInfo()
{
}

PaintWidgetPartInfo::PaintWidgetPartInfo(const Widget* widget)
{
  bgColor = (!widget->isTransparent() ? widget->bgColor() : gfx::ColorNone);
  styleFlags = PaintWidgetPartInfo::getStyleFlagsForWidget(widget);
  text = &widget->text();
  textBlob = widget->textBlob();
  baseline = widget->textBaseline();
  mnemonic = widget->mnemonic();
  icon = nullptr;
  if (const Style::Layer::IconSurfaceProvider* iconProvider =
        dynamic_cast<const Style::Layer::IconSurfaceProvider*>(widget)) {
    icon = iconProvider->iconSurface();
  }
}

// static
int PaintWidgetPartInfo::getStyleFlagsForWidget(const Widget* widget)
{
  return (widget->isEnabled() ? 0 : Style::Layer::kDisabled) |
         (widget->isSelected() ? Style::Layer::kSelected : 0) |
         (widget->hasMouse() ? Style::Layer::kMouse : 0) |
         (widget->hasFocus() ? Style::Layer::kFocus : 0) |
         (widget->hasCapture() ? Style::Layer::kCapture : 0);
}

Theme::Theme() : m_fontMgr(text::FontMgr::Make())
{
}

Theme::~Theme()
{
  if (current_theme == this)
    set_theme(nullptr, guiscale());
}

text::FontRef Theme::getDefaultFont() const
{
  return m_fontMgr->defaultFont(kDefaultFontHeight);
}

// static
ui::Style Theme::m_emptyStyle(nullptr);
ui::Style Theme::m_simpleStyle(nullptr);

void Theme::regenerateTheme()
{
  set_mouse_cursor(kNoCursor);
  onRegenerateTheme();
}

void Theme::initWidget(Widget* widget)
{
  if (m_simpleStyle.layers().empty()) {
    Style::Layer bg;
    Style::Layer br;
    Style::Layer fg;
    bg.setType(Style::Layer::Type::kBackground);
    bg.setColor(kBgColor);
    br.setType(Style::Layer::Type::kBorder);
    br.setColor(kFgColor);
    fg.setType(Style::Layer::Type::kText);
    fg.setColor(kFgColor);
    m_simpleStyle.layers().push_back(bg);
    m_simpleStyle.layers().push_back(br);
    m_simpleStyle.layers().push_back(fg);

    bg.setFlags(Style::Layer::kSelected);
    bg.setColor(kFgColor);
    br.setFlags(Style::Layer::kSelected);
    br.setColor(kFgColor);
    fg.setFlags(Style::Layer::kSelected);
    fg.setColor(kBgColor);
    m_simpleStyle.layers().push_back(bg);
    m_simpleStyle.layers().push_back(br);
    m_simpleStyle.layers().push_back(fg);
  }

  widget->setFont(getDefaultFont());
  widget->setStyle(&m_simpleStyle);

  switch (widget->type()) {
    case kViewScrollbarWidget:
      static_cast<ScrollBar*>(widget)->setThumbStyle(&m_simpleStyle);
      break;
  }
}

void Theme::setDecorativeWidgetBounds(Widget* widget)
{
  switch (widget->type()) {
    case kWindowTitleLabelWidget: {
      Window* window = widget->window();
      gfx::Rect labelBounds(widget->sizeHint());
      gfx::Rect windowBounds(window->bounds());
      gfx::Border margin;
      if (widget->style())
        margin = widget->style()->margin();

      labelBounds.offset(windowBounds.x + margin.left(), windowBounds.y + margin.top());

      widget->setBounds(labelBounds);
      break;
    }

    case kWindowCloseButtonWidget: {
      Window* window = widget->window();
      gfx::Rect buttonBounds(widget->sizeHint());
      gfx::Rect windowBounds(window->bounds());
      gfx::Border margin;
      if (widget->style())
        margin = widget->style()->margin();

      buttonBounds.offset(windowBounds.x2() - margin.right() - buttonBounds.w,
                          windowBounds.y + margin.top());

      widget->setBounds(buttonBounds);
      break;
    }
  }
}

void Theme::paintListBox(PaintEvent& ev)
{
  Graphics* g = ev.graphics();
  g->fillRect(kBgColor, g->getClipBounds());
}

void Theme::paintViewViewport(PaintEvent& ev)
{
  Graphics* g = ev.graphics();
  g->fillRect(kBgColor, g->getClipBounds());
}

void Theme::paintWidgetPart(Graphics* g,
                            const Style* style,
                            const gfx::Rect& bounds,
                            const PaintWidgetPartInfo& info)
{
  ASSERT(g);
  ASSERT(style);

  // External background
  if (!gfx::is_transparent(info.bgColor))
    g->fillRect(info.bgColor, bounds);

  gfx::Rect rc = bounds;
  gfx::Color outBgColor = gfx::ColorNone;
  for_each_layer(info.styleFlags,
                 style,
                 [this, g, style, &info, &rc, &outBgColor](const Style::Layer& layer) {
                   paintLayer(g,
                              style,
                              layer,
                              (info.text ? *info.text : std::string()),
                              info.textBlob,
                              info.baseline,
                              info.mnemonic,
                              info.icon,
                              rc,
                              outBgColor);
                 });
}

void Theme::paintWidget(Graphics* g,
                        const Widget* widget,
                        const Style* style,
                        const gfx::Rect& bounds)
{
  ASSERT(widget);

  PaintWidgetPartInfo info(widget);
  paintWidgetPart(g, style, bounds, info);
}

void Theme::paintScrollBar(Graphics* g,
                           const Widget* widget,
                           const Style* style,
                           const Style* thumbStyle,
                           const gfx::Rect& bounds,
                           const gfx::Rect& thumbBounds)
{
  PaintWidgetPartInfo info(widget);
  paintWidgetPart(g, style, bounds, info);

  // TODO flags for the thumb could have "mouse" only
  //      when the mouse is inside the thumb

  info.bgColor = gfx::ColorNone;
  paintWidgetPart(g, thumbStyle, thumbBounds, info);
}

void Theme::paintTooltip(Graphics* g,
                         const Widget* widget,
                         const Style* style,
                         const Style* arrowStyle,
                         const gfx::Rect& bounds,
                         const int arrowAlign,
                         const gfx::Rect& target)
{
  if (style)
    paintWidget(g, widget, style, bounds);

  // Draw arrow
  if (arrowStyle && arrowAlign) {
    gfx::Size topLeft;
    gfx::Size center;
    gfx::Size bottomRight;
    calcSlices(widget, arrowStyle, topLeft, center, bottomRight);

    gfx::Rect clip,
      rc(0, 0, topLeft.w + center.w + bottomRight.w, topLeft.h + center.h + bottomRight.h);

    if (arrowAlign & LEFT) {
      clip.w = topLeft.w;
      clip.x = bounds.x;
      rc.x = bounds.x;
    }
    else if (arrowAlign & RIGHT) {
      clip.w = bottomRight.w;
      clip.x = bounds.x + bounds.w - clip.w;
      rc.x = bounds.x2() - rc.w;
    }
    else {
      clip.w = center.w;
      clip.x = target.x + target.w / 2 - clip.w / 2;
      rc.x = clip.x - topLeft.w;
    }

    if (arrowAlign & TOP) {
      clip.h = topLeft.h;
      clip.y = bounds.y;
      rc.y = bounds.y;
    }
    else if (arrowAlign & BOTTOM) {
      clip.h = bottomRight.h;
      clip.y = bounds.y + bounds.h - clip.h;
      rc.y = bounds.y2() - rc.h;
    }
    else {
      clip.h = center.h;
      clip.y = target.y + target.h / 2 - clip.h / 2;
      rc.y = clip.y - topLeft.h;
    }

    IntersectClip intClip(g, clip);
    if (intClip)
      paintWidget(g, widget, arrowStyle, rc);
  }
}

void Theme::paintTextBoxWithStyle(Graphics* g, const Widget* widget)
{
  gfx::Color bg = gfx::ColorNone, fg = gfx::ColorNone;

  for_each_layer(PaintWidgetPartInfo::getStyleFlagsForWidget(widget),
                 widget->style(),
                 [&fg, &bg](const Style::Layer& layer) {
                   switch (layer.type()) {
                     case Style::Layer::Type::kBackground: bg = layer.color(); break;
                     case Style::Layer::Type::kText:       fg = layer.color(); break;
                   }
                 });

  if (fg != gfx::ColorNone)
    Theme::drawTextBox(g, widget, nullptr, nullptr, bg, fg);
}

void Theme::paintLayer(Graphics* g,
                       const Style* style,
                       const Style::Layer& layer,
                       const std::string& text,
                       text::TextBlobRef textBlob,
                       const float baseline,
                       const int mnemonic,
                       os::Surface* providedIcon,
                       gfx::Rect& rc,
                       gfx::Color& bgColor)
{
  ASSERT(style);
  if (!style)
    return;

  switch (layer.type()) {
    case Style::Layer::Type::kBackground:
    case Style::Layer::Type::kBackgroundBorder:
      if (layer.spriteSheet() && !layer.spriteBounds().isEmpty()) {
        if (!layer.slicesBounds().isEmpty()) {
          Theme::drawSlices(g,
                            layer.spriteSheet(),
                            rc,
                            layer.spriteBounds(),
                            layer.slicesBounds(),
                            layer.color(),
                            true);

          if (layer.type() == Style::Layer::Type::kBackgroundBorder) {
            rc.x += layer.slicesBounds().x;
            rc.y += layer.slicesBounds().y;
            rc.w -= layer.spriteBounds().w - layer.slicesBounds().w;
            rc.h -= layer.spriteBounds().h - layer.slicesBounds().h;
          }
        }
        // Draw background using different methods
        else {
          IntersectClip clip(g, rc);
          if (clip) {
            auto draw = getDrawSurfaceFunction(g, layer.spriteSheet(), layer.color());

            switch (layer.align()) {
              // Horizontal line
              case MIDDLE: {
                const float y = guiscaled_center(rc.y, rc.h, layer.spriteBounds().h);
                for (int x = rc.x; x < rc.x2(); x += layer.spriteBounds().w) {
                  draw(layer.spriteBounds().x,
                       layer.spriteBounds().y,
                       x,
                       y,
                       layer.spriteBounds().w,
                       layer.spriteBounds().h);
                }
                break;
              }

              // Vertical line
              case CENTER: {
                const float x = guiscaled_center(rc.x, rc.w, layer.spriteBounds().w);
                for (int y = rc.y; y < rc.y2(); y += layer.spriteBounds().h) {
                  draw(layer.spriteBounds().x,
                       layer.spriteBounds().y,
                       x,
                       y,
                       layer.spriteBounds().w,
                       layer.spriteBounds().h);
                }
                break;
              }

              // One instance
              case CENTER | MIDDLE: {
                const float x = guiscaled_center(rc.x, rc.w, layer.spriteBounds().w);
                const float y = guiscaled_center(rc.y, rc.h, layer.spriteBounds().h);
                draw(layer.spriteBounds().x,
                     layer.spriteBounds().y,
                     x,
                     y,
                     layer.spriteBounds().w,
                     layer.spriteBounds().h);
                break;
              }

              // Pattern
              case 0:
                for (int y = rc.y; y < rc.y2(); y += layer.spriteBounds().h) {
                  for (int x = rc.x; x < rc.x2(); x += layer.spriteBounds().w)
                    draw(layer.spriteBounds().x,
                         layer.spriteBounds().y,
                         x,
                         y,
                         layer.spriteBounds().w,
                         layer.spriteBounds().h);
                }
                break;
            }
          }
        }
      }
      else if (layer.color() != gfx::ColorNone) {
        bgColor = layer.color();
        g->fillRect(layer.color(), rc);
      }
      break;

    case Style::Layer::Type::kBorder:
      if (layer.spriteSheet() && !layer.spriteBounds().isEmpty() &&
          !layer.slicesBounds().isEmpty()) {
        Theme::drawSlices(g,
                          layer.spriteSheet(),
                          rc,
                          layer.spriteBounds(),
                          layer.slicesBounds(),
                          layer.color(),
                          false);

        rc.x += layer.slicesBounds().x;
        rc.y += layer.slicesBounds().y;
        rc.w -= layer.spriteBounds().w - layer.slicesBounds().w;
        rc.h -= layer.spriteBounds().h - layer.slicesBounds().h;
      }
      else if (layer.color() != gfx::ColorNone) {
        g->drawRect(layer.color(), rc);
      }
      break;

    case Style::Layer::Type::kText:
      if (text.empty())
        break;

      if (layer.color() != gfx::ColorNone) {
        text::FontRef oldFont = g->font();
        if (style->font())
          g->setFont(style->font());

        if (layer.align() & WORDWRAP) {
          gfx::Rect textBounds = rc;
          textBounds.offset(layer.offset());

          g->drawAlignedUIText(text, layer.color(), bgColor, textBounds, layer.align());
        }
        else {
          if (!textBlob || style->font() != nullptr)
            textBlob = text::TextBlob::MakeWithShaper(m_fontMgr, g->font(), text);

          const gfx::RectF blobSize = textBlob->bounds();
          const gfx::Border padding = style->padding();
          gfx::PointF pt;

          if (layer.align() & LEFT)
            pt.x = rc.x + padding.left();
          else if (layer.align() & RIGHT)
            pt.x = rc.x + rc.w - blobSize.w - padding.right();
          else
            pt.x = guiscaled_center(rc.x + padding.left(), rc.w - padding.width(), blobSize.w);

          if (layer.align() & TOP)
            pt.y = rc.y + padding.top();
          else if (layer.align() & BOTTOM)
            pt.y = rc.y + rc.h - blobSize.h - padding.bottom();
          else
            pt.y = baseline - textBlob->baseline();

          pt += layer.offset();

          Paint paint;
          if (gfx::geta(bgColor) > 0) { // Paint background
            paint.color(bgColor);
            paint.style(os::Paint::Fill);
            g->drawRect(gfx::RectF(textBlob->bounds()).offset(pt), paint);
          }
          paint.color(layer.color());
          g->drawTextBlob(textBlob, gfx::PointF(pt), paint);

          if (style->mnemonics() && mnemonic != 0)
            drawMnemonicUnderline(g, text, textBlob, pt, mnemonic, paint);
        }

        if (style->font())
          g->setFont(oldFont);
      }
      break;

    case Style::Layer::Type::kIcon: {
      os::Surface* icon = providedIcon ? providedIcon : layer.icon();
      if (icon) {
        const gfx::Size iconSize(icon->width(), icon->height());
        const gfx::Border padding = style->padding();
        gfx::Point pt;

        if (layer.align() & LEFT)
          pt.x = rc.x + padding.left();
        else if (layer.align() & RIGHT)
          pt.x = rc.x + rc.w - iconSize.w - padding.right();
        else {
          pt.x = guiscaled_center(rc.x + padding.left(), rc.w - padding.width(), iconSize.w);
        }

        if (layer.align() & TOP)
          pt.y = rc.y + padding.top();
        else if (layer.align() & BOTTOM)
          pt.y = rc.y + rc.h - iconSize.h - padding.bottom();
        else {
          pt.y = guiscaled_center(rc.y + padding.top(), rc.h - padding.height(), iconSize.h);
        }

        pt += layer.offset();

        if (layer.color() != gfx::ColorNone)
          g->drawColoredRgbaSurface(icon, layer.color(), pt.x, pt.y);
        else
          g->drawRgbaSurface(icon, pt.x, pt.y);
      }
      break;
    }
  }
}

gfx::Size Theme::calcSizeHint(const Widget* widget, const Style* style)
{
  gfx::Size sizeHint;
  gfx::Border borderHint;
  gfx::Rect textHint;
  int textAlign;
  calcWidgetMetrics(widget, style, sizeHint, borderHint, textHint, textAlign);
  return sizeHint;
}

void Theme::calcTextInfo(const Widget* widget,
                         const Style* style,
                         const gfx::Rect& bounds,
                         gfx::Rect& textBounds,
                         int& textAlign)
{
  gfx::Size sizeHint;
  gfx::Border borderHint;
  gfx::Rect textHint;
  calcWidgetMetrics(widget, style, sizeHint, borderHint, textHint, textAlign);

  textBounds = bounds;
  textBounds.shrink(borderHint);
  textBounds.offset(textHint.origin());
}

void Theme::measureLayer(const Widget* widget,
                         const Style* style,
                         const Style::Layer& layer,
                         gfx::Border& borderHint,
                         gfx::Rect& textHint,
                         int& textAlign,
                         gfx::Size& iconHint,
                         int& iconAlign)
{
  ASSERT(style);
  if (!style)
    return;

  switch (layer.type()) {
    case Style::Layer::Type::kBackground:
    case Style::Layer::Type::kBackgroundBorder:
    case Style::Layer::Type::kBorder:
      if (layer.spriteSheet() && !layer.spriteBounds().isEmpty()) {
        if (!layer.slicesBounds().isEmpty()) {
          borderHint.left(std::max(borderHint.left(), layer.slicesBounds().x));
          borderHint.top(std::max(borderHint.top(), layer.slicesBounds().y));
          borderHint.right(
            std::max(borderHint.right(), layer.spriteBounds().w - layer.slicesBounds().x2()));
          borderHint.bottom(
            std::max(borderHint.bottom(), layer.spriteBounds().h - layer.slicesBounds().y2()));
        }
        else {
          iconHint.w = std::max(iconHint.w, layer.spriteBounds().w);
          iconHint.h = std::max(iconHint.h, layer.spriteBounds().h);
        }
      }
      break;

    case Style::Layer::Type::kText:
      if (layer.color() != gfx::ColorNone) {
        const text::FontRef& styleFont = style->font();
        gfx::Size textSize;
        if (styleFont && styleFont != widget->font()) {
          textSize = gfx::Size(styleFont->textLength(widget->text()), styleFont->lineHeight());
        }
        else {
          // We can use Widget::textSize() because we're going to use
          // the widget font and, probably, the cached TextBlob size.
          textSize = widget->textSize();
        }

        textHint.offset(layer.offset());
        textHint.w = std::max(textHint.w, textSize.w + ABS(layer.offset().x));
        textHint.h = std::max(textHint.h, textSize.h + ABS(layer.offset().y));
        textAlign = layer.align();
      }
      break;

    case Style::Layer::Type::kIcon: {
      const os::Surface* icon = layer.icon();
      if (const Style::Layer::IconSurfaceProvider* iconProvider =
            dynamic_cast<const Style::Layer::IconSurfaceProvider*>(widget)) {
        icon = iconProvider->iconSurface() ? iconProvider->iconSurface() : icon;
      }

      if (icon) {
        iconHint.w = std::max(iconHint.w, icon->width() + ABS(layer.offset().x));
        iconHint.h = std::max(iconHint.h, icon->height() + ABS(layer.offset().y));
        iconAlign = layer.align();
      }
      break;
    }
  }
}

gfx::Border Theme::calcBorder(const Widget* widget, const Style* style)
{
  gfx::Size sizeHint;
  gfx::Border borderHint;
  gfx::Rect textHint;
  int textAlign;
  calcWidgetMetrics(widget, style, sizeHint, borderHint, textHint, textAlign);
  return borderHint;
}

void Theme::calcSlices(const Widget* widget,
                       const Style* style,
                       gfx::Size& topLeft,
                       gfx::Size& center,
                       gfx::Size& bottomRight)
{
  ASSERT(widget);
  ASSERT(style);

  for_each_layer(widget, style, [&topLeft, &center, &bottomRight](const Style::Layer& layer) {
    if (layer.spriteSheet() && !layer.spriteBounds().isEmpty() && !layer.slicesBounds().isEmpty()) {
      gfx::Rect sprite = layer.spriteBounds();
      gfx::Rect slices = layer.slicesBounds();
      topLeft.w = std::max(topLeft.w, slices.x);
      topLeft.h = std::max(topLeft.h, slices.y);
      center.w = std::max(center.w, slices.w);
      center.h = std::max(center.h, slices.h);
      bottomRight.w = std::max(bottomRight.w, sprite.w - slices.x2());
      bottomRight.h = std::max(bottomRight.h, sprite.h - slices.y2());
    }
  });
}

gfx::Color Theme::calcBgColor(const Widget* widget, const Style* style)
{
  ASSERT(widget);
  ASSERT(style);

  gfx::Color bgColor = gfx::ColorNone;

  for_each_layer(widget, style, [&bgColor](const Style::Layer& layer) {
    if (layer.type() == Style::Layer::Type::kBackground ||
        layer.type() == Style::Layer::Type::kBackgroundBorder)
      bgColor = layer.color();
  });

  return bgColor;
}

gfx::Size Theme::calcMinSize(const Widget* widget, const Style* style)
{
  ASSERT(widget);
  ASSERT(style);

  gfx::Size sz = widget->minSize();

  if (sz.w == 0 || style->minSize().w > 0)
    sz.w = style->minSize().w;
  if (sz.h == 0 || style->minSize().h > 0)
    sz.h = style->minSize().h;

  return sz;
}

gfx::Size Theme::calcMaxSize(const Widget* widget, const Style* style)
{
  ASSERT(widget);
  ASSERT(style);

  gfx::Size sz = widget->maxSize();

  int maxInt = std::numeric_limits<int>::max();
  if (sz.w == maxInt || style->maxSize().w < maxInt)
    sz.w = style->maxSize().w;
  if (sz.h == maxInt || style->maxSize().h < maxInt)
    sz.h = style->maxSize().h;

  return sz;
}

void Theme::calcWidgetMetrics(const Widget* widget,
                              const Style* style,
                              gfx::Size& sizeHint,
                              gfx::Border& borderHint,
                              gfx::Rect& textHint,
                              int& textAlign)
{
  ASSERT(widget);
  ASSERT(style);
  if (!style)
    return;

  borderHint = gfx::Border(0, 0, 0, 0);
  gfx::Border paddingHint(0, 0, 0, 0);

  textHint = gfx::Rect(0, 0, 0, 0);
  textAlign = CENTER | MIDDLE;

  gfx::Size iconHint(0, 0);
  int iconAlign = CENTER | MIDDLE;

  for_each_layer(
    widget,
    style,
    [this, widget, style, &borderHint, &textHint, &textAlign, &iconHint, &iconAlign](
      const Style::Layer& layer) {
      measureLayer(widget, style, layer, borderHint, textHint, textAlign, iconHint, iconAlign);
    });

  Style::applyOnlyDefinedBorders(borderHint, style->rawBorder());
  Style::applyOnlyDefinedBorders(paddingHint, style->rawPadding());

  sizeHint = gfx::Size(borderHint.width() + paddingHint.width(),
                       borderHint.height() + paddingHint.height());

  if ((textAlign & (LEFT | CENTER | RIGHT)) == (iconAlign & (LEFT | CENTER | RIGHT)))
    sizeHint.w += std::max(textHint.w, iconHint.w);
  else
    sizeHint.w += textHint.w + iconHint.w;

  if ((textAlign & (TOP | MIDDLE | BOTTOM)) == (iconAlign & (TOP | MIDDLE | BOTTOM)))
    sizeHint.h += std::max(textHint.h, iconHint.h);
  else
    sizeHint.h += textHint.h + iconHint.h;

  sizeHint.w = std::clamp(sizeHint.w, widget->minSize().w, widget->maxSize().w);
  sizeHint.h = std::clamp(sizeHint.h, widget->minSize().h, widget->maxSize().h);
}

//////////////////////////////////////////////////////////////////////

void set_theme(Theme* theme, const int uiscale)
{
  old_ui_scale = current_ui_scale;
  current_ui_scale = uiscale;
  current_theme = theme;

  if (theme)
    theme->regenerateTheme();

  // Set the theme for all widgets (even if the theme is nullptr, so
  // widgets don't contain a pointer to a destroyed theme).
  details::reinitThemeForAllWidgets();

  // Reinitialize all widget using the new theme/uiscale
  if (Manager* manager = Manager::getDefault()) {
    manager->initTheme();
    manager->invalidate();
  }

  old_ui_scale = current_ui_scale;
}

Theme* get_theme()
{
  return current_theme;
}

int guiscale()
{
  return current_ui_scale;
}

int details::old_guiscale()
{
  return old_ui_scale;
}

// static
void Theme::drawSlices(Graphics* g,
                       os::Surface* sheet,
                       const gfx::Rect& rc,
                       const gfx::Rect& sprite,
                       const gfx::Rect& slices,
                       const gfx::Color color,
                       const bool drawCenter)
{
  Paint paint;
  paint.color(color);
  g->drawSurfaceNine(sheet, sprite, slices, rc, drawCenter, &paint);
}

// static
void Theme::drawTextBox(Graphics* g,
                        const Widget* widget,
                        int* w,
                        int* h,
                        gfx::Color bg,
                        gfx::Color fg)
{
  View* view = (g ? View::getView(widget) : nullptr);
  char* text = const_cast<char*>(widget->text().c_str());
  char *beg, *end;
  int x1, y1;
  int x, y, chr, len;
  gfx::Point scroll;
  int textheight = widget->textHeight();
  const text::FontRef& font = widget->font();
  char *beg_end, *old_end;
  int width;
  gfx::Rect vp;

  if (view) {
    vp = view->viewportBounds().offset(-widget->bounds().origin());
    scroll = view->viewScroll();
  }
  else {
    vp = widget->clientBounds();
    scroll.x = scroll.y = 0;
  }
  x1 = widget->clientBounds().x + widget->border().left();
  y1 = widget->clientBounds().y + widget->border().top();

  // Fill background
  if (g)
    g->fillRect(bg, vp);

  chr = 0;

  // Without word-wrap
  if (!(widget->align() & WORDWRAP)) {
    width = widget->clientChildrenBounds().w;
  }
  // With word-wrap
  else {
    if (w) {
      width = *w;
      *w = 0;
    }
    else {
      // TODO modificable option? I don't think so, this is very internal stuff
#if 0
      // Shows more information in x-scroll 0
      width = vp.w;
#else
      // Make good use of the complete text-box
      if (view) {
        gfx::Size maxSize = view->getScrollableSize();
        width = std::max(vp.w, maxSize.w);
      }
      else {
        width = vp.w;
      }
      width -= widget->border().width();
#endif
    }
  }

  // Draw line-by-line
  y = y1;
  for (beg = end = text; end;) {
    x = x1;

    // Without word-wrap
    if (!(widget->align() & WORDWRAP)) {
      end = std::strchr(beg, '\n');
      if (end) {
        chr = *end;
        *end = 0;
      }
    }
    // With word-wrap
    else {
      old_end = nullptr;
      for (beg_end = beg;;) {
        end = std::strpbrk(beg_end, " \n");
        if (end) {
          chr = *end;
          *end = 0;
        }

        // To here we can print
        if ((old_end) && (x + font->textLength(beg) > x1 + width - scroll.x)) {
          if (end)
            *end = chr;

          end = old_end;
          chr = *end;
          *end = 0;
          break;
        }
        // We can print one word more
        else if (end) {
          // Force break
          if (chr == '\n')
            break;

          *end = chr;
          beg_end = end + 1;
        }
        // We are in the end of text
        else
          break;

        old_end = end;
      }
    }

    len = font->textLength(beg);

    // Render the text
    if (g && len > 0) {
      int xout;

      if (widget->align() & CENTER)
        xout = x + width / 2 - len / 2;
      else if (widget->align() & RIGHT)
        xout = x + width - len;
      else // Left align
        xout = x;

      g->drawText(beg, fg, gfx::ColorNone, gfx::Point(xout, y));
    }

    if (w)
      *w = std::max(*w, len);

    y += textheight;

    if (end) {
      *end = chr;
      beg = end + 1;
    }
  }

  if (h)
    *h = (y - y1 + scroll.y);

  if (w)
    *w += widget->border().width();
  if (h)
    *h += widget->border().height();
}

// static
void Theme::drawMnemonicUnderline(Graphics* g,
                                  const std::string& text,
                                  text::TextBlobRef textBlob,
                                  const gfx::PointF& pt,
                                  const int mnemonic,
                                  const Paint& paint)
{
  base::utf8_decode decode(text);
  int pos = decode.pos() - text.begin();
  int mnemonicUtf8Pos = -1;
  while (int chr = decode.next()) {
    if (std::tolower(chr) == std::tolower(mnemonic)) {
      mnemonicUtf8Pos = pos;
      break;
    }
    pos = decode.pos() - text.begin();
  }

  if (mnemonicUtf8Pos >= 0) {
    decode = base::utf8_decode(text);
    decode.next(); // Go to first char
    size_t glyphUtf8Begin = 0;

    float baseline = textBlob->baseline();
    textBlob->visitRuns([g, baseline, mnemonicUtf8Pos, pt, &paint, &decode, &glyphUtf8Begin, &text](
                          text::TextBlob::RunInfo& info) {
      for (int i = 0; i < info.glyphCount; ++i, decode.next()) {
        // TODO This doesn't work because the TextBlob::RunInfo::clusters is nullptr at this
        //      point, it's only valid when the RunHandler::commitRunBuffer()
        if (info.clusters)
          glyphUtf8Begin = info.getGlyphUtf8Range(i).begin;

        if (mnemonicUtf8Pos == glyphUtf8Begin) {
          text::FontMetrics metrics;
          info.font->metrics(&metrics);

          gfx::RectF mnemonicBounds = info.getGlyphBounds(i);
          float thickness = metrics.underlineThickness * guiscale();
          if (thickness < 1.0f)
            thickness = 1.0f;

          mnemonicBounds = gfx::RectF(pt.x + mnemonicBounds.x,
                                      pt.y + baseline + metrics.underlinePosition * guiscale(),
                                      mnemonicBounds.w,
                                      thickness);

          g->drawRect(mnemonicBounds, paint);
          break;
        }

        glyphUtf8Begin = decode.pos() - text.begin();
      }
    });
  }
}

} // namespace ui
