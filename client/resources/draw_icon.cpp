// family-signal icon v2: red-and-white lighthouse, green light radiating
// outwards in concentric waves (a "signal" motif).
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QLinearGradient>
#include <cstdio>

static void draw(QPainter &p, int S) {
    const qreal s = S / 1024.0;
    p.setRenderHint(QPainter::Antialiasing);

    // Rounded-square night background
    QPainterPath bg;
    bg.addRoundedRect(QRectF(0, 0, S, S), S * 0.22, S * 0.22);
    QLinearGradient sky(0, 0, 0, S);
    sky.setColorAt(0.0, QColor(0x10, 0x1a, 0x26));
    sky.setColorAt(1.0, QColor(0x0d, 0x23, 0x2b));
    p.fillPath(bg, sky);
    p.setClipPath(bg);

    const QPointF lamp(512*s, 300*s);

    // --- Green signal waves: concentric rings radiating from the lamp ----
    p.setBrush(Qt::NoBrush);
    const struct { qreal r; int alpha; qreal w; } rings[] = {
        {150, 235, 26}, {260, 170, 24}, {370, 110, 22}, {480, 60, 20},
    };
    for (const auto &r : rings) {
        QPen pen(QColor(0x4c, 0xff, 0x9e, r.alpha), r.w * s);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.drawEllipse(lamp, r.r * s, r.r * s);
    }

    // Soft glow behind the lamp
    QRadialGradient glow(lamp, 200*s);
    glow.setColorAt(0.0, QColor(0x9d, 0xff, 0xc3, 200));
    glow.setColorAt(0.5, QColor(0x4c, 0xff, 0x9e, 70));
    glow.setColorAt(1.0, QColor(0x4c, 0xff, 0x9e, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(lamp, 200*s, 200*s);

    // --- Rock base ---------------------------------------------------------
    QPainterPath rock;
    rock.moveTo(320*s, 920*s);
    rock.quadTo(512*s, 838*s, 704*s, 920*s);
    rock.lineTo(704*s, 960*s); rock.lineTo(320*s, 960*s);
    rock.closeSubpath();
    p.fillPath(rock, QColor(0x2a, 0x33, 0x3c));

    // --- Tower: red & white stripes -----------------------------------------
    QPainterPath tower;
    tower.moveTo(424*s, 884*s);
    tower.lineTo(466*s, 396*s);
    tower.lineTo(558*s, 396*s);
    tower.lineTo(600*s, 884*s);
    tower.closeSubpath();
    p.fillPath(tower, QColor(0xf4, 0xf6, 0xf8));
    p.save();
    p.setClipPath(tower, Qt::IntersectClip);
    p.setBrush(QColor(0xe0, 0x3e, 0x36));   // lighthouse red
    p.setPen(Qt::NoPen);
    const qreal stripeTop[] = {396, 520, 644, 768};
    for (qreal y : stripeTop)
        p.drawRect(QRectF(390*s, y*s, 360*s, 62*s));
    p.restore();

    // Gallery under the lantern
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x26, 0x31, 0x3b));
    p.drawRoundedRect(QRectF(440*s, 366*s, 144*s, 32*s), 10*s, 10*s);

    // --- Lantern room with green core ----------------------------------------
    QRectF lantern(460*s, 244*s, 104*s, 124*s);
    p.setBrush(QColor(0x1c, 0x26, 0x30));
    p.drawRoundedRect(lantern.adjusted(-8*s, -6*s, 8*s, 6*s), 12*s, 12*s);
    QRadialGradient core(lamp, 62*s);
    core.setColorAt(0, QColor(0xe8, 0xff, 0xf2));
    core.setColorAt(0.55, QColor(0x4c, 0xff, 0x9e));
    core.setColorAt(1, QColor(0x14, 0x8f, 0x66));
    p.setBrush(core);
    p.drawRoundedRect(lantern, 10*s, 10*s);
    p.setPen(QPen(QColor(0x1c, 0x26, 0x30), 8*s));
    p.drawLine(QPointF(512*s, 244*s), QPointF(512*s, 368*s));

    // --- Roof (red cone) + finial --------------------------------------------
    p.setPen(Qt::NoPen);
    QPainterPath roof;
    roof.moveTo(442*s, 238*s);
    roof.lineTo(512*s, 156*s);
    roof.lineTo(582*s, 238*s);
    roof.closeSubpath();
    p.fillPath(roof, QColor(0xe0, 0x3e, 0x36));
    p.setBrush(QColor(0x9d, 0xff, 0xc3));
    p.drawEllipse(QPointF(512*s, 146*s), 12*s, 12*s);
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const int sizes[] = {1024, 512, 256, 128, 64, 48, 32, 16};
    for (int sz : sizes) {
        QImage img(sz, sz, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter p(&img);
        draw(p, sz);
        p.end();
        img.save(QStringLiteral("/tmp/icon_%1.png").arg(sz));
    }
    printf("done\n");
    return 0;
}
