// SPDX-FileCopyrightText: 2021 Nheko Contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <vector>

#include "MatrixClient.h"
#include <mtxclient/http/errors.hpp>
#include <mtx/responses/public_rooms.hpp>

#include "Logging.h"

namespace mtx::http {
using RequestErr = const std::optional<mtx::http::ClientError> &;
}
namespace mtx::responses {
struct PublicRooms;
}

class RoomDirectoryModel : public QAbstractListModel
{
    Q_OBJECT

public: 
    explicit RoomDirectoryModel
    (QObject *parent = nullptr, const std::string &s = "");

    enum Roles {
        RoomName = Qt::UserRole, // does this matter? Should this be Qt::DisplayRole?
        RoomId,
        RoomAvatarUrl,
        RoomTopic,
        MemberCount,
        Previewable
    };
    virtual QHash<int, QByteArray> roleNames() const override;

    virtual QVariant data(const QModelIndex &index, int role) const override;

    virtual inline int rowCount([[maybe_unused]] const QModelIndex &parent = QModelIndex()) const override
    {
            return static_cast<int> (publicRoomsData.size());
    }

    virtual inline bool canFetchMore([[maybe_unused]] const QModelIndex &parent) const override
    {
        nhlog::net()->debug("determining if can fetch more");
        return canFetchMore_;
    }
    virtual void fetchMore([[maybe_unused]] const QModelIndex &parent) override;

signals:
    void fetchedRoomsBatchFromMtxClient(std::vector<mtx::responses::PublicRoomsChunk> rooms, 
                                        const std::string prev_batch, const std::string next_batch
                                        /*, std::optional<size_t> total_room_count_estimate*/);
    void serverChanged();
    void searchTermEntered();

public slots:
    void displayRooms(std::vector<mtx::responses::PublicRoomsChunk> rooms,
                      const std::string prev_batch, const std::string next_batch
                      /*, std::optional<size_t> total_room_count_estimate*/);
    void setMatrixServer(const QString &s = "");
    void setFilter(const QString &f);

private:
    using PublicRoomsChunk = mtx::responses::PublicRoomsChunk;
    static constexpr size_t limit_ = 50; // # rooms requested from mtxclient at once
    
    std::string server_;
    std::string filter_;
    std::string prev_batch_;
    std::string next_batch_;
    bool canFetchMore_;
    
    std::vector<PublicRoomsChunk> publicRoomsData;

    void printPublicRoomsData() {
        for (const auto &room: publicRoomsData) {
            nhlog::ui()->debug(room.name);
        }
    }

    // static constexpr std::vector<std::string> dummyData_ = {"Element1", "Eleme1", "EleMent4", "NewElement1", "Element77"};
};