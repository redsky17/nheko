#pragma once

#include <QColor>
#include <QHash>
#include <QObject>

namespace ui {
enum class AvatarType
{
        Image,
        Letter,
        Jdenticon
};

// Default font size.
const int FontSize = 16;

// Default avatar size. Width and height.
const int AvatarSize = 40;

enum class ButtonPreset
{
        FlatPreset,
        CheckablePreset
};

enum class RippleStyle
{
        CenteredRipple,
        PositionedRipple,
        NoRipple
};

enum class OverlayStyle
{
        NoOverlay,
        TintedOverlay,
        GrayOverlay
};

enum class Role
{
        Default,
        Primary,
        Secondary
};

enum class ButtonIconPlacement
{
        LeftIcon,
        RightIcon
};

enum class ProgressType
{
        DeterminateProgress,
        IndeterminateProgress
};

enum class Color
{
        Black,
        BrightWhite,
        FadedWhite,
        MediumWhite,
        DarkGreen,
        LightGreen,
        BrightGreen,
        Gray,
        Red,
        Blue,
        Transparent
};

} // namespace ui

class Theme : public QObject
{
        Q_OBJECT
public:
        explicit Theme(QObject *parent = nullptr);

        QColor getColor(const QString &key) const;

        void setColor(const QString &key, const QColor &color);
        void setColor(const QString &key, ui::Color color);

private:
        QColor rgba(int r, int g, int b, qreal a) const;

        QHash<QString, QColor> colors_;
};
