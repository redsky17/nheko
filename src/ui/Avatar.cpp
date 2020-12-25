#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>

#include "AvatarProvider.h"
#include "UserSettingsPage.h"
#include "Utils.h"
#include "jdenticoninterface.h"
#include "ui/Avatar.h"

Avatar::Avatar(QWidget *parent, int size)
  : QWidget(parent)
  , size_(size)
  , jdenticonInterface_{Jdenticon::getJdenticonInterface()}
{
        if (UserSettings::instance()->useIdenticon()) {
                type_ = ui::AvatarType::Jdenticon;
                user_ = "?";
        } else {
                type_   = ui::AvatarType::Letter;
                letter_ = "?";
        }

        QFont _font(font());
        _font.setPointSizeF(ui::FontSize);
        setFont(_font);

        QSizePolicy policy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
        setSizePolicy(policy);
}

QColor
Avatar::textColor() const
{
        if (!text_color_.isValid())
                return QColor("black");

        return text_color_;
}

QColor
Avatar::backgroundColor() const
{
        if (!text_color_.isValid())
                return QColor("white");

        return background_color_;
}

QSize
Avatar::sizeHint() const
{
        return QSize(size_ + 2, size_ + 2);
}

void
Avatar::setTextColor(const QColor &color)
{
        text_color_ = color;
}

void
Avatar::setBackgroundColor(const QColor &color)
{
        background_color_ = color;
}

void
Avatar::setFiller(const QString &id)
{
        if (UserSettings::instance()->useIdenticon()) {
                type_ = ui::AvatarType::Jdenticon;
                user_ = id;
        } else {
                // If it's a matrix id we use the second letter.
                if (id.size() > 1 && id.at(0) == '@')
                        letter_ = QChar(id.at(1));
                else
                        letter_ = utils::firstChar(id);
                type_ = ui::AvatarType::Letter;
        }
        update();
}

void
Avatar::setLetter(const QString &letter)
{
        // If it's a matrix id we use the second letter.
        if (letter.size() > 1 && letter.at(0) == '@')
                letter_ = QChar(letter.at(1));
        else
                letter_ = utils::firstChar(letter);
        type_ = ui::AvatarType::Letter;
        update();
}

void
Avatar::setJdenticon(const QString &userId)
{
        type_ = ui::AvatarType::Jdenticon;
        user_ = userId;
        update();
}

void
Avatar::setImage(const QString &avatar_url)
{
        avatar_url_ = avatar_url;
        AvatarProvider::resolve(avatar_url,
                                static_cast<int>(size_ * pixmap_.devicePixelRatio()),
                                this,
                                [this, requestedRatio = pixmap_.devicePixelRatio()](QPixmap pm) {
                                        if (pm.isNull())
                                                return;
                                        type_   = ui::AvatarType::Image;
                                        pixmap_ = pm;
                                        pixmap_.setDevicePixelRatio(requestedRatio);
                                        update();
                                });
}

void
Avatar::setImage(const QString &room, const QString &user)
{
        room_ = room;
        user_ = user;
        AvatarProvider::resolve(room,
                                user,
                                static_cast<int>(size_ * pixmap_.devicePixelRatio()),
                                this,
                                [this, requestedRatio = pixmap_.devicePixelRatio()](QPixmap pm) {
                                        if (pm.isNull())
                                                return;
                                        type_   = ui::AvatarType::Image;
                                        pixmap_ = pm;
                                        pixmap_.setDevicePixelRatio(requestedRatio);
                                        update();
                                });
}

void
Avatar::setDevicePixelRatio(double ratio)
{
        if (type_ == ui::AvatarType::Image && abs(pixmap_.devicePixelRatio() - ratio) > 0.01) {
                pixmap_ = pixmap_.scaled(QSize(size_, size_) * ratio);
                pixmap_.setDevicePixelRatio(ratio);

                if (!avatar_url_.isEmpty())
                        setImage(avatar_url_);
                else
                        setImage(room_, user_);
        }
}

void
Avatar::paintEvent(QPaintEvent *)
{
        bool rounded = UserSettings::instance()->avatarCircles();

        QPainter painter(this);

        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform |
                               QPainter::TextAntialiasing);

        QRectF r     = rect();
        const int hs = size_ / 2;

        if (type_ != ui::AvatarType::Image) {
                QBrush brush;
                brush.setStyle(Qt::SolidPattern);
                brush.setColor(backgroundColor());

                painter.setPen(Qt::NoPen);
                painter.setBrush(brush);
                rounded ? painter.drawEllipse(r) : painter.drawRoundedRect(r, 3, 3);
        } else if (painter.isActive()) {
                setDevicePixelRatio(painter.device()->devicePixelRatioF());
        }

        switch (type_) {
        case ui::AvatarType::Image: {
                QPainterPath ppath;

                rounded ? ppath.addEllipse(width() / 2 - hs, height() / 2 - hs, size_, size_)
                        : ppath.addRoundedRect(r, 3, 3);

                painter.setClipPath(ppath);
                painter.drawPixmap(QRect(width() / 2 - hs, height() / 2 - hs, size_, size_),
                                   pixmap_);
                break;
        }
        case ui::AvatarType::Letter: {
                painter.setPen(textColor());
                painter.setBrush(Qt::NoBrush);
                painter.drawText(r.translated(0, -1), Qt::AlignCenter, letter_);
                break;
        }
        case ui::AvatarType::Jdenticon: {
                QSvgRenderer renderer;
                renderer.load(jdenticonInterface_->generate(user_, size_).toUtf8());
                renderer.render(&painter);
                break;
        }
        default:
                break;
        }
}
