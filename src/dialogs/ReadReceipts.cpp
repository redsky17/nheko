#include <QDebug>
#include <QIcon>
#include <QLabel>
#include <QListWidgetItem>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QStyleOption>
#include <QTimer>
#include <QVBoxLayout>

#include "dialogs/ReadReceipts.h"

#include "AvatarProvider.h"
#include "Cache.h"
#include "ChatPage.h"
#include "Config.h"
#include "Utils.h"
#include "ui/Avatar.h"

using namespace dialogs;

ReceiptItem::ReceiptItem(QWidget *parent,
                         const QString &user_id,
                         uint64_t timestamp,
                         const QString &room_id)
  : QWidget(parent)
{
        topLayout_ = new QHBoxLayout(this);
        topLayout_->setMargin(0);

        textLayout_ = new QVBoxLayout;
        textLayout_->setMargin(0);
        textLayout_->setSpacing(conf::modals::TEXT_SPACING);

        QFont nameFont;
        nameFont.setPointSizeF(nameFont.pointSizeF() * 1.1);

        auto displayName = cache::displayName(room_id, user_id);

        avatar_ = new Avatar(this, 44);
        avatar_->setFiller(displayName);

        userName_ = new QLabel(displayName, this);
        userName_->setFont(nameFont);

        timestamp_ = new QLabel(dateFormat(QDateTime::fromMSecsSinceEpoch(timestamp)), this);

        textLayout_->addWidget(userName_);
        textLayout_->addWidget(timestamp_);

        topLayout_->addWidget(avatar_);
        topLayout_->addLayout(textLayout_, 1);

        avatar_->setImage(ChatPage::instance()->currentRoom(), user_id);
}

void
ReceiptItem::paintEvent(QPaintEvent *)
{
        QStyleOption opt;
        opt.init(this);
        QPainter p(this);
        style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

QString
ReceiptItem::dateFormat(const QDateTime &then) const
{
        auto now  = QDateTime::currentDateTime();
        auto days = then.daysTo(now);

        if (days == 0)
                return tr("Today %1")
                  .arg(QLocale::system().toString(then.time(), QLocale::ShortFormat));
        else if (days < 2)
                return tr("Yesterday %1")
                  .arg(QLocale::system().toString(then.time(), QLocale::ShortFormat));
        else if (days < 7)
                return QString("%1 %2")
                  .arg(then.toString("dddd"))
                  .arg(QLocale::system().toString(then.time(), QLocale::ShortFormat));

        return QLocale::system().toString(then.time(), QLocale::ShortFormat);
}

ReadReceipts::ReadReceipts(QWidget *parent)
  : QFrame(parent)
{
        setAutoFillBackground(true);
        setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint);
        setWindowModality(Qt::WindowModal);
        setAttribute(Qt::WA_DeleteOnClose, true);

        auto layout = new QVBoxLayout(this);
        layout->setSpacing(conf::modals::WIDGET_SPACING);
        layout->setMargin(conf::modals::WIDGET_MARGIN);

        userList_ = new QListWidget;
        userList_->setFrameStyle(QFrame::NoFrame);
        userList_->setSelectionMode(QAbstractItemView::NoSelection);
        userList_->setSpacing(conf::modals::TEXT_SPACING);

        QFont largeFont;
        largeFont.setPointSizeF(largeFont.pointSizeF() * 1.5);

        setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
        setMinimumHeight(userList_->sizeHint().height() * 2);
        setMinimumWidth(std::max(userList_->sizeHint().width() + 4 * conf::modals::WIDGET_MARGIN,
                                 QFontMetrics(largeFont).averageCharWidth() * 30 -
                                   2 * conf::modals::WIDGET_MARGIN));

        QFont font;
        font.setPointSizeF(font.pointSizeF() * conf::modals::LABEL_MEDIUM_SIZE_RATIO);

        topLabel_ = new QLabel(tr("Read receipts"), this);
        topLabel_->setAlignment(Qt::AlignCenter);
        topLabel_->setFont(font);

        auto okBtn = new QPushButton(tr("Close"), this);

        auto buttonLayout = new QHBoxLayout();
        buttonLayout->setSpacing(15);
        buttonLayout->addStretch(1);
        buttonLayout->addWidget(okBtn);

        layout->addWidget(topLabel_);
        layout->addWidget(userList_);
        layout->addLayout(buttonLayout);

        auto closeShortcut = new QShortcut(QKeySequence(QKeySequence::Cancel), this);
        connect(closeShortcut, &QShortcut::activated, this, &ReadReceipts::close);
        connect(okBtn, &QPushButton::clicked, this, &ReadReceipts::close);
}

void
ReadReceipts::addUsers(const std::multimap<uint64_t, std::string, std::greater<uint64_t>> &receipts)
{
        // We want to remove any previous items that have been set.
        userList_->clear();

        for (const auto &receipt : receipts) {
                auto user = new ReceiptItem(this,
                                            QString::fromStdString(receipt.second),
                                            receipt.first,
                                            ChatPage::instance()->currentRoom());
                auto item = new QListWidgetItem(userList_);

                item->setSizeHint(user->minimumSizeHint());
                item->setFlags(Qt::NoItemFlags);
                item->setTextAlignment(Qt::AlignCenter);

                userList_->setItemWidget(item, user);
        }
}

void
ReadReceipts::paintEvent(QPaintEvent *)
{
        QStyleOption opt;
        opt.init(this);
        QPainter p(this);
        style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void
ReadReceipts::hideEvent(QHideEvent *event)
{
        userList_->clear();
        QFrame::hideEvent(event);
}
