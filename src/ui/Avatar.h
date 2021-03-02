#pragma once

#include <QImage>
#include <QPixmap>
#include <QWidget>

#include "Theme.h"
#include "jdenticoninterface.h"

class Avatar : public QWidget
{
        Q_OBJECT

        Q_PROPERTY(QColor textColor WRITE setTextColor READ textColor)
        Q_PROPERTY(QColor backgroundColor WRITE setBackgroundColor READ backgroundColor)

public:
        explicit Avatar(QWidget *parent = nullptr, int size = ui::AvatarSize);

        void setBackgroundColor(const QColor &color);
        void setImage(const QString &avatar_url);
        void setImage(const QString &room, const QString &user);
        void setFiller(const QString &id);
        void setLetter(const QString &letter);
        void setJdenticon(const QString &userId);
        void setTextColor(const QColor &color);
        void setDevicePixelRatio(double ratio);

        QColor backgroundColor() const;
        QColor textColor() const;

        QSize sizeHint() const override;

protected:
        void paintEvent(QPaintEvent *event) override;

private:
        void init();

        ui::AvatarType type_;
        QString letter_;
        QString avatar_url_, room_, user_;
        QColor background_color_;
        QColor text_color_;
        QPixmap pixmap_;
        int size_;
        JdenticonInterface *jdenticonInterface_;
};
