/*
 * nheko Copyright (C) 2017  Konstantinos Sideris <siderisk@auth.gr>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QDateTime>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QtGlobal>

#include "AvatarProvider.h"
#include "Cache.h"
#include "ChatPage.h"
#include "Config.h"
#include "Logging.h"
#include "MatrixClient.h"
#include "RoomInfoListItem.h"
#include "Splitter.h"
#include "UserSettingsPage.h"
#include "Utils.h"
#include "ui/Ripple.h"
#include "ui/RippleOverlay.h"

constexpr int MaxUnreadCountDisplayed = 99;

struct WidgetMetrics
{
        int maxHeight;
        int iconSize;
        int padding;
        int unit;

        int unreadLineWidth;
        int unreadLineOffset;

        int inviteBtnX;
        int inviteBtnY;
};

WidgetMetrics
getMetrics(const QFont &font)
{
        WidgetMetrics m;

        const int height = QFontMetrics(font).lineSpacing();

        m.unit             = height;
        m.maxHeight        = std::ceil((double)height * 3.8);
        m.iconSize         = std::ceil((double)height * 2.8);
        m.padding          = std::ceil((double)height / 2.0);
        m.unreadLineWidth  = m.padding - m.padding / 3;
        m.unreadLineOffset = m.padding - m.padding / 4;

        m.inviteBtnX = m.iconSize + 2 * m.padding;
        m.inviteBtnY = m.iconSize / 2.0 + m.padding + m.padding / 3.0;

        return m;
}

void
RoomInfoListItem::init(QWidget *parent)
{
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover);

        auto wm = getMetrics(QFont{});
        setFixedHeight(wm.maxHeight);

        QPainterPath path;
        path.addRect(0, 0, parent->width(), height());

        ripple_overlay_ = new RippleOverlay(this);
        ripple_overlay_->setClipPath(path);
        ripple_overlay_->setClipping(true);

        avatar_ = new Avatar(nullptr, wm.iconSize);
        avatar_->setFiller(roomName_);
        avatar_->resize(wm.iconSize, wm.iconSize);

        unreadCountFont_.setPointSizeF(unreadCountFont_.pointSizeF() * 0.8);
        unreadCountFont_.setBold(true);

        bubbleDiameter_ = QFontMetrics(unreadCountFont_).averageCharWidth() * 3;

        menu_      = new QMenu(this);
        leaveRoom_ = new QAction(tr("Leave room"), this);
        connect(leaveRoom_, &QAction::triggered, this, [this]() { emit leaveRoom(roomId_); });

        connect(menu_, &QMenu::aboutToShow, this, [this]() {
                menu_->clear();
                menu_->addAction(leaveRoom_);

                menu_->addSection(QIcon(":/icons/icons/ui/tag.png"), tr("Tag room as:"));

                auto roomInfo = cache::singleRoomInfo(roomId_.toStdString());

                auto tags = ChatPage::instance()->communitiesList()->currentTags();

                // add default tag, remove server notice tag
                if (std::find(tags.begin(), tags.end(), "m.favourite") == tags.end())
                        tags.push_back("m.favourite");
                if (std::find(tags.begin(), tags.end(), "m.lowpriority") == tags.end())
                        tags.push_back("m.lowpriority");
                if (auto it = std::find(tags.begin(), tags.end(), "m.server_notice");
                    it != tags.end())
                        tags.erase(it);

                for (const auto &tag : tags) {
                        QString tagName;
                        if (tag == "m.favourite")
                                tagName = tr("Favourite", "Standard matrix tag for favourites");
                        else if (tag == "m.lowpriority")
                                tagName =
                                  tr("Low Priority", "Standard matrix tag for low priority rooms");
                        else if (tag == "m.server_notice")
                                tagName =
                                  tr("Server Notice", "Standard matrix tag for server notices");
                        else if ((tag.size() > 2 && tag.substr(0, 2) == "u.") ||
                                 tag.find(".") !=
                                   std::string::npos) // tag manager creates tags without u., which
                                                      // is wrong, but we still want to display them
                                tagName = QString::fromStdString(tag.substr(2));

                        if (tagName.isEmpty())
                                continue;

                        auto tagAction = menu_->addAction(tagName);
                        tagAction->setCheckable(true);
                        tagAction->setWhatsThis(tr("Adds or removes the specified tag.",
                                                   "WhatsThis hint for tag menu actions"));

                        for (const auto &riTag : roomInfo.tags) {
                                if (riTag == tag) {
                                        tagAction->setChecked(true);
                                        break;
                                }
                        }

                        connect(tagAction, &QAction::triggered, this, [this, tag](bool checked) {
                                if (checked)
                                        http::client()->put_tag(
                                          roomId_.toStdString(),
                                          tag,
                                          {},
                                          [tag](mtx::http::RequestErr err) {
                                                  if (err) {
                                                          nhlog::ui()->error(
                                                            "Failed to add tag: {}, {}",
                                                            tag,
                                                            err->matrix_error.error);
                                                  }
                                          });
                                else
                                        http::client()->delete_tag(
                                          roomId_.toStdString(),
                                          tag,
                                          [tag](mtx::http::RequestErr err) {
                                                  if (err) {
                                                          nhlog::ui()->error(
                                                            "Failed to delete tag: {}, {}",
                                                            tag,
                                                            err->matrix_error.error);
                                                  }
                                          });
                        });
                }

                auto newTagAction = menu_->addAction(tr("New tag...", "Add a new tag to the room"));
                connect(newTagAction, &QAction::triggered, this, [this]() {
                        QString tagName =
                          QInputDialog::getText(this,
                                                tr("New Tag", "Tag name prompt title"),
                                                tr("Tag:", "Tag name prompt"));
                        if (tagName.isEmpty())
                                return;

                        std::string tag = "u." + tagName.toStdString();

                        http::client()->put_tag(
                          roomId_.toStdString(), tag, {}, [tag](mtx::http::RequestErr err) {
                                  if (err) {
                                          nhlog::ui()->error("Failed to add tag: {}, {}",
                                                             tag,
                                                             err->matrix_error.error);
                                  }
                          });
                });
        });
}

RoomInfoListItem::RoomInfoListItem(QString room_id, const RoomInfo &info, QWidget *parent)
  : QWidget(parent)
  , roomType_{info.is_invite ? RoomType::Invited : RoomType::Joined}
  , roomId_(std::move(room_id))
  , roomName_{QString::fromStdString(std::move(info.name))}
  , isPressed_(false)
  , unreadMsgCount_(0)
  , unreadHighlightedMsgCount_(0)
{
        init(parent);
}

void
RoomInfoListItem::resizeEvent(QResizeEvent *)
{
        // Update ripple's clipping path.
        QPainterPath path;
        path.addRect(0, 0, width(), height());

        const auto sidebarSizes = splitter::calculateSidebarSizes(QFont{});

        if (width() > sidebarSizes.small)
                setToolTip("");
        else
                setToolTip(roomName_);

        ripple_overlay_->setClipPath(path);
        ripple_overlay_->setClipping(true);
}

void
RoomInfoListItem::paintEvent(QPaintEvent *event)
{
        Q_UNUSED(event);

        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::Antialiasing);

        QFontMetrics metrics(QFont{});

        QPen titlePen(titleColor_);
        QPen subtitlePen(subtitleColor_);

        auto wm = getMetrics(QFont{});

        QPixmap pixmap(avatar_->size() * p.device()->devicePixelRatioF());
        pixmap.setDevicePixelRatio(p.device()->devicePixelRatioF());
        if (isPressed_) {
                p.fillRect(rect(), highlightedBackgroundColor_);
                titlePen.setColor(highlightedTitleColor_);
                subtitlePen.setColor(highlightedSubtitleColor_);
                pixmap.fill(highlightedBackgroundColor_);
        } else if (underMouse()) {
                p.fillRect(rect(), hoverBackgroundColor_);
                titlePen.setColor(hoverTitleColor_);
                subtitlePen.setColor(hoverSubtitleColor_);
                pixmap.fill(hoverBackgroundColor_);
        } else {
                p.fillRect(rect(), backgroundColor_);
                titlePen.setColor(titleColor_);
                subtitlePen.setColor(subtitleColor_);
                pixmap.fill(backgroundColor_);
        }

        avatar_->render(&pixmap, QPoint(), QRegion(), RenderFlags(DrawChildren));
        p.drawPixmap(QPoint(wm.padding, wm.padding), pixmap);

        // Description line with the default font.
        int bottom_y = wm.maxHeight - wm.padding - metrics.ascent() / 2;

        const auto sidebarSizes = splitter::calculateSidebarSizes(QFont{});

        if (width() > sidebarSizes.small) {
                QFont headingFont;
                headingFont.setWeight(QFont::Medium);
                p.setFont(headingFont);
                p.setPen(titlePen);

                QFont tsFont;
                tsFont.setPointSizeF(tsFont.pointSizeF() * 0.9);
#if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
                const int msgStampWidth =
                  QFontMetrics(tsFont).width(lastMsgInfo_.descriptiveTime) + 4;
#else
                const int msgStampWidth =
                  QFontMetrics(tsFont).horizontalAdvance(lastMsgInfo_.descriptiveTime) + 4;
#endif
                // We use the full width of the widget if there is no unread msg bubble.
                const int bottomLineWidthLimit = (unreadMsgCount_ > 0) ? msgStampWidth : 0;

                // Name line.
                QFontMetrics fontNameMetrics(headingFont);
                int top_y = 2 * wm.padding + fontNameMetrics.ascent() / 2;

                const auto name = metrics.elidedText(
                  roomName(),
                  Qt::ElideRight,
                  (width() - wm.iconSize - 2 * wm.padding - msgStampWidth) * 0.8);
                p.drawText(QPoint(2 * wm.padding + wm.iconSize, top_y), name);

                if (roomType_ == RoomType::Joined) {
                        p.setFont(QFont{});
                        p.setPen(subtitlePen);

                        int descriptionLimit = std::max(
                          0, width() - 3 * wm.padding - bottomLineWidthLimit - wm.iconSize);
                        auto description =
                          metrics.elidedText(lastMsgInfo_.body, Qt::ElideRight, descriptionLimit);
                        p.drawText(QPoint(2 * wm.padding + wm.iconSize, bottom_y), description);

                        // We show the last message timestamp.
                        p.save();
                        if (isPressed_) {
                                p.setPen(QPen(highlightedTimestampColor_));
                        } else if (underMouse()) {
                                p.setPen(QPen(hoverTimestampColor_));
                        } else {
                                p.setPen(QPen(timestampColor_));
                        }

                        p.setFont(tsFont);
                        p.drawText(QPoint(width() - wm.padding - msgStampWidth, top_y),
                                   lastMsgInfo_.descriptiveTime);
                        p.restore();
                } else {
                        int btnWidth = (width() - wm.iconSize - 6 * wm.padding) / 2;

                        acceptBtnRegion_  = QRectF(wm.inviteBtnX, wm.inviteBtnY, btnWidth, 20);
                        declineBtnRegion_ = QRectF(
                          wm.inviteBtnX + btnWidth + 2 * wm.padding, wm.inviteBtnY, btnWidth, 20);

                        QPainterPath acceptPath;
                        acceptPath.addRoundedRect(acceptBtnRegion_, 10, 10);

                        p.setPen(Qt::NoPen);
                        p.fillPath(acceptPath, btnColor_);
                        p.drawPath(acceptPath);

                        QPainterPath declinePath;
                        declinePath.addRoundedRect(declineBtnRegion_, 10, 10);

                        p.setPen(Qt::NoPen);
                        p.fillPath(declinePath, btnColor_);
                        p.drawPath(declinePath);

                        p.setPen(QPen(btnTextColor_));
                        p.setFont(QFont{});
                        p.drawText(acceptBtnRegion_,
                                   Qt::AlignCenter,
                                   metrics.elidedText(tr("Accept"), Qt::ElideRight, btnWidth));
                        p.drawText(declineBtnRegion_,
                                   Qt::AlignCenter,
                                   metrics.elidedText(tr("Decline"), Qt::ElideRight, btnWidth));
                }
        }

        p.setPen(Qt::NoPen);

        if (unreadMsgCount_ > 0) {
                QBrush brush;
                brush.setStyle(Qt::SolidPattern);
                if (unreadHighlightedMsgCount_ > 0) {
                        brush.setColor(mentionedColor());
                } else {
                        brush.setColor(bubbleBgColor());
                }

                if (isPressed_)
                        brush.setColor(bubbleFgColor());

                p.setBrush(brush);
                p.setPen(Qt::NoPen);
                p.setFont(unreadCountFont_);

                // Extra space on the x-axis to accomodate the extra character space
                // inside the bubble.
                const int x_width = unreadMsgCount_ > MaxUnreadCountDisplayed
                                      ? QFontMetrics(p.font()).averageCharWidth()
                                      : 0;

                QRectF r(width() - bubbleDiameter_ - wm.padding - x_width,
                         bottom_y - bubbleDiameter_ / 2 - 5,
                         bubbleDiameter_ + x_width,
                         bubbleDiameter_);

                if (width() == sidebarSizes.small)
                        r = QRectF(width() - bubbleDiameter_ - 5,
                                   height() - bubbleDiameter_ - 5,
                                   bubbleDiameter_ + x_width,
                                   bubbleDiameter_);

                p.setPen(Qt::NoPen);
                p.drawEllipse(r);

                p.setPen(QPen(bubbleFgColor()));

                if (isPressed_)
                        p.setPen(QPen(bubbleBgColor()));

                auto countTxt = unreadMsgCount_ > MaxUnreadCountDisplayed
                                  ? QString("99+")
                                  : QString::number(unreadMsgCount_);

                p.setBrush(Qt::NoBrush);
                p.drawText(r.translated(0, -0.5), Qt::AlignCenter, countTxt);
        }

        if (!isPressed_ && hasUnreadMessages_) {
                QPen pen;
                pen.setWidth(wm.unreadLineWidth);
                pen.setColor(highlightedBackgroundColor_);

                p.setPen(pen);
                p.drawLine(0, wm.unreadLineOffset, 0, height() - wm.unreadLineOffset);
        }
}

void
RoomInfoListItem::updateUnreadMessageCount(int count, int highlightedCount)
{
        unreadMsgCount_            = count;
        unreadHighlightedMsgCount_ = highlightedCount;
        update();
}

enum NotificationImportance : short
{
        ImportanceDisabled = -1,
        AllEventsRead      = 0,
        NewMessage         = 1,
        NewMentions        = 2,
        Invite             = 3
};

short int
RoomInfoListItem::calculateImportance() const
{
        // Returns the degree of importance of the unread messages in the room.
        // If sorting by importance is disabled in settings, this only ever
        // returns ImportanceDisabled or Invite
        if (isInvite()) {
                return Invite;
        } else if (!ChatPage::instance()->userSettings()->sortByImportance()) {
                return ImportanceDisabled;
        } else if (unreadHighlightedMsgCount_) {
                return NewMentions;
        } else if (unreadMsgCount_) {
                return NewMessage;
        } else {
                return AllEventsRead;
        }
}

void
RoomInfoListItem::setPressedState(bool state)
{
        if (isPressed_ != state) {
                isPressed_ = state;
                update();
        }
}

void
RoomInfoListItem::contextMenuEvent(QContextMenuEvent *event)
{
        Q_UNUSED(event);

        if (roomType_ == RoomType::Invited)
                return;

        menu_->popup(event->globalPos());
}

void
RoomInfoListItem::mousePressEvent(QMouseEvent *event)
{
        if (event->buttons() == Qt::RightButton) {
                QWidget::mousePressEvent(event);
                return;
        } else if (event->buttons() == Qt::LeftButton) {
                if (roomType_ == RoomType::Invited) {
                        const auto point = event->pos();

                        if (acceptBtnRegion_.contains(point))
                                emit acceptInvite(roomId_);

                        if (declineBtnRegion_.contains(point))
                                emit declineInvite(roomId_);

                        return;
                }

                emit clicked(roomId_);

                setPressedState(true);

                // Ripple on mouse position by default.
                QPoint pos           = event->pos();
                qreal radiusEndValue = static_cast<qreal>(width()) / 3;

                Ripple *ripple = new Ripple(pos);

                ripple->setRadiusEndValue(radiusEndValue);
                ripple->setOpacityStartValue(0.15);
                ripple->setColor(QColor("white"));
                ripple->radiusAnimation()->setDuration(200);
                ripple->opacityAnimation()->setDuration(400);

                ripple_overlay_->addRipple(ripple);
        }
}

void
RoomInfoListItem::setAvatar(const QString &avatar_url)
{
        if (avatar_url.isEmpty())
                avatar_->setFiller(roomName_);
        else
                avatar_->setImage(avatar_url);
}

void
RoomInfoListItem::setDescriptionMessage(const DescInfo &info)
{
        lastMsgInfo_ = info;
        update();
}
