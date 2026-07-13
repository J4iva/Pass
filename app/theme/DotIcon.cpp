// SPDX-License-Identifier: GPL-3.0-or-later
#include "DotIcon.h"

#include <QPainter>
#include <QPainterPath>

namespace pass::theme {

namespace {

// 10 filas x 10 columnas. '#' = dot encendido, '.' = apagado.
struct Bitmap {
    const char* rows[10];
};

constexpr Bitmap kGrid{
    {"##..##..##", "##..##..##", "..........", "##..##..##", "##..##..##",
     "..........", "##..##..##", "##..##..##", "..........", ".........."}};
constexpr Bitmap kCalendar{
    {"##########", "##.####.##", "#........#", "#.#..#...#", "#........#",
     "#..#..#..#", "#........#", "#.#..#...#", "#........#", "##########"}};
constexpr Bitmap kNote{
    {".########.", ".#......#.", ".#.####.#.", ".#......#.", ".#.####.#.",
     ".#......#.", ".#.####.#.", ".#......#.", ".########.", ".........."}};
constexpr Bitmap kClock{
    {"...####...", ".##....##.", "##..##..##", "#...##...#", "#...##...#",
     "#........#", "##......##", ".##....##.", "...####...", ".........."}};
constexpr Bitmap kBars{
    {"..........", "......##..", "......##..", ".....###..", ".....###..",
     "....####..", "....####..", "##########", "##########", ".........."}};
constexpr Bitmap kSliders{
    {"..........", "##########", "...##.....", "...##.....", "...##.....",
     "..........", "##########", "......##..", "......##..", ".........."}};
constexpr Bitmap kPlay{
    {"#.........", "###.......", "#####.....", "#######...", "#########.",
     "#########.", "#######...", "#####.....", "###.......", "#........."}};
constexpr Bitmap kPause{
    {".###..###.", ".###..###.", ".###..###.", ".###..###.", ".###..###.",
     ".###..###.", ".###..###.", ".###..###.", ".###..###.", ".........."}};
constexpr Bitmap kCheck{
    {"..........", ".........#", "........##", ".......##.", "##....##..",
     "###..##...", ".####.....", "..###.....", "...##.....", ".........."}};
constexpr Bitmap kWarn{
    {"....##....", "...####...", "...####...", "..##..##..", "..##.##...",
     ".##..##...", ".##....##.", ".#......#.", "##########", "....##...."}};
constexpr Bitmap kSync{
    {"...####...", ".##....##.", ".#......##", "#........#", "#..####..#",
     "#..####..#", ".#......#.", ".##....##.", "...####...", ".........."}};
constexpr Bitmap kTask{
    {".#####....", "#.....#...", "#.###.#...", "#.....#...", "#.....#...",
     "#.....#...", "#.....#...", "#.....#...", "#.....#...", ".#####...."}};
constexpr Bitmap kStudy{
    {"..........", ".#......#.", "##......##", "#........#", "#..####..#",
     "#........#", "#........#", "##......##", ".#......#.", ".........."}};
constexpr Bitmap kWeb{
    {"...####...", ".##....##.", "##..##..##", "#..####..#", "#########.",
     "#..####..#", "##..##..##", ".##....##.", "...####...", ".........."}};
constexpr Bitmap kPlus{
    {"....##....", "....##....", "....##....", "....##....", "##########",
     "##########", "....##....", "....##....", "....##....", ".........."}};
constexpr Bitmap kEdit{
    {"........##", ".......##.", "......##..", ".....##...", "....##....",
     "...##.....", "..##......", ".##.......", "##........", ".........."}};
constexpr Bitmap kTrash{
    {".##....##.", "##########", ".#......#.", ".#.#..#.#.", ".#.#..#.#.",
     ".#.#..#.#.", ".#.#..#.#.", ".#......#.", ".########.", ".........."}};
constexpr Bitmap kInfo{
    {"....##....", "....##....", "..........", "....##....", "....##....",
     "....##....", "....##....", "....##....", "....##....", ".........."}};

const Bitmap& bitmapFor(Glyph g) {
    switch (g) {
    case Glyph::Grid: return kGrid;
    case Glyph::Calendar: return kCalendar;
    case Glyph::Note: return kNote;
    case Glyph::Clock: return kClock;
    case Glyph::Bars: return kBars;
    case Glyph::Sliders: return kSliders;
    case Glyph::Play: return kPlay;
    case Glyph::Pause: return kPause;
    case Glyph::Check: return kCheck;
    case Glyph::Warn: return kWarn;
    case Glyph::Sync: return kSync;
    case Glyph::Task: return kTask;
    case Glyph::Study: return kStudy;
    case Glyph::Web: return kWeb;
    case Glyph::Plus: return kPlus;
    case Glyph::Edit: return kEdit;
    case Glyph::Trash: return kTrash;
    case Glyph::Info: return kInfo;
    }
    return kInfo;
}

// Pinta la rejilla 10x10 del bitmap escalada para llenar `rect` (cuadrado,
// centrado). Cada celda encendida -> disco.
void paintBitmap(QPainter* p, const Bitmap& bm, const QRect& rect, const QColor& color) {
    const int side = qMin(rect.width(), rect.height());
    const qreal cell = side / 10.0;
    const qreal d = cell * 0.78;
    const qreal ox = rect.left() + (rect.width() - side) / 2.0 + (cell - d) / 2.0;
    const qreal oy = rect.top() + (rect.height() - side) / 2.0 + (cell - d) / 2.0;
    p->save();
    p->setPen(Qt::NoPen);
    p->setBrush(color);
    p->setRenderHint(QPainter::Antialiasing, true);
    for (int r = 0; r < 10; ++r) {
        for (int c = 0; c < 10; ++c) {
            if (bm.rows[r][c] != '#')
                continue;
            const qreal x = ox + c * cell;
            const qreal y = oy + r * cell;
            p->drawEllipse(QRectF(x, y, d, d));
        }
    }
    p->restore();
}

} // namespace

void paintGlyph(QPainter* p, Glyph g, const QRect& rect, const QColor& color) {
    paintBitmap(p, bitmapFor(g), rect, color.isValid() ? color : kFg);
}

QPixmap glyphPixmap(Glyph g, int px, const QColor& color) {
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paintGlyph(&p, g, pm.rect(), color.isValid() ? color : kFg);
    return pm;
}

QIcon glyphIcon(Glyph g, int px, const QColor& color) {
    return QIcon(glyphPixmap(g, px, color.isValid() ? color : kFg));
}

void paintStateDot(QPainter* p, StateDot s, const QRect& rect) {
    const int side = qMin(rect.width(), rect.height());
    const qreal pad = side * 0.18;
    const QRectF r(QRectF(rect).center() - QPointF(side / 2.0 - pad, side / 2.0 - pad),
                   QSizeF(side - 2 * pad, side - 2 * pad));
    QColor fill, stroke;
    switch (s) {
    case StateDot::On:
        fill = kFg;
        stroke = kFg;
        break;
    case StateDot::Off:
        fill = Qt::transparent;
        stroke = kFgFaint;
        break;
    case StateDot::Error:
        fill = kAccent;
        stroke = kAccent;
        break;
    }
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(QPen(stroke, qMax(1.0, side * 0.10)));
    p->setBrush(fill);
    p->drawEllipse(r);
    p->restore();
}

QPixmap statePixmap(StateDot s, int px) {
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paintStateDot(&p, s, pm.rect());
    return pm;
}

} // namespace pass::theme
