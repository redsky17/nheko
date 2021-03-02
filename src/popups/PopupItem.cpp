#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QStyleOption>

#include "../Utils.h"
#include "../ui/Avatar.h"
#include "PopupItem.h"

constexpr int PopupHMargin    = 4;
constexpr int PopupItemMargin = 3;

PopupItem::PopupItem(QWidget *parent)
  : QWidget(parent)
  , avatar_{new Avatar(this, conf::popup::avatar)}
  , hovering_{false}
{
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover);

        topLayout_ = new QHBoxLayout(this);
        topLayout_->setContentsMargins(
          PopupHMargin, PopupItemMargin, PopupHMargin, PopupItemMargin);

        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void
PopupItem::paintEvent(QPaintEvent *)
{
        QStyleOption opt;
        opt.init(this);
        QPainter p(this);
        style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

        if (underMouse() || hovering_)
                p.fillRect(rect(), hoverColor_);
}

RoomItem::RoomItem(QWidget *parent, const RoomSearchResult &res)
  : PopupItem(parent)
  , roomId_{QString::fromStdString(res.room_id)}
{
        auto name = QFontMetrics(QFont()).elidedText(
          QString::fromStdString(res.info.name), Qt::ElideRight, parentWidget()->width() - 10);

        avatar_->setFiller(name);

        roomName_ = new QLabel(name, this);
        roomName_->setMargin(0);

        topLayout_->addWidget(avatar_);
        topLayout_->addWidget(roomName_, 1);

        if (!res.info.avatar_url.empty())
                avatar_->setImage(QString::fromStdString(res.info.avatar_url));
}

void
RoomItem::updateItem(const RoomSearchResult &result)
{
        roomId_ = QString::fromStdString(std::move(result.room_id));

        auto name =
          QFontMetrics(QFont()).elidedText(QString::fromStdString(std::move(result.info.name)),
                                           Qt::ElideRight,
                                           parentWidget()->width() - 10);

        roomName_->setText(name);

        // if there is not an avatar set for the room, we want to at least show the filler
        // correctly!
        avatar_->setFiller(name);
        if (!result.info.avatar_url.empty())
                avatar_->setImage(QString::fromStdString(result.info.avatar_url));
}

void
RoomItem::mousePressEvent(QMouseEvent *event)
{
        if (event->buttons() != Qt::RightButton)
                emit clicked(selectedText());

        QWidget::mousePressEvent(event);
}
