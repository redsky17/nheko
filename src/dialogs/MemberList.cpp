#include <QAbstractSlider>
#include <QLabel>
#include <QListWidgetItem>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QStyleOption>
#include <QVBoxLayout>

#include "dialogs/MemberList.h"

#include "Cache.h"
#include "ChatPage.h"
#include "Config.h"
#include "Logging.h"
#include "Utils.h"
#include "ui/Avatar.h"

using namespace dialogs;

MemberItem::MemberItem(const RoomMember &member, QWidget *parent)
  : QWidget(parent)
{
        topLayout_ = new QHBoxLayout(this);
        topLayout_->setMargin(0);

        textLayout_ = new QVBoxLayout;
        textLayout_->setMargin(0);
        textLayout_->setSpacing(0);

        avatar_ = new Avatar(this, 44);

        avatar_->setFiller(member.user_id);
        avatar_->setImage(ChatPage::instance()->currentRoom(), member.user_id);

        QFont nameFont;
        nameFont.setPointSizeF(nameFont.pointSizeF() * 1.1);

        userId_   = new QLabel(member.user_id, this);
        userName_ = new QLabel(member.display_name, this);
        userName_->setFont(nameFont);

        textLayout_->addWidget(userName_);
        textLayout_->addWidget(userId_);

        topLayout_->addWidget(avatar_);
        topLayout_->addLayout(textLayout_, 1);
}

void
MemberItem::paintEvent(QPaintEvent *)
{
        QStyleOption opt;
        opt.init(this);
        QPainter p(this);
        style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

MemberList::MemberList(const QString &room_id, QWidget *parent)
  : QFrame(parent)
  , room_id_{room_id}
{
        setAutoFillBackground(true);
        setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint);
        setWindowModality(Qt::WindowModal);
        setAttribute(Qt::WA_DeleteOnClose, true);

        auto layout = new QVBoxLayout(this);
        layout->setSpacing(conf::modals::WIDGET_SPACING);
        layout->setMargin(conf::modals::WIDGET_MARGIN);

        list_ = new QListWidget;
        list_->setFrameStyle(QFrame::NoFrame);
        list_->setSelectionMode(QAbstractItemView::NoSelection);
        list_->setSpacing(5);

        QFont largeFont;
        largeFont.setPointSizeF(largeFont.pointSizeF() * 1.5);

        setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
        setMinimumHeight(list_->sizeHint().height() * 2);
        setMinimumWidth(std::max(list_->sizeHint().width() + 4 * conf::modals::WIDGET_MARGIN,
                                 QFontMetrics(largeFont).averageCharWidth() * 30 -
                                   2 * conf::modals::WIDGET_MARGIN));

        QFont font;
        font.setPointSizeF(font.pointSizeF() * conf::modals::LABEL_MEDIUM_SIZE_RATIO);

        topLabel_ = new QLabel(tr("Room members"), this);
        topLabel_->setAlignment(Qt::AlignCenter);
        topLabel_->setFont(font);

        auto okBtn = new QPushButton(tr("OK"), this);

        auto buttonLayout = new QHBoxLayout();
        buttonLayout->setSpacing(15);
        buttonLayout->addStretch(1);
        buttonLayout->addWidget(okBtn);

        layout->addWidget(topLabel_);
        layout->addWidget(list_);
        layout->addLayout(buttonLayout);

        list_->clear();

        connect(list_->verticalScrollBar(), &QAbstractSlider::valueChanged, this, [this](int pos) {
                if (pos != list_->verticalScrollBar()->maximum())
                        return;

                const size_t numMembers = list_->count() - 1;

                if (numMembers > 0)
                        addUsers(cache::getMembers(room_id_.toStdString(), numMembers));
        });

        try {
                addUsers(cache::getMembers(room_id_.toStdString()));
        } catch (const lmdb::error &e) {
                nhlog::db()->critical("Failed to retrieve members from cache: {}", e.what());
        }

        auto closeShortcut = new QShortcut(QKeySequence(QKeySequence::Cancel), this);
        connect(closeShortcut, &QShortcut::activated, this, &MemberList::close);
        connect(okBtn, &QPushButton::clicked, this, &MemberList::close);
}

void
MemberList::addUsers(const std::vector<RoomMember> &members)
{
        for (const auto &member : members) {
                auto user = new MemberItem(member, this);
                auto item = new QListWidgetItem;

                item->setSizeHint(user->minimumSizeHint());
                item->setFlags(Qt::NoItemFlags);
                item->setTextAlignment(Qt::AlignCenter);

                list_->insertItem(list_->count() - 1, item);
                list_->setItemWidget(item, user);
        }
}
