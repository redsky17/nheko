// SPDX-FileCopyrightText: 2017 Konstantinos Sideris <siderisk@auth.gr>
// SPDX-FileCopyrightText: 2021 Nheko Contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <limits>
#include <stdexcept>
#include <variant>

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QStandardPaths>

#if __has_include(<keychain.h>)
#include <keychain.h>
#else
#include <qt5keychain/keychain.h>
#endif

#include <mtx/responses/common.hpp>

#include "Cache.h"
#include "Cache_p.h"
#include "ChatPage.h"
#include "EventAccessors.h"
#include "Logging.h"
#include "MatrixClient.h"
#include "Olm.h"
#include "UserSettingsPage.h"
#include "Utils.h"

//! Should be changed when a breaking change occurs in the cache format.
//! This will reset client's data.
static const std::string CURRENT_CACHE_FORMAT_VERSION("2020.10.20");
static const std::string SECRET("secret");

//! Keys used for the DB
static const std::string_view NEXT_BATCH_KEY("next_batch");
static const std::string_view OLM_ACCOUNT_KEY("olm_account");
static const std::string_view CACHE_FORMAT_VERSION_KEY("cache_format_version");

constexpr size_t MAX_RESTORED_MESSAGES = 30'000;

// Android builds don't like this to be constexpr
#ifndef Q_OS_ANDROID
constexpr
#endif
  auto DB_SIZE = 32ULL * 1024ULL * 1024ULL * 1024ULL; // 32 GB

constexpr auto MAX_DBS    = 32384UL;
constexpr auto BATCH_SIZE = 100;

//! Cache databases and their format.
//!
//! Contains UI information for the joined rooms. (i.e name, topic, avatar url etc).
//! Format: room_id -> RoomInfo
constexpr auto ROOMS_DB("rooms");
constexpr auto INVITES_DB("invites");
//! maps each room to its parent space (id->id)
constexpr auto SPACES_PARENTS_DB("space_parents");
//! maps each space to its current children (id->id)
constexpr auto SPACES_CHILDREN_DB("space_children");
//! Information that  must be kept between sync requests.
constexpr auto SYNC_STATE_DB("sync_state");
//! Read receipts per room/event.
constexpr auto READ_RECEIPTS_DB("read_receipts");
constexpr auto NOTIFICATIONS_DB("sent_notifications");

//! Encryption related databases.

//! user_id -> list of devices
constexpr auto DEVICES_DB("devices");
//! device_id -> device keys
constexpr auto DEVICE_KEYS_DB("device_keys");
//! room_ids that have encryption enabled.
constexpr auto ENCRYPTED_ROOMS_DB("encrypted_rooms");

//! room_id -> pickled OlmInboundGroupSession
constexpr auto INBOUND_MEGOLM_SESSIONS_DB("inbound_megolm_sessions");
//! MegolmSessionIndex -> pickled OlmOutboundGroupSession
constexpr auto OUTBOUND_MEGOLM_SESSIONS_DB("outbound_megolm_sessions");

using CachedReceipts = std::multimap<uint64_t, std::string, std::greater<uint64_t>>;
using Receipts       = std::map<std::string, std::map<std::string, uint64_t>>;

Q_DECLARE_METATYPE(RoomMember)
Q_DECLARE_METATYPE(mtx::responses::Timeline)
Q_DECLARE_METATYPE(RoomSearchResult)
Q_DECLARE_METATYPE(RoomInfo)
Q_DECLARE_METATYPE(mtx::responses::QueryKeys)

namespace {
std::unique_ptr<Cache> instance_ = nullptr;
}

struct RO_txn
{
        ~RO_txn() { txn.reset(); }
        operator MDB_txn *() const noexcept { return txn.handle(); }
        operator lmdb::txn &() noexcept { return txn; }

        lmdb::txn &txn;
};

RO_txn
ro_txn(lmdb::env &env)
{
        thread_local lmdb::txn txn     = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
        thread_local int reuse_counter = 0;

        if (reuse_counter >= 100 || txn.env() != env.handle()) {
                txn.abort();
                txn           = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
                reuse_counter = 0;
        } else if (reuse_counter > 0) {
                txn.renew();
        }
        reuse_counter++;

        return RO_txn{txn};
}

template<class T>
bool
containsStateUpdates(const T &e)
{
        return std::visit([](const auto &ev) { return Cache::isStateEvent(ev); }, e);
}

bool
containsStateUpdates(const mtx::events::collections::StrippedEvents &e)
{
        using namespace mtx::events;
        using namespace mtx::events::state;

        return std::holds_alternative<StrippedEvent<state::Avatar>>(e) ||
               std::holds_alternative<StrippedEvent<CanonicalAlias>>(e) ||
               std::holds_alternative<StrippedEvent<Name>>(e) ||
               std::holds_alternative<StrippedEvent<Member>>(e) ||
               std::holds_alternative<StrippedEvent<Topic>>(e);
}

bool
Cache::isHiddenEvent(lmdb::txn &txn,
                     mtx::events::collections::TimelineEvents e,
                     const std::string &room_id)
{
        using namespace mtx::events;

        // Always hide edits
        if (mtx::accessors::relations(e).replaces())
                return true;

        if (auto encryptedEvent = std::get_if<EncryptedEvent<msg::Encrypted>>(&e)) {
                MegolmSessionIndex index;
                index.room_id    = room_id;
                index.session_id = encryptedEvent->content.session_id;
                index.sender_key = encryptedEvent->content.sender_key;

                auto result = olm::decryptEvent(index, *encryptedEvent);
                if (!result.error)
                        e = result.event.value();
        }

        mtx::events::account_data::nheko_extensions::HiddenEvents hiddenEvents;
        hiddenEvents.hidden_event_types = {
          EventType::Reaction, EventType::CallCandidates, EventType::Unsupported};

        if (auto temp = getAccountData(txn, mtx::events::EventType::NhekoHiddenEvents, ""))
                hiddenEvents =
                  std::move(std::get<mtx::events::AccountDataEvent<
                              mtx::events::account_data::nheko_extensions::HiddenEvents>>(*temp)
                              .content);
        if (auto temp = getAccountData(txn, mtx::events::EventType::NhekoHiddenEvents, room_id))
                hiddenEvents =
                  std::move(std::get<mtx::events::AccountDataEvent<
                              mtx::events::account_data::nheko_extensions::HiddenEvents>>(*temp)
                              .content);

        return std::visit(
          [hiddenEvents](const auto &ev) {
                  return std::any_of(hiddenEvents.hidden_event_types.begin(),
                                     hiddenEvents.hidden_event_types.end(),
                                     [ev](EventType type) { return type == ev.type; });
          },
          e);
}

Cache::Cache(const QString &userId, QObject *parent)
  : QObject{parent}
  , env_{nullptr}
  , localUserId_{userId}
{
        setup();
        connect(this, &Cache::userKeysUpdate, this, &Cache::updateUserKeys, Qt::QueuedConnection);
}

void
Cache::setup()
{
        auto settings = UserSettings::instance();

        nhlog::db()->debug("setting up cache");

        // Previous location of the cache directory
        auto oldCache = QString("%1/%2%3")
                          .arg(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                          .arg(QString::fromUtf8(localUserId_.toUtf8().toHex()))
                          .arg(QString::fromUtf8(settings->profile().toUtf8().toHex()));

        cacheDirectory_ = QString("%1/%2%3")
                            .arg(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                            .arg(QString::fromUtf8(localUserId_.toUtf8().toHex()))
                            .arg(QString::fromUtf8(settings->profile().toUtf8().toHex()));

        bool isInitial = !QFile::exists(cacheDirectory_);

        // NOTE: If both cache directories exist it's better to do nothing: it
        // could mean a previous migration failed or was interrupted.
        bool needsMigration = isInitial && QFile::exists(oldCache);

        if (needsMigration) {
                nhlog::db()->info("found old state directory, migrating");
                if (!QDir().rename(oldCache, cacheDirectory_)) {
                        throw std::runtime_error(("Unable to migrate the old state directory (" +
                                                  oldCache + ") to the new location (" +
                                                  cacheDirectory_ + ")")
                                                   .toStdString()
                                                   .c_str());
                }
                nhlog::db()->info("completed state migration");
        }

        env_ = lmdb::env::create();
        env_.set_mapsize(DB_SIZE);
        env_.set_max_dbs(MAX_DBS);

        if (isInitial) {
                nhlog::db()->info("initializing LMDB");

                if (!QDir().mkpath(cacheDirectory_)) {
                        throw std::runtime_error(
                          ("Unable to create state directory:" + cacheDirectory_)
                            .toStdString()
                            .c_str());
                }
        }

        try {
                // NOTE(Nico): We may want to use (MDB_MAPASYNC | MDB_WRITEMAP) in the future, but
                // it can really mess up our database, so we shouldn't. For now, hopefully
                // NOMETASYNC is fast enough.
                env_.open(cacheDirectory_.toStdString().c_str(), MDB_NOMETASYNC | MDB_NOSYNC);
        } catch (const lmdb::error &e) {
                if (e.code() != MDB_VERSION_MISMATCH && e.code() != MDB_INVALID) {
                        throw std::runtime_error("LMDB initialization failed" +
                                                 std::string(e.what()));
                }

                nhlog::db()->warn("resetting cache due to LMDB version mismatch: {}", e.what());

                QDir stateDir(cacheDirectory_);

                for (const auto &file : stateDir.entryList(QDir::NoDotAndDotDot)) {
                        if (!stateDir.remove(file))
                                throw std::runtime_error(
                                  ("Unable to delete file " + file).toStdString().c_str());
                }
                env_.open(cacheDirectory_.toStdString().c_str());
        }

        auto txn          = lmdb::txn::begin(env_);
        syncStateDb_      = lmdb::dbi::open(txn, SYNC_STATE_DB, MDB_CREATE);
        roomsDb_          = lmdb::dbi::open(txn, ROOMS_DB, MDB_CREATE);
        spacesChildrenDb_ = lmdb::dbi::open(txn, SPACES_CHILDREN_DB, MDB_CREATE | MDB_DUPSORT);
        spacesParentsDb_  = lmdb::dbi::open(txn, SPACES_PARENTS_DB, MDB_CREATE | MDB_DUPSORT);
        invitesDb_        = lmdb::dbi::open(txn, INVITES_DB, MDB_CREATE);
        readReceiptsDb_   = lmdb::dbi::open(txn, READ_RECEIPTS_DB, MDB_CREATE);
        notificationsDb_  = lmdb::dbi::open(txn, NOTIFICATIONS_DB, MDB_CREATE);

        // Device management
        devicesDb_    = lmdb::dbi::open(txn, DEVICES_DB, MDB_CREATE);
        deviceKeysDb_ = lmdb::dbi::open(txn, DEVICE_KEYS_DB, MDB_CREATE);

        // Session management
        inboundMegolmSessionDb_  = lmdb::dbi::open(txn, INBOUND_MEGOLM_SESSIONS_DB, MDB_CREATE);
        outboundMegolmSessionDb_ = lmdb::dbi::open(txn, OUTBOUND_MEGOLM_SESSIONS_DB, MDB_CREATE);

        txn.commit();

        databaseReady_ = true;
}

void
Cache::setEncryptedRoom(lmdb::txn &txn, const std::string &room_id)
{
        nhlog::db()->info("mark room {} as encrypted", room_id);

        auto db = lmdb::dbi::open(txn, ENCRYPTED_ROOMS_DB, MDB_CREATE);
        db.put(txn, room_id, "0");
}

bool
Cache::isRoomEncrypted(const std::string &room_id)
{
        std::string_view unused;

        auto txn = ro_txn(env_);
        auto db  = lmdb::dbi::open(txn, ENCRYPTED_ROOMS_DB, MDB_CREATE);
        auto res = db.get(txn, room_id, unused);

        return res;
}

std::optional<mtx::events::state::Encryption>
Cache::roomEncryptionSettings(const std::string &room_id)
{
        using namespace mtx::events;
        using namespace mtx::events::state;

        try {
                auto txn      = ro_txn(env_);
                auto statesdb = getStatesDb(txn, room_id);
                std::string_view event;
                bool res =
                  statesdb.get(txn, to_string(mtx::events::EventType::RoomEncryption), event);

                if (res) {
                        try {
                                StateEvent<Encryption> msg = json::parse(event);

                                return msg.content;
                        } catch (const json::exception &e) {
                                nhlog::db()->warn("failed to parse m.room.encryption event: {}",
                                                  e.what());
                                return Encryption{};
                        }
                }
        } catch (lmdb::error &) {
        }

        return std::nullopt;
}

mtx::crypto::ExportedSessionKeys
Cache::exportSessionKeys()
{
        using namespace mtx::crypto;

        ExportedSessionKeys keys;

        auto txn    = ro_txn(env_);
        auto cursor = lmdb::cursor::open(txn, inboundMegolmSessionDb_);

        std::string_view key, value;
        while (cursor.get(key, value, MDB_NEXT)) {
                ExportedSession exported;
                MegolmSessionIndex index;

                auto saved_session = unpickle<InboundSessionObject>(std::string(value), SECRET);

                try {
                        index = nlohmann::json::parse(key).get<MegolmSessionIndex>();
                } catch (const nlohmann::json::exception &e) {
                        nhlog::db()->critical("failed to export megolm session: {}", e.what());
                        continue;
                }

                exported.room_id     = index.room_id;
                exported.sender_key  = index.sender_key;
                exported.session_id  = index.session_id;
                exported.session_key = export_session(saved_session.get(), -1);

                keys.sessions.push_back(exported);
        }

        cursor.close();

        return keys;
}

void
Cache::importSessionKeys(const mtx::crypto::ExportedSessionKeys &keys)
{
        for (const auto &s : keys.sessions) {
                MegolmSessionIndex index;
                index.room_id    = s.room_id;
                index.session_id = s.session_id;
                index.sender_key = s.sender_key;

                auto exported_session = mtx::crypto::import_session(s.session_key);

                saveInboundMegolmSession(index, std::move(exported_session));
                ChatPage::instance()->receivedSessionKey(index.room_id, index.session_id);
        }
}

//
// Session Management
//

void
Cache::saveInboundMegolmSession(const MegolmSessionIndex &index,
                                mtx::crypto::InboundGroupSessionPtr session)
{
        using namespace mtx::crypto;
        const auto key     = json(index).dump();
        const auto pickled = pickle<InboundSessionObject>(session.get(), SECRET);

        auto txn = lmdb::txn::begin(env_);

        std::string_view value;
        if (inboundMegolmSessionDb_.get(txn, key, value)) {
                auto oldSession = unpickle<InboundSessionObject>(std::string(value), SECRET);
                if (olm_inbound_group_session_first_known_index(session.get()) >
                    olm_inbound_group_session_first_known_index(oldSession.get())) {
                        nhlog::crypto()->warn(
                          "Not storing inbound session with newer first known index");
                        return;
                }
        }

        inboundMegolmSessionDb_.put(txn, key, pickled);
        txn.commit();
}

mtx::crypto::InboundGroupSessionPtr
Cache::getInboundMegolmSession(const MegolmSessionIndex &index)
{
        using namespace mtx::crypto;

        try {
                auto txn        = ro_txn(env_);
                std::string key = json(index).dump();
                std::string_view value;

                if (inboundMegolmSessionDb_.get(txn, key, value)) {
                        auto session = unpickle<InboundSessionObject>(std::string(value), SECRET);
                        return session;
                }
        } catch (std::exception &e) {
                nhlog::db()->error("Failed to get inbound megolm session {}", e.what());
        }

        return nullptr;
}

bool
Cache::inboundMegolmSessionExists(const MegolmSessionIndex &index)
{
        using namespace mtx::crypto;

        try {
                auto txn        = ro_txn(env_);
                std::string key = json(index).dump();
                std::string_view value;

                return inboundMegolmSessionDb_.get(txn, key, value);
        } catch (std::exception &e) {
                nhlog::db()->error("Failed to get inbound megolm session {}", e.what());
        }

        return false;
}

void
Cache::updateOutboundMegolmSession(const std::string &room_id,
                                   const OutboundGroupSessionData &data_,
                                   mtx::crypto::OutboundGroupSessionPtr &ptr)
{
        using namespace mtx::crypto;

        if (!outboundMegolmSessionExists(room_id))
                return;

        OutboundGroupSessionData data = data_;
        data.message_index            = olm_outbound_group_session_message_index(ptr.get());
        data.session_id               = mtx::crypto::session_id(ptr.get());
        data.session_key              = mtx::crypto::session_key(ptr.get());

        // Save the updated pickled data for the session.
        json j;
        j["data"]    = data;
        j["session"] = pickle<OutboundSessionObject>(ptr.get(), SECRET);

        auto txn = lmdb::txn::begin(env_);
        outboundMegolmSessionDb_.put(txn, room_id, j.dump());
        txn.commit();
}

void
Cache::dropOutboundMegolmSession(const std::string &room_id)
{
        using namespace mtx::crypto;

        if (!outboundMegolmSessionExists(room_id))
                return;

        {
                auto txn = lmdb::txn::begin(env_);
                outboundMegolmSessionDb_.del(txn, room_id);
                txn.commit();
        }
}

void
Cache::saveOutboundMegolmSession(const std::string &room_id,
                                 const OutboundGroupSessionData &data,
                                 mtx::crypto::OutboundGroupSessionPtr &session)
{
        using namespace mtx::crypto;
        const auto pickled = pickle<OutboundSessionObject>(session.get(), SECRET);

        json j;
        j["data"]    = data;
        j["session"] = pickled;

        auto txn = lmdb::txn::begin(env_);
        outboundMegolmSessionDb_.put(txn, room_id, j.dump());
        txn.commit();
}

bool
Cache::outboundMegolmSessionExists(const std::string &room_id) noexcept
{
        try {
                auto txn = ro_txn(env_);
                std::string_view value;
                return outboundMegolmSessionDb_.get(txn, room_id, value);
        } catch (std::exception &e) {
                nhlog::db()->error("Failed to retrieve outbound Megolm Session: {}", e.what());
                return false;
        }
}

OutboundGroupSessionDataRef
Cache::getOutboundMegolmSession(const std::string &room_id)
{
        try {
                using namespace mtx::crypto;

                auto txn = ro_txn(env_);
                std::string_view value;
                outboundMegolmSessionDb_.get(txn, room_id, value);
                auto obj = json::parse(value);

                OutboundGroupSessionDataRef ref{};
                ref.data    = obj.at("data").get<OutboundGroupSessionData>();
                ref.session = unpickle<OutboundSessionObject>(obj.at("session"), SECRET);
                return ref;
        } catch (std::exception &e) {
                nhlog::db()->error("Failed to retrieve outbound Megolm Session: {}", e.what());
                return {};
        }
}

//
// OLM sessions.
//

void
Cache::saveOlmSession(const std::string &curve25519,
                      mtx::crypto::OlmSessionPtr session,
                      uint64_t timestamp)
{
        using namespace mtx::crypto;

        auto txn = lmdb::txn::begin(env_);
        auto db  = getOlmSessionsDb(txn, curve25519);

        const auto pickled    = pickle<SessionObject>(session.get(), SECRET);
        const auto session_id = mtx::crypto::session_id(session.get());

        StoredOlmSession stored_session;
        stored_session.pickled_session = pickled;
        stored_session.last_message_ts = timestamp;

        db.put(txn, session_id, json(stored_session).dump());

        txn.commit();
}

std::optional<mtx::crypto::OlmSessionPtr>
Cache::getOlmSession(const std::string &curve25519, const std::string &session_id)
{
        using namespace mtx::crypto;

        auto txn = lmdb::txn::begin(env_);
        auto db  = getOlmSessionsDb(txn, curve25519);

        std::string_view pickled;
        bool found = db.get(txn, session_id, pickled);

        txn.commit();

        if (found) {
                auto data = json::parse(pickled).get<StoredOlmSession>();
                return unpickle<SessionObject>(data.pickled_session, SECRET);
        }

        return std::nullopt;
}

std::optional<mtx::crypto::OlmSessionPtr>
Cache::getLatestOlmSession(const std::string &curve25519)
{
        using namespace mtx::crypto;

        auto txn = lmdb::txn::begin(env_);
        auto db  = getOlmSessionsDb(txn, curve25519);

        std::string_view session_id, pickled_session;

        std::optional<StoredOlmSession> currentNewest;

        auto cursor = lmdb::cursor::open(txn, db);
        while (cursor.get(session_id, pickled_session, MDB_NEXT)) {
                auto data = json::parse(pickled_session).get<StoredOlmSession>();
                if (!currentNewest || currentNewest->last_message_ts < data.last_message_ts)
                        currentNewest = data;
        }
        cursor.close();

        txn.commit();

        return currentNewest
                 ? std::optional(unpickle<SessionObject>(currentNewest->pickled_session, SECRET))
                 : std::nullopt;
}

std::vector<std::string>
Cache::getOlmSessions(const std::string &curve25519)
{
        using namespace mtx::crypto;

        auto txn = lmdb::txn::begin(env_);
        auto db  = getOlmSessionsDb(txn, curve25519);

        std::string_view session_id, unused;
        std::vector<std::string> res;

        auto cursor = lmdb::cursor::open(txn, db);
        while (cursor.get(session_id, unused, MDB_NEXT))
                res.emplace_back(session_id);
        cursor.close();

        txn.commit();

        return res;
}

void
Cache::saveOlmAccount(const std::string &data)
{
        auto txn = lmdb::txn::begin(env_);
        syncStateDb_.put(txn, OLM_ACCOUNT_KEY, data);
        txn.commit();
}

std::string
Cache::restoreOlmAccount()
{
        auto txn = ro_txn(env_);
        std::string_view pickled;
        syncStateDb_.get(txn, OLM_ACCOUNT_KEY, pickled);

        return std::string(pickled.data(), pickled.size());
}

void
Cache::storeSecret(const std::string &name, const std::string &secret)
{
        auto settings = UserSettings::instance();
        QKeychain::WritePasswordJob job(QCoreApplication::applicationName());
        job.setAutoDelete(false);
        job.setInsecureFallback(true);
        job.setKey("matrix." +
                   QString(QCryptographicHash::hash(settings->profile().toUtf8(),
                                                    QCryptographicHash::Sha256)) +
                   "." + name.c_str());
        job.setTextData(QString::fromStdString(secret));
        QEventLoop loop;
        job.connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
        job.start();
        loop.exec();

        if (job.error()) {
                nhlog::db()->warn(
                  "Storing secret '{}' failed: {}", name, job.errorString().toStdString());
        } else {
                emit secretChanged(name);
        }
}

void
Cache::deleteSecret(const std::string &name)
{
        auto settings = UserSettings::instance();
        QKeychain::DeletePasswordJob job(QCoreApplication::applicationName());
        job.setAutoDelete(false);
        job.setInsecureFallback(true);
        job.setKey("matrix." +
                   QString(QCryptographicHash::hash(settings->profile().toUtf8(),
                                                    QCryptographicHash::Sha256)) +
                   "." + name.c_str());
        QEventLoop loop;
        job.connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
        job.start();
        loop.exec();

        emit secretChanged(name);
}

std::optional<std::string>
Cache::secret(const std::string &name)
{
        auto settings = UserSettings::instance();
        QKeychain::ReadPasswordJob job(QCoreApplication::applicationName());
        job.setAutoDelete(false);
        job.setInsecureFallback(true);
        job.setKey("matrix." +
                   QString(QCryptographicHash::hash(settings->profile().toUtf8(),
                                                    QCryptographicHash::Sha256)) +
                   "." + name.c_str());
        QEventLoop loop;
        job.connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
        job.start();
        loop.exec();

        const QString secret = job.textData();
        if (job.error()) {
                nhlog::db()->debug(
                  "Restoring secret '{}' failed: {}", name, job.errorString().toStdString());
                return std::nullopt;
        }
        if (secret.isEmpty()) {
                nhlog::db()->debug("Restored empty secret '{}'.", name);
                return std::nullopt;
        }

        return secret.toStdString();
}

void
Cache::removeInvite(lmdb::txn &txn, const std::string &room_id)
{
        invitesDb_.del(txn, room_id);
        getInviteStatesDb(txn, room_id).drop(txn, true);
        getInviteMembersDb(txn, room_id).drop(txn, true);
}

void
Cache::removeInvite(const std::string &room_id)
{
        auto txn = lmdb::txn::begin(env_);
        removeInvite(txn, room_id);
        txn.commit();
}

void
Cache::removeRoom(lmdb::txn &txn, const std::string &roomid)
{
        roomsDb_.del(txn, roomid);
        getStatesDb(txn, roomid).drop(txn, true);
        getAccountDataDb(txn, roomid).drop(txn, true);
        getMembersDb(txn, roomid).drop(txn, true);
}

void
Cache::removeRoom(const std::string &roomid)
{
        auto txn = lmdb::txn::begin(env_, nullptr, 0);
        roomsDb_.del(txn, roomid);
        txn.commit();
}

void
Cache::setNextBatchToken(lmdb::txn &txn, const std::string &token)
{
        syncStateDb_.put(txn, NEXT_BATCH_KEY, token);
}

void
Cache::setNextBatchToken(lmdb::txn &txn, const QString &token)
{
        setNextBatchToken(txn, token.toStdString());
}

bool
Cache::isInitialized()
{
        auto txn = ro_txn(env_);
        std::string_view token;

        bool res = syncStateDb_.get(txn, NEXT_BATCH_KEY, token);

        return res;
}

std::string
Cache::nextBatchToken()
{
        if (!env_.handle())
                throw lmdb::error("Env already closed", MDB_INVALID);

        auto txn = ro_txn(env_);
        std::string_view token;

        bool result = syncStateDb_.get(txn, NEXT_BATCH_KEY, token);

        if (result)
                return std::string(token.data(), token.size());
        else
                return "";
}

void
Cache::deleteData()
{
        this->databaseReady_ = false;
        // TODO: We need to remove the env_ while not accepting new requests.
        lmdb::dbi_close(env_, syncStateDb_);
        lmdb::dbi_close(env_, roomsDb_);
        lmdb::dbi_close(env_, invitesDb_);
        lmdb::dbi_close(env_, readReceiptsDb_);
        lmdb::dbi_close(env_, notificationsDb_);

        lmdb::dbi_close(env_, devicesDb_);
        lmdb::dbi_close(env_, deviceKeysDb_);

        lmdb::dbi_close(env_, inboundMegolmSessionDb_);
        lmdb::dbi_close(env_, outboundMegolmSessionDb_);

        env_.close();

        verification_storage.status.clear();

        if (!cacheDirectory_.isEmpty()) {
                QDir(cacheDirectory_).removeRecursively();
                nhlog::db()->info("deleted cache files from disk");
        }

        deleteSecret(mtx::secret_storage::secrets::megolm_backup_v1);
        deleteSecret(mtx::secret_storage::secrets::cross_signing_master);
        deleteSecret(mtx::secret_storage::secrets::cross_signing_user_signing);
        deleteSecret(mtx::secret_storage::secrets::cross_signing_self_signing);
}

//! migrates db to the current format
bool
Cache::runMigrations()
{
        std::string stored_version;
        {
                auto txn = ro_txn(env_);

                std::string_view current_version;
                bool res = syncStateDb_.get(txn, CACHE_FORMAT_VERSION_KEY, current_version);

                if (!res)
                        return false;

                stored_version = std::string(current_version);
        }

        std::vector<std::pair<std::string, std::function<bool()>>> migrations{
          {"2020.05.01",
           [this]() {
                   try {
                           auto txn = lmdb::txn::begin(env_, nullptr);
                           auto pending_receipts =
                             lmdb::dbi::open(txn, "pending_receipts", MDB_CREATE);
                           lmdb::dbi_drop(txn, pending_receipts, true);
                           txn.commit();
                   } catch (const lmdb::error &) {
                           nhlog::db()->critical(
                             "Failed to delete pending_receipts database in migration!");
                           return false;
                   }

                   nhlog::db()->info("Successfully deleted pending receipts database.");
                   return true;
           }},
          {"2020.07.05",
           [this]() {
                   try {
                           auto txn      = lmdb::txn::begin(env_, nullptr);
                           auto room_ids = getRoomIds(txn);

                           for (const auto &room_id : room_ids) {
                                   try {
                                           auto messagesDb = lmdb::dbi::open(
                                             txn, std::string(room_id + "/messages").c_str());

                                           // keep some old messages and batch token
                                           {
                                                   auto roomsCursor =
                                                     lmdb::cursor::open(txn, messagesDb);
                                                   std::string_view ts, stored_message;
                                                   bool start = true;
                                                   mtx::responses::Timeline oldMessages;
                                                   while (roomsCursor.get(ts,
                                                                          stored_message,
                                                                          start ? MDB_FIRST
                                                                                : MDB_NEXT)) {
                                                           start = false;

                                                           auto j = json::parse(std::string_view(
                                                             stored_message.data(),
                                                             stored_message.size()));

                                                           if (oldMessages.prev_batch.empty())
                                                                   oldMessages.prev_batch =
                                                                     j["token"].get<std::string>();
                                                           else if (j["token"] !=
                                                                    oldMessages.prev_batch)
                                                                   break;

                                                           mtx::events::collections::TimelineEvent
                                                             te;
                                                           mtx::events::collections::from_json(
                                                             j["event"], te);
                                                           oldMessages.events.push_back(te.data);
                                                   }
                                                   // messages were stored in reverse order, so we
                                                   // need to reverse them
                                                   std::reverse(oldMessages.events.begin(),
                                                                oldMessages.events.end());
                                                   // save messages using the new method
                                                   auto eventsDb = getEventsDb(txn, room_id);
                                                   saveTimelineMessages(
                                                     txn, eventsDb, room_id, oldMessages);
                                           }

                                           // delete old messages db
                                           lmdb::dbi_drop(txn, messagesDb, true);
                                   } catch (std::exception &e) {
                                           nhlog::db()->error(
                                             "While migrating messages from {}, ignoring error {}",
                                             room_id,
                                             e.what());
                                   }
                           }
                           txn.commit();
                   } catch (const lmdb::error &) {
                           nhlog::db()->critical(
                             "Failed to delete messages database in migration!");
                           return false;
                   }

                   nhlog::db()->info("Successfully deleted pending receipts database.");
                   return true;
           }},
          {"2020.10.20",
           [this]() {
                   try {
                           using namespace mtx::crypto;

                           auto txn = lmdb::txn::begin(env_);

                           auto mainDb = lmdb::dbi::open(txn, nullptr);

                           std::string_view dbName, ignored;
                           auto olmDbCursor = lmdb::cursor::open(txn, mainDb);
                           while (olmDbCursor.get(dbName, ignored, MDB_NEXT)) {
                                   // skip every db but olm session dbs
                                   nhlog::db()->debug("Db {}", dbName);
                                   if (dbName.find("olm_sessions/") != 0)
                                           continue;

                                   nhlog::db()->debug("Migrating {}", dbName);

                                   auto olmDb = lmdb::dbi::open(txn, std::string(dbName).c_str());

                                   std::string_view session_id, session_value;

                                   std::vector<std::pair<std::string, StoredOlmSession>> sessions;

                                   auto cursor = lmdb::cursor::open(txn, olmDb);
                                   while (cursor.get(session_id, session_value, MDB_NEXT)) {
                                           nhlog::db()->debug("session_id {}, session_value {}",
                                                              session_id,
                                                              session_value);
                                           StoredOlmSession session;
                                           bool invalid = false;
                                           for (auto c : session_value)
                                                   if (!isprint(c)) {
                                                           invalid = true;
                                                           break;
                                                   }
                                           if (invalid)
                                                   continue;

                                           nhlog::db()->debug("Not skipped");

                                           session.pickled_session = session_value;
                                           sessions.emplace_back(session_id, session);
                                   }
                                   cursor.close();

                                   olmDb.drop(txn, true);

                                   auto newDbName = std::string(dbName);
                                   newDbName.erase(0, sizeof("olm_sessions") - 1);
                                   newDbName = "olm_sessions.v2" + newDbName;

                                   auto newDb = lmdb::dbi::open(txn, newDbName.c_str(), MDB_CREATE);

                                   for (const auto &[key, value] : sessions) {
                                           // nhlog::db()->debug("{}\n{}", key, json(value).dump());
                                           newDb.put(txn, key, json(value).dump());
                                   }
                           }
                           olmDbCursor.close();

                           txn.commit();
                   } catch (const lmdb::error &) {
                           nhlog::db()->critical("Failed to migrate olm sessions,");
                           return false;
                   }

                   nhlog::db()->info("Successfully migrated olm sessions.");
                   return true;
           }},
        };

        nhlog::db()->info("Running migrations, this may take a while!");
        for (const auto &[target_version, migration] : migrations) {
                if (target_version > stored_version)
                        if (!migration()) {
                                nhlog::db()->critical("migration failure!");
                                return false;
                        }
        }
        nhlog::db()->info("Migrations finished.");

        setCurrentFormat();
        return true;
}

cache::CacheVersion
Cache::formatVersion()
{
        auto txn = ro_txn(env_);

        std::string_view current_version;
        bool res = syncStateDb_.get(txn, CACHE_FORMAT_VERSION_KEY, current_version);

        if (!res)
                return cache::CacheVersion::Older;

        std::string stored_version(current_version.data(), current_version.size());

        if (stored_version < CURRENT_CACHE_FORMAT_VERSION)
                return cache::CacheVersion::Older;
        else if (stored_version > CURRENT_CACHE_FORMAT_VERSION)
                return cache::CacheVersion::Older;
        else
                return cache::CacheVersion::Current;
}

void
Cache::setCurrentFormat()
{
        auto txn = lmdb::txn::begin(env_);

        syncStateDb_.put(txn, CACHE_FORMAT_VERSION_KEY, CURRENT_CACHE_FORMAT_VERSION);

        txn.commit();
}

CachedReceipts
Cache::readReceipts(const QString &event_id, const QString &room_id)
{
        CachedReceipts receipts;

        ReadReceiptKey receipt_key{event_id.toStdString(), room_id.toStdString()};
        nlohmann::json json_key = receipt_key;

        try {
                auto txn = ro_txn(env_);
                auto key = json_key.dump();

                std::string_view value;

                bool res = readReceiptsDb_.get(txn, key, value);

                if (res) {
                        auto json_response =
                          json::parse(std::string_view(value.data(), value.size()));
                        auto values = json_response.get<std::map<std::string, uint64_t>>();

                        for (const auto &v : values)
                                // timestamp, user_id
                                receipts.emplace(v.second, v.first);
                }

        } catch (const lmdb::error &e) {
                nhlog::db()->critical("readReceipts: {}", e.what());
        }

        return receipts;
}

void
Cache::updateReadReceipt(lmdb::txn &txn, const std::string &room_id, const Receipts &receipts)
{
        auto user_id = this->localUserId_.toStdString();
        for (const auto &receipt : receipts) {
                const auto event_id = receipt.first;
                auto event_receipts = receipt.second;

                ReadReceiptKey receipt_key{event_id, room_id};
                nlohmann::json json_key = receipt_key;

                try {
                        const auto key = json_key.dump();

                        std::string_view prev_value;

                        bool exists = readReceiptsDb_.get(txn, key, prev_value);

                        std::map<std::string, uint64_t> saved_receipts;

                        // If an entry for the event id already exists, we would
                        // merge the existing receipts with the new ones.
                        if (exists) {
                                auto json_value = json::parse(
                                  std::string_view(prev_value.data(), prev_value.size()));

                                // Retrieve the saved receipts.
                                saved_receipts = json_value.get<std::map<std::string, uint64_t>>();
                        }

                        // Append the new ones.
                        for (const auto &[read_by, timestamp] : event_receipts) {
                                if (read_by == user_id) {
                                        emit removeNotification(QString::fromStdString(room_id),
                                                                QString::fromStdString(event_id));
                                }
                                saved_receipts.emplace(read_by, timestamp);
                        }

                        // Save back the merged (or only the new) receipts.
                        nlohmann::json json_updated_value = saved_receipts;
                        std::string merged_receipts       = json_updated_value.dump();

                        readReceiptsDb_.put(txn, key, merged_receipts);

                } catch (const lmdb::error &e) {
                        nhlog::db()->critical("updateReadReceipts: {}", e.what());
                }
        }
}

void
Cache::calculateRoomReadStatus()
{
        const auto joined_rooms = joinedRooms();

        std::map<QString, bool> readStatus;

        for (const auto &room : joined_rooms)
                readStatus.emplace(QString::fromStdString(room), calculateRoomReadStatus(room));

        emit roomReadStatus(readStatus);
}

bool
Cache::calculateRoomReadStatus(const std::string &room_id)
{
        std::string last_event_id_, fullyReadEventId_;
        {
                auto txn = ro_txn(env_);

                // Get last event id on the room.
                const auto last_event_id = getLastEventId(txn, room_id);
                const auto localUser     = utils::localUser().toStdString();

                std::string fullyReadEventId;
                if (auto ev = getAccountData(txn, mtx::events::EventType::FullyRead, room_id)) {
                        if (auto fr = std::get_if<
                              mtx::events::AccountDataEvent<mtx::events::account_data::FullyRead>>(
                              &ev.value())) {
                                fullyReadEventId = fr->content.event_id;
                        }
                }

                if (last_event_id.empty() || fullyReadEventId.empty())
                        return true;

                if (last_event_id == fullyReadEventId)
                        return false;

                last_event_id_    = std::string(last_event_id);
                fullyReadEventId_ = std::string(fullyReadEventId);
        }

        // Retrieve all read receipts for that event.
        return getEventIndex(room_id, last_event_id_) > getEventIndex(room_id, fullyReadEventId_);
}

void
Cache::saveState(const mtx::responses::Sync &res)
{
        using namespace mtx::events;
        auto local_user_id = this->localUserId_.toStdString();

        auto currentBatchToken = nextBatchToken();

        auto txn = lmdb::txn::begin(env_);

        setNextBatchToken(txn, res.next_batch);

        if (!res.account_data.events.empty()) {
                auto accountDataDb = getAccountDataDb(txn, "");
                for (const auto &ev : res.account_data.events)
                        std::visit(
                          [&txn, &accountDataDb](const auto &event) {
                                  auto j = json(event);
                                  accountDataDb.put(txn, j["type"].get<std::string>(), j.dump());
                          },
                          ev);
        }

        auto userKeyCacheDb = getUserKeysDb(txn);

        std::set<std::string> spaces_with_updates;
        std::set<std::string> rooms_with_space_updates;

        // Save joined rooms
        for (const auto &room : res.rooms.join) {
                auto statesdb    = getStatesDb(txn, room.first);
                auto stateskeydb = getStatesKeyDb(txn, room.first);
                auto membersdb   = getMembersDb(txn, room.first);
                auto eventsDb    = getEventsDb(txn, room.first);

                saveStateEvents(txn,
                                statesdb,
                                stateskeydb,
                                membersdb,
                                eventsDb,
                                room.first,
                                room.second.state.events);
                saveStateEvents(txn,
                                statesdb,
                                stateskeydb,
                                membersdb,
                                eventsDb,
                                room.first,
                                room.second.timeline.events);

                saveTimelineMessages(txn, eventsDb, room.first, room.second.timeline);

                RoomInfo updatedInfo;
                updatedInfo.name       = getRoomName(txn, statesdb, membersdb).toStdString();
                updatedInfo.topic      = getRoomTopic(txn, statesdb).toStdString();
                updatedInfo.avatar_url = getRoomAvatarUrl(txn, statesdb, membersdb).toStdString();
                updatedInfo.version    = getRoomVersion(txn, statesdb).toStdString();
                updatedInfo.is_space   = getRoomIsSpace(txn, statesdb);

                if (updatedInfo.is_space) {
                        bool space_updates = false;
                        for (const auto &e : room.second.state.events)
                                if (std::holds_alternative<StateEvent<state::space::Child>>(e) ||
                                    std::holds_alternative<StateEvent<state::PowerLevels>>(e))
                                        space_updates = true;
                        for (const auto &e : room.second.timeline.events)
                                if (std::holds_alternative<StateEvent<state::space::Child>>(e) ||
                                    std::holds_alternative<StateEvent<state::PowerLevels>>(e))
                                        space_updates = true;

                        if (space_updates)
                                spaces_with_updates.insert(room.first);
                }

                {
                        bool room_has_space_update = false;
                        for (const auto &e : room.second.state.events) {
                                if (auto se = std::get_if<StateEvent<state::space::Parent>>(&e)) {
                                        spaces_with_updates.insert(se->state_key);
                                        room_has_space_update = true;
                                }
                        }
                        for (const auto &e : room.second.timeline.events) {
                                if (auto se = std::get_if<StateEvent<state::space::Parent>>(&e)) {
                                        spaces_with_updates.insert(se->state_key);
                                        room_has_space_update = true;
                                }
                        }

                        if (room_has_space_update)
                                rooms_with_space_updates.insert(room.first);
                }

                bool has_new_tags = false;
                // Process the account_data associated with this room
                if (!room.second.account_data.events.empty()) {
                        auto accountDataDb = getAccountDataDb(txn, room.first);

                        for (const auto &evt : room.second.account_data.events) {
                                std::visit(
                                  [&txn, &accountDataDb](const auto &event) {
                                          auto j = json(event);
                                          accountDataDb.put(
                                            txn, j["type"].get<std::string>(), j.dump());
                                  },
                                  evt);

                                // for tag events
                                if (std::holds_alternative<AccountDataEvent<account_data::Tags>>(
                                      evt)) {
                                        auto tags_evt =
                                          std::get<AccountDataEvent<account_data::Tags>>(evt);
                                        has_new_tags = true;
                                        for (const auto &tag : tags_evt.content.tags) {
                                                updatedInfo.tags.push_back(tag.first);
                                        }
                                }
                                if (auto fr = std::get_if<mtx::events::AccountDataEvent<
                                      mtx::events::account_data::FullyRead>>(&evt)) {
                                        nhlog::db()->debug("Fully read: {}", fr->content.event_id);
                                }
                        }
                }
                if (!has_new_tags) {
                        // retrieve the old tags, they haven't changed
                        std::string_view data;
                        if (roomsDb_.get(txn, room.first, data)) {
                                try {
                                        RoomInfo tmp =
                                          json::parse(std::string_view(data.data(), data.size()));
                                        updatedInfo.tags = tmp.tags;
                                } catch (const json::exception &e) {
                                        nhlog::db()->warn(
                                          "failed to parse room info: room_id ({}), {}: {}",
                                          room.first,
                                          std::string(data.data(), data.size()),
                                          e.what());
                                }
                        }
                }

                roomsDb_.put(txn, room.first, json(updatedInfo).dump());

                for (const auto &e : room.second.ephemeral.events) {
                        if (auto receiptsEv = std::get_if<
                              mtx::events::EphemeralEvent<mtx::events::ephemeral::Receipt>>(&e)) {
                                Receipts receipts;

                                for (const auto &[event_id, userReceipts] :
                                     receiptsEv->content.receipts) {
                                        for (const auto &[user_id, receipt] : userReceipts.users) {
                                                receipts[event_id][user_id] = receipt.ts;
                                        }
                                }
                                updateReadReceipt(txn, room.first, receipts);
                        }
                }

                // Clean up non-valid invites.
                removeInvite(txn, room.first);
        }

        saveInvites(txn, res.rooms.invite);

        savePresence(txn, res.presence);

        markUserKeysOutOfDate(txn, userKeyCacheDb, res.device_lists.changed, currentBatchToken);
        deleteUserKeys(txn, userKeyCacheDb, res.device_lists.left);

        removeLeftRooms(txn, res.rooms.leave);

        updateSpaces(txn, spaces_with_updates, std::move(rooms_with_space_updates));

        txn.commit();

        std::map<QString, bool> readStatus;

        for (const auto &room : res.rooms.join) {
                for (const auto &e : room.second.ephemeral.events) {
                        if (auto receiptsEv = std::get_if<
                              mtx::events::EphemeralEvent<mtx::events::ephemeral::Receipt>>(&e)) {
                                std::vector<QString> receipts;

                                for (const auto &[event_id, userReceipts] :
                                     receiptsEv->content.receipts) {
                                        for (const auto &[user_id, receipt] : userReceipts.users) {
                                                (void)receipt;

                                                if (user_id != local_user_id) {
                                                        receipts.push_back(
                                                          QString::fromStdString(event_id));
                                                        break;
                                                }
                                        }
                                }
                                if (!receipts.empty())
                                        emit newReadReceipts(QString::fromStdString(room.first),
                                                             receipts);
                        }
                }
                readStatus.emplace(QString::fromStdString(room.first),
                                   calculateRoomReadStatus(room.first));
        }

        emit roomReadStatus(readStatus);
}

void
Cache::saveInvites(lmdb::txn &txn, const std::map<std::string, mtx::responses::InvitedRoom> &rooms)
{
        for (const auto &room : rooms) {
                auto statesdb  = getInviteStatesDb(txn, room.first);
                auto membersdb = getInviteMembersDb(txn, room.first);

                saveInvite(txn, statesdb, membersdb, room.second);

                RoomInfo updatedInfo;
                updatedInfo.name  = getInviteRoomName(txn, statesdb, membersdb).toStdString();
                updatedInfo.topic = getInviteRoomTopic(txn, statesdb).toStdString();
                updatedInfo.avatar_url =
                  getInviteRoomAvatarUrl(txn, statesdb, membersdb).toStdString();
                updatedInfo.is_space  = getInviteRoomIsSpace(txn, statesdb);
                updatedInfo.is_invite = true;

                invitesDb_.put(txn, room.first, json(updatedInfo).dump());
        }
}

void
Cache::saveInvite(lmdb::txn &txn,
                  lmdb::dbi &statesdb,
                  lmdb::dbi &membersdb,
                  const mtx::responses::InvitedRoom &room)
{
        using namespace mtx::events;
        using namespace mtx::events::state;

        for (const auto &e : room.invite_state) {
                if (auto msg = std::get_if<StrippedEvent<Member>>(&e)) {
                        auto display_name = msg->content.display_name.empty()
                                              ? msg->state_key
                                              : msg->content.display_name;

                        MemberInfo tmp{display_name, msg->content.avatar_url};

                        membersdb.put(txn, msg->state_key, json(tmp).dump());
                } else {
                        std::visit(
                          [&txn, &statesdb](auto msg) {
                                  auto j = json(msg);
                                  bool res =
                                    statesdb.put(txn, j["type"].get<std::string>(), j.dump());

                                  if (!res)
                                          nhlog::db()->warn("couldn't save data: {}",
                                                            json(msg).dump());
                          },
                          e);
                }
        }
}

void
Cache::savePresence(
  lmdb::txn &txn,
  const std::vector<mtx::events::Event<mtx::events::presence::Presence>> &presenceUpdates)
{
        for (const auto &update : presenceUpdates) {
                auto presenceDb = getPresenceDb(txn);

                presenceDb.put(txn, update.sender, json(update.content).dump());
        }
}

std::vector<std::string>
Cache::roomsWithStateUpdates(const mtx::responses::Sync &res)
{
        std::vector<std::string> rooms;
        for (const auto &room : res.rooms.join) {
                bool hasUpdates = false;
                for (const auto &s : room.second.state.events) {
                        if (containsStateUpdates(s)) {
                                hasUpdates = true;
                                break;
                        }
                }

                for (const auto &s : room.second.timeline.events) {
                        if (containsStateUpdates(s)) {
                                hasUpdates = true;
                                break;
                        }
                }

                if (hasUpdates)
                        rooms.emplace_back(room.first);
        }

        for (const auto &room : res.rooms.invite) {
                for (const auto &s : room.second.invite_state) {
                        if (containsStateUpdates(s)) {
                                rooms.emplace_back(room.first);
                                break;
                        }
                }
        }

        return rooms;
}

RoomInfo
Cache::singleRoomInfo(const std::string &room_id)
{
        auto txn      = ro_txn(env_);
        auto statesdb = getStatesDb(txn, room_id);

        std::string_view data;

        // Check if the room is joined.
        if (roomsDb_.get(txn, room_id, data)) {
                try {
                        RoomInfo tmp     = json::parse(data);
                        tmp.member_count = getMembersDb(txn, room_id).size(txn);
                        tmp.join_rule    = getRoomJoinRule(txn, statesdb);
                        tmp.guest_access = getRoomGuestAccess(txn, statesdb);

                        return tmp;
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse room info: room_id ({}), {}: {}",
                                          room_id,
                                          std::string(data.data(), data.size()),
                                          e.what());
                }
        }

        return RoomInfo();
}

std::map<QString, RoomInfo>
Cache::getRoomInfo(const std::vector<std::string> &rooms)
{
        std::map<QString, RoomInfo> room_info;

        // TODO This should be read only.
        auto txn = lmdb::txn::begin(env_);

        for (const auto &room : rooms) {
                std::string_view data;
                auto statesdb = getStatesDb(txn, room);

                // Check if the room is joined.
                if (roomsDb_.get(txn, room, data)) {
                        try {
                                RoomInfo tmp     = json::parse(data);
                                tmp.member_count = getMembersDb(txn, room).size(txn);
                                tmp.join_rule    = getRoomJoinRule(txn, statesdb);
                                tmp.guest_access = getRoomGuestAccess(txn, statesdb);

                                room_info.emplace(QString::fromStdString(room), std::move(tmp));
                        } catch (const json::exception &e) {
                                nhlog::db()->warn("failed to parse room info: room_id ({}), {}: {}",
                                                  room,
                                                  std::string(data.data(), data.size()),
                                                  e.what());
                        }
                } else {
                        // Check if the room is an invite.
                        if (invitesDb_.get(txn, room, data)) {
                                try {
                                        RoomInfo tmp     = json::parse(std::string_view(data));
                                        tmp.member_count = getInviteMembersDb(txn, room).size(txn);

                                        room_info.emplace(QString::fromStdString(room),
                                                          std::move(tmp));
                                } catch (const json::exception &e) {
                                        nhlog::db()->warn("failed to parse room info for invite: "
                                                          "room_id ({}), {}: {}",
                                                          room,
                                                          std::string(data.data(), data.size()),
                                                          e.what());
                                }
                        }
                }
        }

        txn.commit();

        return room_info;
}

std::vector<QString>
Cache::roomIds()
{
        auto txn = ro_txn(env_);

        std::vector<QString> rooms;
        std::string_view room_id, unused;

        auto roomsCursor = lmdb::cursor::open(txn, roomsDb_);
        while (roomsCursor.get(room_id, unused, MDB_NEXT))
                rooms.push_back(QString::fromStdString(std::string(room_id)));

        roomsCursor.close();

        return rooms;
}

QMap<QString, mtx::responses::Notifications>
Cache::getTimelineMentions()
{
        // TODO: Should be read-only, but getMentionsDb will attempt to create a DB
        // if it doesn't exist, throwing an error.
        auto txn = lmdb::txn::begin(env_, nullptr);

        QMap<QString, mtx::responses::Notifications> notifs;

        auto room_ids = getRoomIds(txn);

        for (const auto &room_id : room_ids) {
                auto roomNotifs                         = getTimelineMentionsForRoom(txn, room_id);
                notifs[QString::fromStdString(room_id)] = roomNotifs;
        }

        txn.commit();

        return notifs;
}

std::string
Cache::previousBatchToken(const std::string &room_id)
{
        auto txn     = lmdb::txn::begin(env_, nullptr);
        auto orderDb = getEventOrderDb(txn, room_id);

        auto cursor = lmdb::cursor::open(txn, orderDb);
        std::string_view indexVal, val;
        if (!cursor.get(indexVal, val, MDB_FIRST)) {
                return "";
        }

        auto j = json::parse(val);

        return j.value("prev_batch", "");
}

Cache::Messages
Cache::getTimelineMessages(lmdb::txn &txn, const std::string &room_id, uint64_t index, bool forward)
{
        // TODO(nico): Limit the messages returned by this maybe?
        auto orderDb  = getOrderToMessageDb(txn, room_id);
        auto eventsDb = getEventsDb(txn, room_id);

        Messages messages{};

        std::string_view indexVal, event_id;

        auto cursor = lmdb::cursor::open(txn, orderDb);
        if (index == std::numeric_limits<uint64_t>::max()) {
                if (cursor.get(indexVal, event_id, forward ? MDB_FIRST : MDB_LAST)) {
                        index = lmdb::from_sv<uint64_t>(indexVal);
                } else {
                        messages.end_of_cache = true;
                        return messages;
                }
        } else {
                if (cursor.get(indexVal, event_id, MDB_SET)) {
                        index = lmdb::from_sv<uint64_t>(indexVal);
                } else {
                        messages.end_of_cache = true;
                        return messages;
                }
        }

        int counter = 0;

        bool ret;
        while ((ret = cursor.get(indexVal,
                                 event_id,
                                 counter == 0 ? (forward ? MDB_FIRST : MDB_LAST)
                                              : (forward ? MDB_NEXT : MDB_PREV))) &&
               counter++ < BATCH_SIZE) {
                std::string_view event;
                bool success = eventsDb.get(txn, event_id, event);
                if (!success)
                        continue;

                mtx::events::collections::TimelineEvent te;
                try {
                        mtx::events::collections::from_json(json::parse(event), te);
                } catch (std::exception &e) {
                        nhlog::db()->error("Failed to parse message from cache {}", e.what());
                        continue;
                }

                messages.timeline.events.push_back(std::move(te.data));
        }
        cursor.close();

        // std::reverse(timeline.events.begin(), timeline.events.end());
        messages.next_index   = lmdb::from_sv<uint64_t>(indexVal);
        messages.end_of_cache = !ret;

        return messages;
}

std::optional<mtx::events::collections::TimelineEvent>
Cache::getEvent(const std::string &room_id, const std::string &event_id)
{
        auto txn      = ro_txn(env_);
        auto eventsDb = getEventsDb(txn, room_id);

        std::string_view event{};
        bool success = eventsDb.get(txn, event_id, event);
        if (!success)
                return {};

        mtx::events::collections::TimelineEvent te;
        try {
                mtx::events::collections::from_json(json::parse(event), te);
        } catch (std::exception &e) {
                nhlog::db()->error("Failed to parse message from cache {}", e.what());
                return std::nullopt;
        }

        return te;
}
void
Cache::storeEvent(const std::string &room_id,
                  const std::string &event_id,
                  const mtx::events::collections::TimelineEvent &event)
{
        auto txn        = lmdb::txn::begin(env_);
        auto eventsDb   = getEventsDb(txn, room_id);
        auto event_json = mtx::accessors::serialize_event(event.data);
        eventsDb.put(txn, event_id, event_json.dump());
        txn.commit();
}

void
Cache::replaceEvent(const std::string &room_id,
                    const std::string &event_id,
                    const mtx::events::collections::TimelineEvent &event)
{
        auto txn         = lmdb::txn::begin(env_);
        auto eventsDb    = getEventsDb(txn, room_id);
        auto relationsDb = getRelationsDb(txn, room_id);
        auto event_json  = mtx::accessors::serialize_event(event.data).dump();

        {
                eventsDb.del(txn, event_id);
                eventsDb.put(txn, event_id, event_json);
                for (auto relation : mtx::accessors::relations(event.data).relations) {
                        relationsDb.put(txn, relation.event_id, event_id);
                }
        }

        txn.commit();
}

std::vector<std::string>
Cache::relatedEvents(const std::string &room_id, const std::string &event_id)
{
        auto txn         = ro_txn(env_);
        auto relationsDb = getRelationsDb(txn, room_id);

        std::vector<std::string> related_ids;

        auto related_cursor         = lmdb::cursor::open(txn, relationsDb);
        std::string_view related_to = event_id, related_event;
        bool first                  = true;

        try {
                if (!related_cursor.get(related_to, related_event, MDB_SET))
                        return {};

                while (related_cursor.get(
                  related_to, related_event, first ? MDB_FIRST_DUP : MDB_NEXT_DUP)) {
                        first = false;
                        if (event_id != std::string_view(related_to.data(), related_to.size()))
                                break;

                        related_ids.emplace_back(related_event.data(), related_event.size());
                }
        } catch (const lmdb::error &e) {
                nhlog::db()->error("related events error: {}", e.what());
        }

        return related_ids;
}

size_t
Cache::memberCount(const std::string &room_id)
{
        auto txn = ro_txn(env_);
        return getMembersDb(txn, room_id).size(txn);
}

QMap<QString, RoomInfo>
Cache::roomInfo(bool withInvites)
{
        QMap<QString, RoomInfo> result;

        auto txn = ro_txn(env_);

        std::string_view room_id;
        std::string_view room_data;

        // Gather info about the joined rooms.
        auto roomsCursor = lmdb::cursor::open(txn, roomsDb_);
        while (roomsCursor.get(room_id, room_data, MDB_NEXT)) {
                RoomInfo tmp     = json::parse(std::move(room_data));
                tmp.member_count = getMembersDb(txn, std::string(room_id)).size(txn);
                result.insert(QString::fromStdString(std::string(room_id)), std::move(tmp));
        }
        roomsCursor.close();

        if (withInvites) {
                // Gather info about the invites.
                auto invitesCursor = lmdb::cursor::open(txn, invitesDb_);
                while (invitesCursor.get(room_id, room_data, MDB_NEXT)) {
                        RoomInfo tmp     = json::parse(room_data);
                        tmp.member_count = getInviteMembersDb(txn, std::string(room_id)).size(txn);
                        result.insert(QString::fromStdString(std::string(room_id)), std::move(tmp));
                }
                invitesCursor.close();
        }

        return result;
}

std::string
Cache::getLastEventId(lmdb::txn &txn, const std::string &room_id)
{
        lmdb::dbi orderDb;
        try {
                orderDb = getOrderToMessageDb(txn, room_id);
        } catch (lmdb::runtime_error &e) {
                nhlog::db()->error("Can't open db for room '{}', probably doesn't exist yet. ({})",
                                   room_id,
                                   e.what());
                return {};
        }

        std::string_view indexVal, val;

        auto cursor = lmdb::cursor::open(txn, orderDb);
        if (!cursor.get(indexVal, val, MDB_LAST)) {
                return {};
        }

        return std::string(val.data(), val.size());
}

std::optional<Cache::TimelineRange>
Cache::getTimelineRange(const std::string &room_id)
{
        auto txn = ro_txn(env_);
        lmdb::dbi orderDb;
        try {
                orderDb = getOrderToMessageDb(txn, room_id);
        } catch (lmdb::runtime_error &e) {
                nhlog::db()->error("Can't open db for room '{}', probably doesn't exist yet. ({})",
                                   room_id,
                                   e.what());
                return {};
        }

        std::string_view indexVal, val;

        auto cursor = lmdb::cursor::open(txn, orderDb);
        if (!cursor.get(indexVal, val, MDB_LAST)) {
                return {};
        }

        TimelineRange range{};
        range.last = lmdb::from_sv<uint64_t>(indexVal);

        if (!cursor.get(indexVal, val, MDB_FIRST)) {
                return {};
        }
        range.first = lmdb::from_sv<uint64_t>(indexVal);

        return range;
}
std::optional<uint64_t>
Cache::getTimelineIndex(const std::string &room_id, std::string_view event_id)
{
        if (event_id.empty() || room_id.empty())
                return {};

        auto txn = ro_txn(env_);

        lmdb::dbi orderDb;
        try {
                orderDb = getMessageToOrderDb(txn, room_id);
        } catch (lmdb::runtime_error &e) {
                nhlog::db()->error("Can't open db for room '{}', probably doesn't exist yet. ({})",
                                   room_id,
                                   e.what());
                return {};
        }

        std::string_view indexVal{event_id.data(), event_id.size()}, val;

        bool success = orderDb.get(txn, indexVal, val);
        if (!success) {
                return {};
        }

        return lmdb::from_sv<uint64_t>(val);
}

std::optional<uint64_t>
Cache::getEventIndex(const std::string &room_id, std::string_view event_id)
{
        if (room_id.empty() || event_id.empty())
                return {};

        auto txn = ro_txn(env_);

        lmdb::dbi orderDb;
        try {
                orderDb = getEventToOrderDb(txn, room_id);
        } catch (lmdb::runtime_error &e) {
                nhlog::db()->error("Can't open db for room '{}', probably doesn't exist yet. ({})",
                                   room_id,
                                   e.what());
                return {};
        }

        std::string_view val;

        bool success = orderDb.get(txn, event_id, val);
        if (!success) {
                return {};
        }

        return lmdb::from_sv<uint64_t>(val);
}

std::optional<std::pair<uint64_t, std::string>>
Cache::lastInvisibleEventAfter(const std::string &room_id, std::string_view event_id)
{
        if (room_id.empty() || event_id.empty())
                return {};

        auto txn = ro_txn(env_);

        lmdb::dbi orderDb;
        lmdb::dbi eventOrderDb;
        lmdb::dbi timelineDb;
        try {
                orderDb      = getEventToOrderDb(txn, room_id);
                eventOrderDb = getEventOrderDb(txn, room_id);
                timelineDb   = getMessageToOrderDb(txn, room_id);
        } catch (lmdb::runtime_error &e) {
                nhlog::db()->error("Can't open db for room '{}', probably doesn't exist yet. ({})",
                                   room_id,
                                   e.what());
                return {};
        }

        std::string_view indexVal;

        bool success = orderDb.get(txn, event_id, indexVal);
        if (!success) {
                return {};
        }

        try {
                uint64_t prevIdx = lmdb::from_sv<uint64_t>(indexVal);
                std::string prevId{event_id};

                auto cursor = lmdb::cursor::open(txn, eventOrderDb);
                cursor.get(indexVal, MDB_SET);
                while (cursor.get(indexVal, event_id, MDB_NEXT)) {
                        std::string evId = json::parse(event_id)["event_id"].get<std::string>();
                        std::string_view temp;
                        if (timelineDb.get(txn, evId, temp)) {
                                return std::pair{prevIdx, std::string(prevId)};
                        } else {
                                prevIdx = lmdb::from_sv<uint64_t>(indexVal);
                                prevId  = std::move(evId);
                        }
                }

                return std::pair{prevIdx, std::string(prevId)};
        } catch (lmdb::runtime_error &e) {
                nhlog::db()->error(
                  "Failed to get last invisible event after {}", event_id, e.what());
                return {};
        }
}

std::optional<uint64_t>
Cache::getArrivalIndex(const std::string &room_id, std::string_view event_id)
{
        auto txn = ro_txn(env_);

        lmdb::dbi orderDb;
        try {
                orderDb = getEventToOrderDb(txn, room_id);
        } catch (lmdb::runtime_error &e) {
                nhlog::db()->error("Can't open db for room '{}', probably doesn't exist yet. ({})",
                                   room_id,
                                   e.what());
                return {};
        }

        std::string_view val;

        bool success = orderDb.get(txn, event_id, val);
        if (!success) {
                return {};
        }

        return lmdb::from_sv<uint64_t>(val);
}

std::optional<std::string>
Cache::getTimelineEventId(const std::string &room_id, uint64_t index)
{
        auto txn = ro_txn(env_);
        lmdb::dbi orderDb;
        try {
                orderDb = getOrderToMessageDb(txn, room_id);
        } catch (lmdb::runtime_error &e) {
                nhlog::db()->error("Can't open db for room '{}', probably doesn't exist yet. ({})",
                                   room_id,
                                   e.what());
                return {};
        }

        std::string_view val;

        bool success = orderDb.get(txn, lmdb::to_sv(index), val);
        if (!success) {
                return {};
        }

        return std::string(val);
}

QHash<QString, RoomInfo>
Cache::invites()
{
        QHash<QString, RoomInfo> result;

        auto txn    = ro_txn(env_);
        auto cursor = lmdb::cursor::open(txn, invitesDb_);

        std::string_view room_id, room_data;

        while (cursor.get(room_id, room_data, MDB_NEXT)) {
                try {
                        RoomInfo tmp     = json::parse(room_data);
                        tmp.member_count = getInviteMembersDb(txn, std::string(room_id)).size(txn);
                        result.insert(QString::fromStdString(std::string(room_id)), std::move(tmp));
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse room info for invite: "
                                          "room_id ({}), {}: {}",
                                          room_id,
                                          std::string(room_data),
                                          e.what());
                }
        }

        cursor.close();

        return result;
}

std::optional<RoomInfo>
Cache::invite(std::string_view roomid)
{
        std::optional<RoomInfo> result;

        auto txn = ro_txn(env_);

        std::string_view room_data;

        if (invitesDb_.get(txn, roomid, room_data)) {
                try {
                        RoomInfo tmp     = json::parse(room_data);
                        tmp.member_count = getInviteMembersDb(txn, std::string(roomid)).size(txn);
                        result           = std::move(tmp);
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse room info for invite: "
                                          "room_id ({}), {}: {}",
                                          roomid,
                                          std::string(room_data),
                                          e.what());
                }
        }

        return result;
}

QString
Cache::getRoomAvatarUrl(lmdb::txn &txn, lmdb::dbi &statesdb, lmdb::dbi &membersdb)
{
        using namespace mtx::events;
        using namespace mtx::events::state;

        std::string_view event;
        bool res = statesdb.get(txn, to_string(mtx::events::EventType::RoomAvatar), event);

        if (res) {
                try {
                        StateEvent<Avatar> msg =
                          json::parse(std::string_view(event.data(), event.size()));

                        if (!msg.content.url.empty())
                                return QString::fromStdString(msg.content.url);
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse m.room.avatar event: {}", e.what());
                }
        }

        // We don't use an avatar for group chats.
        if (membersdb.size(txn) > 2)
                return QString();

        auto cursor = lmdb::cursor::open(txn, membersdb);
        std::string_view user_id;
        std::string_view member_data;
        std::string fallback_url;

        // Resolve avatar for 1-1 chats.
        while (cursor.get(user_id, member_data, MDB_NEXT)) {
                try {
                        MemberInfo m = json::parse(member_data);
                        if (user_id == localUserId_.toStdString()) {
                                fallback_url = m.avatar_url;
                                continue;
                        }

                        cursor.close();
                        return QString::fromStdString(m.avatar_url);
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse member info: {}", e.what());
                }
        }

        cursor.close();

        // Default case when there is only one member.
        return QString::fromStdString(fallback_url);
}

QString
Cache::getRoomName(lmdb::txn &txn, lmdb::dbi &statesdb, lmdb::dbi &membersdb)
{
        using namespace mtx::events;
        using namespace mtx::events::state;

        std::string_view event;
        bool res = statesdb.get(txn, to_string(mtx::events::EventType::RoomName), event);

        if (res) {
                try {
                        StateEvent<Name> msg =
                          json::parse(std::string_view(event.data(), event.size()));

                        if (!msg.content.name.empty())
                                return QString::fromStdString(msg.content.name);
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse m.room.name event: {}", e.what());
                }
        }

        res = statesdb.get(txn, to_string(mtx::events::EventType::RoomCanonicalAlias), event);

        if (res) {
                try {
                        StateEvent<CanonicalAlias> msg =
                          json::parse(std::string_view(event.data(), event.size()));

                        if (!msg.content.alias.empty())
                                return QString::fromStdString(msg.content.alias);
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse m.room.canonical_alias event: {}",
                                          e.what());
                }
        }

        auto cursor      = lmdb::cursor::open(txn, membersdb);
        const auto total = membersdb.size(txn);

        std::size_t ii = 0;
        std::string_view user_id;
        std::string_view member_data;
        std::map<std::string, MemberInfo> members;

        while (cursor.get(user_id, member_data, MDB_NEXT) && ii < 3) {
                try {
                        members.emplace(user_id, json::parse(member_data));
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse member info: {}", e.what());
                }

                ii++;
        }

        cursor.close();

        if (total == 1 && !members.empty())
                return QString::fromStdString(members.begin()->second.name);

        auto first_member = [&members, this]() {
                for (const auto &m : members) {
                        if (m.first != localUserId_.toStdString())
                                return QString::fromStdString(m.second.name);
                }

                return localUserId_;
        }();

        if (total == 2)
                return first_member;
        else if (total > 2)
                return QString("%1 and %2 others").arg(first_member).arg(total - 1);

        return "Empty Room";
}

mtx::events::state::JoinRule
Cache::getRoomJoinRule(lmdb::txn &txn, lmdb::dbi &statesdb)
{
        using namespace mtx::events;
        using namespace mtx::events::state;

        std::string_view event;
        bool res = statesdb.get(txn, to_string(mtx::events::EventType::RoomJoinRules), event);

        if (res) {
                try {
                        StateEvent<state::JoinRules> msg = json::parse(event);
                        return msg.content.join_rule;
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse m.room.join_rule event: {}", e.what());
                }
        }
        return state::JoinRule::Knock;
}

bool
Cache::getRoomGuestAccess(lmdb::txn &txn, lmdb::dbi &statesdb)
{
        using namespace mtx::events;
        using namespace mtx::events::state;

        std::string_view event;
        bool res = statesdb.get(txn, to_string(mtx::events::EventType::RoomGuestAccess), event);

        if (res) {
                try {
                        StateEvent<GuestAccess> msg = json::parse(event);
                        return msg.content.guest_access == AccessState::CanJoin;
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse m.room.guest_access event: {}",
                                          e.what());
                }
        }
        return false;
}

QString
Cache::getRoomTopic(lmdb::txn &txn, lmdb::dbi &statesdb)
{
        using namespace mtx::events;
        using namespace mtx::events::state;

        std::string_view event;
        bool res = statesdb.get(txn, to_string(mtx::events::EventType::RoomTopic), event);

        if (res) {
                try {
                        StateEvent<Topic> msg = json::parse(event);

                        if (!msg.content.topic.empty())
                                return QString::fromStdString(msg.content.topic);
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse m.room.topic event: {}", e.what());
                }
        }

        return QString();
}

QString
Cache::getRoomVersion(lmdb::txn &txn, lmdb::dbi &statesdb)
{
        using namespace mtx::events;
        using namespace mtx::events::state;

        std::string_view event;
        bool res = statesdb.get(txn, to_string(mtx::events::EventType::RoomCreate), event);

        if (res) {
                try {
                        StateEvent<Create> msg = json::parse(event);

                        if (!msg.content.room_version.empty())
                                return QString::fromStdString(msg.content.room_version);
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse m.room.create event: {}", e.what());
                }
        }

        nhlog::db()->warn("m.room.create event is missing room version, assuming version \"1\"");
        return QString("1");
}

bool
Cache::getRoomIsSpace(lmdb::txn &txn, lmdb::dbi &statesdb)
{
        using namespace mtx::events;
        using namespace mtx::events::state;

        std::string_view event;
        bool res = statesdb.get(txn, to_string(mtx::events::EventType::RoomCreate), event);

        if (res) {
                try {
                        StateEvent<Create> msg = json::parse(event);

                        return msg.content.type == mtx::events::state::room_type::space;
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse m.room.create event: {}", e.what());
                }
        }

        nhlog::db()->warn("m.room.create event is missing room version, assuming version \"1\"");
        return false;
}

std::optional<mtx::events::state::CanonicalAlias>
Cache::getRoomAliases(const std::string &roomid)
{
        using namespace mtx::events;
        using namespace mtx::events::state;

        auto txn      = ro_txn(env_);
        auto statesdb = getStatesDb(txn, roomid);

        std::string_view event;
        bool res = statesdb.get(txn, to_string(mtx::events::EventType::RoomCanonicalAlias), event);

        if (res) {
                try {
                        StateEvent<CanonicalAlias> msg = json::parse(event);

                        return msg.content;
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse m.room.canonical_alias event: {}",
                                          e.what());
                }
        }

        return std::nullopt;
}

QString
Cache::getInviteRoomName(lmdb::txn &txn, lmdb::dbi &statesdb, lmdb::dbi &membersdb)
{
        using namespace mtx::events;
        using namespace mtx::events::state;

        std::string_view event;
        bool res = statesdb.get(txn, to_string(mtx::events::EventType::RoomName), event);

        if (res) {
                try {
                        StrippedEvent<state::Name> msg = json::parse(event);
                        return QString::fromStdString(msg.content.name);
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse m.room.name event: {}", e.what());
                }
        }

        auto cursor = lmdb::cursor::open(txn, membersdb);
        std::string_view user_id, member_data;

        while (cursor.get(user_id, member_data, MDB_NEXT)) {
                if (user_id == localUserId_.toStdString())
                        continue;

                try {
                        MemberInfo tmp = json::parse(member_data);
                        cursor.close();

                        return QString::fromStdString(tmp.name);
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse member info: {}", e.what());
                }
        }

        cursor.close();

        return QString("Empty Room");
}

QString
Cache::getInviteRoomAvatarUrl(lmdb::txn &txn, lmdb::dbi &statesdb, lmdb::dbi &membersdb)
{
        using namespace mtx::events;
        using namespace mtx::events::state;

        std::string_view event;
        bool res = statesdb.get(txn, to_string(mtx::events::EventType::RoomAvatar), event);

        if (res) {
                try {
                        StrippedEvent<state::Avatar> msg = json::parse(event);
                        return QString::fromStdString(msg.content.url);
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse m.room.avatar event: {}", e.what());
                }
        }

        auto cursor = lmdb::cursor::open(txn, membersdb);
        std::string_view user_id, member_data;

        while (cursor.get(user_id, member_data, MDB_NEXT)) {
                if (user_id == localUserId_.toStdString())
                        continue;

                try {
                        MemberInfo tmp = json::parse(member_data);
                        cursor.close();

                        return QString::fromStdString(tmp.avatar_url);
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse member info: {}", e.what());
                }
        }

        cursor.close();

        return QString();
}

QString
Cache::getInviteRoomTopic(lmdb::txn &txn, lmdb::dbi &db)
{
        using namespace mtx::events;
        using namespace mtx::events::state;

        std::string_view event;
        bool res = db.get(txn, to_string(mtx::events::EventType::RoomTopic), event);

        if (res) {
                try {
                        StrippedEvent<Topic> msg = json::parse(event);
                        return QString::fromStdString(msg.content.topic);
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse m.room.topic event: {}", e.what());
                }
        }

        return QString();
}

bool
Cache::getInviteRoomIsSpace(lmdb::txn &txn, lmdb::dbi &db)
{
        using namespace mtx::events;
        using namespace mtx::events::state;

        std::string_view event;
        bool res = db.get(txn, to_string(mtx::events::EventType::RoomCreate), event);

        if (res) {
                try {
                        StrippedEvent<Create> msg = json::parse(event);
                        return msg.content.type == mtx::events::state::room_type::space;
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse m.room.topic event: {}", e.what());
                }
        }

        return false;
}

std::vector<std::string>
Cache::joinedRooms()
{
        auto txn         = ro_txn(env_);
        auto roomsCursor = lmdb::cursor::open(txn, roomsDb_);

        std::string_view id, data;
        std::vector<std::string> room_ids;

        // Gather the room ids for the joined rooms.
        while (roomsCursor.get(id, data, MDB_NEXT))
                room_ids.emplace_back(id);

        roomsCursor.close();

        return room_ids;
}

std::optional<MemberInfo>
Cache::getMember(const std::string &room_id, const std::string &user_id)
{
        if (user_id.empty() || !env_.handle())
                return std::nullopt;

        try {
                auto txn = ro_txn(env_);

                auto membersdb = getMembersDb(txn, room_id);

                std::string_view info;
                if (membersdb.get(txn, user_id, info)) {
                        MemberInfo m = json::parse(info);
                        return m;
                }
        } catch (std::exception &e) {
                nhlog::db()->warn(
                  "Failed to read member ({}) in room ({}): {}", user_id, room_id, e.what());
        }
        return std::nullopt;
}

std::vector<RoomMember>
Cache::getMembers(const std::string &room_id, std::size_t startIndex, std::size_t len)
{
        auto txn    = ro_txn(env_);
        auto db     = getMembersDb(txn, room_id);
        auto cursor = lmdb::cursor::open(txn, db);

        std::size_t currentIndex = 0;

        const auto endIndex = std::min(startIndex + len, db.size(txn));

        std::vector<RoomMember> members;

        std::string_view user_id, user_data;
        while (cursor.get(user_id, user_data, MDB_NEXT)) {
                if (currentIndex < startIndex) {
                        currentIndex += 1;
                        continue;
                }

                if (currentIndex >= endIndex)
                        break;

                try {
                        MemberInfo tmp = json::parse(user_data);
                        members.emplace_back(
                          RoomMember{QString::fromStdString(std::string(user_id)),
                                     QString::fromStdString(tmp.name)});
                } catch (const json::exception &e) {
                        nhlog::db()->warn("{}", e.what());
                }

                currentIndex += 1;
        }

        cursor.close();

        return members;
}

bool
Cache::isRoomMember(const std::string &user_id, const std::string &room_id)
{
        try {
                auto txn = ro_txn(env_);
                auto db  = getMembersDb(txn, room_id);

                std::string_view value;
                bool res = db.get(txn, user_id, value);

                return res;
        } catch (std::exception &e) {
                nhlog::db()->warn("Failed to read member membership ({}) in room ({}): {}",
                                  user_id,
                                  room_id,
                                  e.what());
        }
        return false;
}

void
Cache::savePendingMessage(const std::string &room_id,
                          const mtx::events::collections::TimelineEvent &message)
{
        auto txn      = lmdb::txn::begin(env_);
        auto eventsDb = getEventsDb(txn, room_id);

        mtx::responses::Timeline timeline;
        timeline.events.push_back(message.data);
        saveTimelineMessages(txn, eventsDb, room_id, timeline);

        auto pending = getPendingMessagesDb(txn, room_id);

        int64_t now = QDateTime::currentMSecsSinceEpoch();
        pending.put(txn, lmdb::to_sv(now), mtx::accessors::event_id(message.data));

        txn.commit();
}

std::optional<mtx::events::collections::TimelineEvent>
Cache::firstPendingMessage(const std::string &room_id)
{
        auto txn     = lmdb::txn::begin(env_);
        auto pending = getPendingMessagesDb(txn, room_id);

        {
                auto pendingCursor = lmdb::cursor::open(txn, pending);
                std::string_view tsIgnored, pendingTxn;
                while (pendingCursor.get(tsIgnored, pendingTxn, MDB_NEXT)) {
                        auto eventsDb = getEventsDb(txn, room_id);
                        std::string_view event;
                        if (!eventsDb.get(txn, pendingTxn, event)) {
                                pending.del(txn, tsIgnored, pendingTxn);
                                continue;
                        }

                        try {
                                mtx::events::collections::TimelineEvent te;
                                mtx::events::collections::from_json(json::parse(event), te);

                                pendingCursor.close();
                                txn.commit();
                                return te;
                        } catch (std::exception &e) {
                                nhlog::db()->error("Failed to parse message from cache {}",
                                                   e.what());
                                pending.del(txn, tsIgnored, pendingTxn);
                                continue;
                        }
                }
        }

        txn.commit();

        return std::nullopt;
}

void
Cache::removePendingStatus(const std::string &room_id, const std::string &txn_id)
{
        auto txn     = lmdb::txn::begin(env_);
        auto pending = getPendingMessagesDb(txn, room_id);

        {
                auto pendingCursor = lmdb::cursor::open(txn, pending);
                std::string_view tsIgnored, pendingTxn;
                while (pendingCursor.get(tsIgnored, pendingTxn, MDB_NEXT)) {
                        if (std::string_view(pendingTxn.data(), pendingTxn.size()) == txn_id)
                                lmdb::cursor_del(pendingCursor);
                }
        }

        txn.commit();
}

void
Cache::saveTimelineMessages(lmdb::txn &txn,
                            lmdb::dbi &eventsDb,
                            const std::string &room_id,
                            const mtx::responses::Timeline &res)
{
        if (res.events.empty())
                return;

        auto relationsDb = getRelationsDb(txn, room_id);

        auto orderDb     = getEventOrderDb(txn, room_id);
        auto evToOrderDb = getEventToOrderDb(txn, room_id);
        auto msg2orderDb = getMessageToOrderDb(txn, room_id);
        auto order2msgDb = getOrderToMessageDb(txn, room_id);
        auto pending     = getPendingMessagesDb(txn, room_id);

        if (res.limited) {
                lmdb::dbi_drop(txn, orderDb, false);
                lmdb::dbi_drop(txn, evToOrderDb, false);
                lmdb::dbi_drop(txn, msg2orderDb, false);
                lmdb::dbi_drop(txn, order2msgDb, false);
                lmdb::dbi_drop(txn, pending, true);
        }

        using namespace mtx::events;
        using namespace mtx::events::state;

        std::string_view indexVal, val;
        uint64_t index = std::numeric_limits<uint64_t>::max() / 2;
        auto cursor    = lmdb::cursor::open(txn, orderDb);
        if (cursor.get(indexVal, val, MDB_LAST)) {
                index = lmdb::from_sv<uint64_t>(indexVal);
        }

        uint64_t msgIndex = std::numeric_limits<uint64_t>::max() / 2;
        auto msgCursor    = lmdb::cursor::open(txn, order2msgDb);
        if (msgCursor.get(indexVal, val, MDB_LAST)) {
                msgIndex = lmdb::from_sv<uint64_t>(indexVal);
        }

        bool first = true;
        for (const auto &e : res.events) {
                auto event  = mtx::accessors::serialize_event(e);
                auto txn_id = mtx::accessors::transaction_id(e);

                std::string event_id_val = event.value("event_id", "");
                if (event_id_val.empty()) {
                        nhlog::db()->error("Event without id!");
                        continue;
                }

                std::string_view event_id = event_id_val;

                json orderEntry        = json::object();
                orderEntry["event_id"] = event_id_val;
                if (first && !res.prev_batch.empty())
                        orderEntry["prev_batch"] = res.prev_batch;

                std::string_view txn_order;
                if (!txn_id.empty() && evToOrderDb.get(txn, txn_id, txn_order)) {
                        eventsDb.put(txn, event_id, event.dump());
                        eventsDb.del(txn, txn_id);

                        std::string_view msg_txn_order;
                        if (msg2orderDb.get(txn, txn_id, msg_txn_order)) {
                                order2msgDb.put(txn, msg_txn_order, event_id);
                                msg2orderDb.put(txn, event_id, msg_txn_order);
                                msg2orderDb.del(txn, txn_id);
                        }

                        orderDb.put(txn, txn_order, orderEntry.dump());
                        evToOrderDb.put(txn, event_id, txn_order);
                        evToOrderDb.del(txn, txn_id);

                        auto relations = mtx::accessors::relations(e);
                        if (!relations.relations.empty()) {
                                for (const auto &r : relations.relations) {
                                        if (!r.event_id.empty()) {
                                                relationsDb.del(txn, r.event_id, txn_id);
                                                relationsDb.put(txn, r.event_id, event_id);
                                        }
                                }
                        }

                        auto pendingCursor = lmdb::cursor::open(txn, pending);
                        std::string_view tsIgnored, pendingTxn;
                        while (pendingCursor.get(tsIgnored, pendingTxn, MDB_NEXT)) {
                                if (std::string_view(pendingTxn.data(), pendingTxn.size()) ==
                                    txn_id)
                                        lmdb::cursor_del(pendingCursor);
                        }
                } else if (auto redaction =
                             std::get_if<mtx::events::RedactionEvent<mtx::events::msg::Redaction>>(
                               &e)) {
                        if (redaction->redacts.empty())
                                continue;

                        std::string_view oldEvent;
                        bool success = eventsDb.get(txn, redaction->redacts, oldEvent);
                        if (!success)
                                continue;

                        mtx::events::collections::TimelineEvent te;
                        try {
                                mtx::events::collections::from_json(
                                  json::parse(std::string_view(oldEvent.data(), oldEvent.size())),
                                  te);
                                // overwrite the content and add redation data
                                std::visit(
                                  [redaction](auto &ev) {
                                          ev.unsigned_data.redacted_because = *redaction;
                                          ev.unsigned_data.redacted_by      = redaction->event_id;
                                  },
                                  te.data);
                                event = mtx::accessors::serialize_event(te.data);
                                event["content"].clear();

                        } catch (std::exception &e) {
                                nhlog::db()->error("Failed to parse message from cache {}",
                                                   e.what());
                                continue;
                        }

                        eventsDb.put(txn, redaction->redacts, event.dump());
                        eventsDb.put(txn, redaction->event_id, json(*redaction).dump());
                } else {
                        eventsDb.put(txn, event_id, event.dump());

                        ++index;

                        first = false;

                        nhlog::db()->debug("saving '{}'", orderEntry.dump());

                        cursor.put(lmdb::to_sv(index), orderEntry.dump(), MDB_APPEND);
                        evToOrderDb.put(txn, event_id, lmdb::to_sv(index));

                        // TODO(Nico): Allow blacklisting more event types in UI
                        if (!isHiddenEvent(txn, e, room_id)) {
                                ++msgIndex;
                                msgCursor.put(lmdb::to_sv(msgIndex), event_id, MDB_APPEND);

                                msg2orderDb.put(txn, event_id, lmdb::to_sv(msgIndex));
                        }

                        auto relations = mtx::accessors::relations(e);
                        if (!relations.relations.empty()) {
                                for (const auto &r : relations.relations) {
                                        if (!r.event_id.empty()) {
                                                relationsDb.put(txn, r.event_id, event_id);
                                        }
                                }
                        }
                }
        }
}

uint64_t
Cache::saveOldMessages(const std::string &room_id, const mtx::responses::Messages &res)
{
        auto txn         = lmdb::txn::begin(env_);
        auto eventsDb    = getEventsDb(txn, room_id);
        auto relationsDb = getRelationsDb(txn, room_id);

        auto orderDb     = getEventOrderDb(txn, room_id);
        auto evToOrderDb = getEventToOrderDb(txn, room_id);
        auto msg2orderDb = getMessageToOrderDb(txn, room_id);
        auto order2msgDb = getOrderToMessageDb(txn, room_id);

        std::string_view indexVal, val;
        uint64_t index = std::numeric_limits<uint64_t>::max() / 2;
        {
                auto cursor = lmdb::cursor::open(txn, orderDb);
                if (cursor.get(indexVal, val, MDB_FIRST)) {
                        index = lmdb::from_sv<uint64_t>(indexVal);
                }
        }

        uint64_t msgIndex = std::numeric_limits<uint64_t>::max() / 2;
        {
                auto msgCursor = lmdb::cursor::open(txn, order2msgDb);
                if (msgCursor.get(indexVal, val, MDB_FIRST)) {
                        msgIndex = lmdb::from_sv<uint64_t>(indexVal);
                }
        }

        if (res.chunk.empty()) {
                if (orderDb.get(txn, lmdb::to_sv(index), val)) {
                        auto orderEntry          = json::parse(val);
                        orderEntry["prev_batch"] = res.end;
                        orderDb.put(txn, lmdb::to_sv(index), orderEntry.dump());
                        txn.commit();
                }
                return index;
        }

        std::string event_id_val;
        for (const auto &e : res.chunk) {
                if (std::holds_alternative<
                      mtx::events::RedactionEvent<mtx::events::msg::Redaction>>(e))
                        continue;

                auto event                = mtx::accessors::serialize_event(e);
                event_id_val              = event["event_id"].get<std::string>();
                std::string_view event_id = event_id_val;
                eventsDb.put(txn, event_id, event.dump());

                --index;

                json orderEntry        = json::object();
                orderEntry["event_id"] = event_id_val;

                orderDb.put(txn, lmdb::to_sv(index), orderEntry.dump());
                evToOrderDb.put(txn, event_id, lmdb::to_sv(index));

                // TODO(Nico): Allow blacklisting more event types in UI
                if (!isHiddenEvent(txn, e, room_id)) {
                        --msgIndex;
                        order2msgDb.put(txn, lmdb::to_sv(msgIndex), event_id);

                        msg2orderDb.put(txn, event_id, lmdb::to_sv(msgIndex));
                }

                auto relations = mtx::accessors::relations(e);
                if (!relations.relations.empty()) {
                        for (const auto &r : relations.relations) {
                                if (!r.event_id.empty()) {
                                        relationsDb.put(txn, r.event_id, event_id);
                                }
                        }
                }
        }

        json orderEntry          = json::object();
        orderEntry["event_id"]   = event_id_val;
        orderEntry["prev_batch"] = res.end;
        orderDb.put(txn, lmdb::to_sv(index), orderEntry.dump());

        txn.commit();

        return msgIndex;
}

void
Cache::clearTimeline(const std::string &room_id)
{
        auto txn         = lmdb::txn::begin(env_);
        auto eventsDb    = getEventsDb(txn, room_id);
        auto relationsDb = getRelationsDb(txn, room_id);

        auto orderDb     = getEventOrderDb(txn, room_id);
        auto evToOrderDb = getEventToOrderDb(txn, room_id);
        auto msg2orderDb = getMessageToOrderDb(txn, room_id);
        auto order2msgDb = getOrderToMessageDb(txn, room_id);

        std::string_view indexVal, val;
        auto cursor = lmdb::cursor::open(txn, orderDb);

        bool start                   = true;
        bool passed_pagination_token = false;
        while (cursor.get(indexVal, val, start ? MDB_LAST : MDB_PREV)) {
                start = false;
                json obj;

                try {
                        obj = json::parse(std::string_view(val.data(), val.size()));
                } catch (std::exception &) {
                        // workaround bug in the initial db format, where we sometimes didn't store
                        // json...
                        obj = {{"event_id", std::string(val.data(), val.size())}};
                }

                if (passed_pagination_token) {
                        if (obj.count("event_id") != 0) {
                                std::string event_id = obj["event_id"].get<std::string>();

                                if (!event_id.empty()) {
                                        evToOrderDb.del(txn, event_id);
                                        eventsDb.del(txn, event_id);
                                        relationsDb.del(txn, event_id);

                                        std::string_view order{};
                                        bool exists = msg2orderDb.get(txn, event_id, order);
                                        if (exists) {
                                                order2msgDb.del(txn, order);
                                                msg2orderDb.del(txn, event_id);
                                        }
                                }
                        }
                        lmdb::cursor_del(cursor);
                } else {
                        if (obj.count("prev_batch") != 0)
                                passed_pagination_token = true;
                }
        }

        auto msgCursor = lmdb::cursor::open(txn, order2msgDb);
        start          = true;
        while (msgCursor.get(indexVal, val, start ? MDB_LAST : MDB_PREV)) {
                start = false;

                std::string_view eventId;
                bool innerStart = true;
                bool found      = false;
                while (cursor.get(indexVal, eventId, innerStart ? MDB_LAST : MDB_PREV)) {
                        innerStart = false;

                        json obj;
                        try {
                                obj = json::parse(std::string_view(eventId.data(), eventId.size()));
                        } catch (std::exception &) {
                                obj = {{"event_id", std::string(eventId.data(), eventId.size())}};
                        }

                        if (obj["event_id"] == std::string(val.data(), val.size())) {
                                found = true;
                                break;
                        }
                }

                if (!found)
                        break;
        }

        do {
                lmdb::cursor_del(msgCursor);
        } while (msgCursor.get(indexVal, val, MDB_PREV));

        cursor.close();
        msgCursor.close();
        txn.commit();
}

mtx::responses::Notifications
Cache::getTimelineMentionsForRoom(lmdb::txn &txn, const std::string &room_id)
{
        auto db = getMentionsDb(txn, room_id);

        if (db.size(txn) == 0) {
                return mtx::responses::Notifications{};
        }

        mtx::responses::Notifications notif;
        std::string_view event_id, msg;

        auto cursor = lmdb::cursor::open(txn, db);

        while (cursor.get(event_id, msg, MDB_NEXT)) {
                auto obj = json::parse(msg);

                if (obj.count("event") == 0)
                        continue;

                mtx::responses::Notification notification;
                mtx::responses::from_json(obj, notification);

                notif.notifications.push_back(notification);
        }
        cursor.close();

        std::reverse(notif.notifications.begin(), notif.notifications.end());

        return notif;
}

//! Add all notifications containing a user mention to the db.
void
Cache::saveTimelineMentions(const mtx::responses::Notifications &res)
{
        QMap<std::string, QList<mtx::responses::Notification>> notifsByRoom;

        // Sort into room-specific 'buckets'
        for (const auto &notif : res.notifications) {
                json val = notif;
                notifsByRoom[notif.room_id].push_back(notif);
        }

        auto txn = lmdb::txn::begin(env_);
        // Insert the entire set of mentions for each room at a time.
        QMap<std::string, QList<mtx::responses::Notification>>::const_iterator it =
          notifsByRoom.constBegin();
        auto end = notifsByRoom.constEnd();
        while (it != end) {
                nhlog::db()->debug("Storing notifications for " + it.key());
                saveTimelineMentions(txn, it.key(), std::move(it.value()));
                ++it;
        }

        txn.commit();
}

void
Cache::saveTimelineMentions(lmdb::txn &txn,
                            const std::string &room_id,
                            const QList<mtx::responses::Notification> &res)
{
        auto db = getMentionsDb(txn, room_id);

        using namespace mtx::events;
        using namespace mtx::events::state;

        for (const auto &notif : res) {
                const auto event_id = mtx::accessors::event_id(notif.event);

                // double check that we have the correct room_id...
                if (room_id.compare(notif.room_id) != 0) {
                        return;
                }

                json obj = notif;

                db.put(txn, event_id, obj.dump());
        }
}

void
Cache::markSentNotification(const std::string &event_id)
{
        auto txn = lmdb::txn::begin(env_);
        notificationsDb_.put(txn, event_id, "");
        txn.commit();
}

void
Cache::removeReadNotification(const std::string &event_id)
{
        auto txn = lmdb::txn::begin(env_);

        notificationsDb_.del(txn, event_id);

        txn.commit();
}

bool
Cache::isNotificationSent(const std::string &event_id)
{
        auto txn = ro_txn(env_);

        std::string_view value;
        bool res = notificationsDb_.get(txn, event_id, value);

        return res;
}

std::vector<std::string>
Cache::getRoomIds(lmdb::txn &txn)
{
        auto db     = lmdb::dbi::open(txn, ROOMS_DB, MDB_CREATE);
        auto cursor = lmdb::cursor::open(txn, db);

        std::vector<std::string> rooms;

        std::string_view room_id, _unused;
        while (cursor.get(room_id, _unused, MDB_NEXT))
                rooms.emplace_back(room_id);

        cursor.close();

        return rooms;
}

void
Cache::deleteOldMessages()
{
        std::string_view indexVal, val;

        auto txn      = lmdb::txn::begin(env_);
        auto room_ids = getRoomIds(txn);

        for (const auto &room_id : room_ids) {
                auto orderDb     = getEventOrderDb(txn, room_id);
                auto evToOrderDb = getEventToOrderDb(txn, room_id);
                auto o2m         = getOrderToMessageDb(txn, room_id);
                auto m2o         = getMessageToOrderDb(txn, room_id);
                auto eventsDb    = getEventsDb(txn, room_id);
                auto relationsDb = getRelationsDb(txn, room_id);
                auto cursor      = lmdb::cursor::open(txn, orderDb);

                uint64_t first, last;
                if (cursor.get(indexVal, val, MDB_LAST)) {
                        last = lmdb::from_sv<uint64_t>(indexVal);
                } else {
                        continue;
                }
                if (cursor.get(indexVal, val, MDB_FIRST)) {
                        first = lmdb::from_sv<uint64_t>(indexVal);
                } else {
                        continue;
                }

                size_t message_count = static_cast<size_t>(last - first);
                if (message_count < MAX_RESTORED_MESSAGES)
                        continue;

                bool start = true;
                while (cursor.get(indexVal, val, start ? MDB_FIRST : MDB_NEXT) &&
                       message_count-- > MAX_RESTORED_MESSAGES) {
                        start    = false;
                        auto obj = json::parse(std::string_view(val.data(), val.size()));

                        if (obj.count("event_id") != 0) {
                                std::string event_id = obj["event_id"].get<std::string>();
                                evToOrderDb.del(txn, event_id);
                                eventsDb.del(txn, event_id);

                                relationsDb.del(txn, event_id);

                                std::string_view order{};
                                bool exists = m2o.get(txn, event_id, order);
                                if (exists) {
                                        o2m.del(txn, order);
                                        m2o.del(txn, event_id);
                                }
                        }
                        cursor.del();
                }
                cursor.close();
        }
        txn.commit();
}

void
Cache::deleteOldData() noexcept
{
        try {
                deleteOldMessages();
        } catch (const lmdb::error &e) {
                nhlog::db()->error("failed to delete old messages: {}", e.what());
        }
}

void
Cache::updateSpaces(lmdb::txn &txn,
                    const std::set<std::string> &spaces_with_updates,
                    std::set<std::string> rooms_with_updates)
{
        if (spaces_with_updates.empty() && rooms_with_updates.empty())
                return;

        for (const auto &space : spaces_with_updates) {
                // delete old entries
                {
                        auto cursor         = lmdb::cursor::open(txn, spacesChildrenDb_);
                        bool first          = true;
                        std::string_view sp = space, space_child = "";

                        if (cursor.get(sp, space_child, MDB_SET)) {
                                while (cursor.get(
                                  sp, space_child, first ? MDB_FIRST_DUP : MDB_NEXT_DUP)) {
                                        first = false;
                                        spacesParentsDb_.del(txn, space_child, space);
                                }
                        }
                        cursor.close();
                        spacesChildrenDb_.del(txn, space);
                }

                for (const auto &event :
                     getStateEventsWithType<mtx::events::state::space::Child>(txn, space)) {
                        if (event.content.via.has_value() && event.state_key.size() > 3 &&
                            event.state_key.at(0) == '!') {
                                spacesChildrenDb_.put(txn, space, event.state_key);
                                spacesParentsDb_.put(txn, event.state_key, space);
                        }
                }
        }

        const auto space_event_type = to_string(mtx::events::EventType::RoomPowerLevels);

        for (const auto &room : rooms_with_updates) {
                for (const auto &event :
                     getStateEventsWithType<mtx::events::state::space::Parent>(txn, room)) {
                        if (event.content.via.has_value() && event.state_key.size() > 3 &&
                            event.state_key.at(0) == '!') {
                                const std::string &space = event.state_key;

                                auto pls =
                                  getStateEvent<mtx::events::state::PowerLevels>(txn, space);

                                if (!pls)
                                        continue;

                                if (pls->content.user_level(event.sender) >=
                                    pls->content.state_level(space_event_type)) {
                                        spacesChildrenDb_.put(txn, space, room);
                                        spacesParentsDb_.put(txn, room, space);
                                }
                        }
                }
        }
}

QMap<QString, std::optional<RoomInfo>>
Cache::spaces()
{
        auto txn = ro_txn(env_);

        QMap<QString, std::optional<RoomInfo>> ret;
        {
                auto cursor = lmdb::cursor::open(txn, spacesChildrenDb_);
                bool first  = true;
                std::string_view space_id, space_child;
                while (cursor.get(space_id, space_child, first ? MDB_FIRST : MDB_NEXT)) {
                        first = false;

                        if (!space_child.empty()) {
                                std::string_view room_data;
                                if (roomsDb_.get(txn, space_id, room_data)) {
                                        RoomInfo tmp = json::parse(std::move(room_data));
                                        ret.insert(
                                          QString::fromUtf8(space_id.data(), space_id.size()), tmp);
                                } else {
                                        ret.insert(
                                          QString::fromUtf8(space_id.data(), space_id.size()),
                                          std::nullopt);
                                }
                        }
                }
                cursor.close();
        }

        return ret;
}

std::vector<std::string>
Cache::getParentRoomIds(const std::string &room_id)
{
        auto txn = ro_txn(env_);

        std::vector<std::string> roomids;
        {
                auto cursor         = lmdb::cursor::open(txn, spacesParentsDb_);
                bool first          = true;
                std::string_view sp = room_id, space_parent;
                if (cursor.get(sp, space_parent, MDB_SET)) {
                        while (cursor.get(sp, space_parent, first ? MDB_FIRST_DUP : MDB_NEXT_DUP)) {
                                first = false;

                                if (!space_parent.empty())
                                        roomids.emplace_back(space_parent);
                        }
                }
                cursor.close();
        }

        return roomids;
}

std::vector<std::string>
Cache::getChildRoomIds(const std::string &room_id)
{
        auto txn = ro_txn(env_);

        std::vector<std::string> roomids;
        {
                auto cursor         = lmdb::cursor::open(txn, spacesChildrenDb_);
                bool first          = true;
                std::string_view sp = room_id, space_child;
                if (cursor.get(sp, space_child, MDB_SET)) {
                        while (cursor.get(sp, space_child, first ? MDB_FIRST_DUP : MDB_NEXT_DUP)) {
                                first = false;

                                if (!space_child.empty())
                                        roomids.emplace_back(space_child);
                        }
                }
                cursor.close();
        }

        return roomids;
}

std::optional<mtx::events::collections::RoomAccountDataEvents>
Cache::getAccountData(lmdb::txn &txn, mtx::events::EventType type, const std::string &room_id)
{
        try {
                auto db = getAccountDataDb(txn, room_id);

                std::string_view data;
                if (db.get(txn, to_string(type), data)) {
                        mtx::responses::utils::RoomAccountDataEvents events;
                        json j = json::array({
                          json::parse(data),
                        });
                        mtx::responses::utils::parse_room_account_data_events(j, events);
                        if (events.size() == 1)
                                return events.front();
                }
        } catch (...) {
        }
        return std::nullopt;
}

bool
Cache::hasEnoughPowerLevel(const std::vector<mtx::events::EventType> &eventTypes,
                           const std::string &room_id,
                           const std::string &user_id)
{
        using namespace mtx::events;
        using namespace mtx::events::state;

        auto txn = lmdb::txn::begin(env_);
        auto db  = getStatesDb(txn, room_id);

        int64_t min_event_level = std::numeric_limits<int64_t>::max();
        int64_t user_level      = std::numeric_limits<int64_t>::min();

        std::string_view event;
        bool res = db.get(txn, to_string(EventType::RoomPowerLevels), event);

        if (res) {
                try {
                        StateEvent<PowerLevels> msg =
                          json::parse(std::string_view(event.data(), event.size()));

                        user_level = msg.content.user_level(user_id);

                        for (const auto &ty : eventTypes)
                                min_event_level =
                                  std::min(min_event_level, msg.content.state_level(to_string(ty)));
                } catch (const json::exception &e) {
                        nhlog::db()->warn("failed to parse m.room.power_levels event: {}",
                                          e.what());
                }
        }

        txn.commit();

        return user_level >= min_event_level;
}

std::vector<std::string>
Cache::roomMembers(const std::string &room_id)
{
        auto txn = ro_txn(env_);

        std::vector<std::string> members;
        std::string_view user_id, unused;

        auto db = getMembersDb(txn, room_id);

        auto cursor = lmdb::cursor::open(txn, db);
        while (cursor.get(user_id, unused, MDB_NEXT))
                members.emplace_back(user_id);
        cursor.close();

        return members;
}

std::map<std::string, std::optional<UserKeyCache>>
Cache::getMembersWithKeys(const std::string &room_id)
{
        std::string_view keys;

        try {
                auto txn = ro_txn(env_);
                std::map<std::string, std::optional<UserKeyCache>> members;

                auto db     = getMembersDb(txn, room_id);
                auto keysDb = getUserKeysDb(txn);

                std::string_view user_id, unused;
                auto cursor = lmdb::cursor::open(txn, db);
                while (cursor.get(user_id, unused, MDB_NEXT)) {
                        auto res = keysDb.get(txn, user_id, keys);

                        if (res) {
                                members[std::string(user_id)] =
                                  json::parse(keys).get<UserKeyCache>();
                        } else {
                                members[std::string(user_id)] = {};
                        }
                }
                cursor.close();

                return members;
        } catch (std::exception &) {
                return {};
        }
}

QString
Cache::displayName(const QString &room_id, const QString &user_id)
{
        if (auto info = getMember(room_id.toStdString(), user_id.toStdString());
            info && !info->name.empty())
                return QString::fromStdString(info->name);

        return user_id;
}

std::string
Cache::displayName(const std::string &room_id, const std::string &user_id)
{
        if (auto info = getMember(room_id, user_id); info && !info->name.empty())
                return info->name;

        return user_id;
}

QString
Cache::avatarUrl(const QString &room_id, const QString &user_id)
{
        if (auto info = getMember(room_id.toStdString(), user_id.toStdString());
            info && !info->avatar_url.empty())
                return QString::fromStdString(info->avatar_url);

        return "";
}

mtx::presence::PresenceState
Cache::presenceState(const std::string &user_id)
{
        if (user_id.empty())
                return {};

        std::string_view presenceVal;

        auto txn = lmdb::txn::begin(env_);
        auto db  = getPresenceDb(txn);
        auto res = db.get(txn, user_id, presenceVal);

        mtx::presence::PresenceState state = mtx::presence::offline;

        if (res) {
                mtx::events::presence::Presence presence =
                  json::parse(std::string_view(presenceVal.data(), presenceVal.size()));
                state = presence.presence;
        }

        txn.commit();

        return state;
}

std::string
Cache::statusMessage(const std::string &user_id)
{
        if (user_id.empty())
                return {};

        std::string_view presenceVal;

        auto txn = lmdb::txn::begin(env_);
        auto db  = getPresenceDb(txn);
        auto res = db.get(txn, user_id, presenceVal);

        std::string status_msg;

        if (res) {
                mtx::events::presence::Presence presence = json::parse(presenceVal);
                status_msg                               = presence.status_msg;
        }

        txn.commit();

        return status_msg;
}

void
to_json(json &j, const UserKeyCache &info)
{
        j["device_keys"]        = info.device_keys;
        j["seen_device_keys"]   = info.seen_device_keys;
        j["master_keys"]        = info.master_keys;
        j["master_key_changed"] = info.master_key_changed;
        j["user_signing_keys"]  = info.user_signing_keys;
        j["self_signing_keys"]  = info.self_signing_keys;
        j["updated_at"]         = info.updated_at;
        j["last_changed"]       = info.last_changed;
}

void
from_json(const json &j, UserKeyCache &info)
{
        info.device_keys = j.value("device_keys", std::map<std::string, mtx::crypto::DeviceKeys>{});
        info.seen_device_keys   = j.value("seen_device_keys", std::set<std::string>{});
        info.master_keys        = j.value("master_keys", mtx::crypto::CrossSigningKeys{});
        info.master_key_changed = j.value("master_key_changed", false);
        info.user_signing_keys  = j.value("user_signing_keys", mtx::crypto::CrossSigningKeys{});
        info.self_signing_keys  = j.value("self_signing_keys", mtx::crypto::CrossSigningKeys{});
        info.updated_at         = j.value("updated_at", "");
        info.last_changed       = j.value("last_changed", "");
}

std::optional<UserKeyCache>
Cache::userKeys(const std::string &user_id)
{
        std::string_view keys;

        try {
                auto txn = ro_txn(env_);
                auto db  = getUserKeysDb(txn);
                auto res = db.get(txn, user_id, keys);

                if (res) {
                        return json::parse(keys).get<UserKeyCache>();
                } else {
                        return {};
                }
        } catch (std::exception &) {
                return {};
        }
}

void
Cache::updateUserKeys(const std::string &sync_token, const mtx::responses::QueryKeys &keyQuery)
{
        auto txn = lmdb::txn::begin(env_);
        auto db  = getUserKeysDb(txn);

        std::map<std::string, UserKeyCache> updates;

        for (const auto &[user, keys] : keyQuery.device_keys)
                updates[user].device_keys = keys;
        for (const auto &[user, keys] : keyQuery.master_keys)
                updates[user].master_keys = keys;
        for (const auto &[user, keys] : keyQuery.user_signing_keys)
                updates[user].user_signing_keys = keys;
        for (const auto &[user, keys] : keyQuery.self_signing_keys)
                updates[user].self_signing_keys = keys;

        for (auto &[user, update] : updates) {
                nhlog::db()->debug("Updated user keys: {}", user);

                auto updateToWrite = update;

                std::string_view oldKeys;
                auto res = db.get(txn, user, oldKeys);

                if (res) {
                        updateToWrite     = json::parse(oldKeys).get<UserKeyCache>();
                        auto last_changed = updateToWrite.last_changed;
                        // skip if we are tracking this and expect it to be up to date with the last
                        // sync token
                        if (!last_changed.empty() && last_changed != sync_token)
                                continue;

                        if (!updateToWrite.master_keys.keys.empty() &&
                            update.master_keys.keys != updateToWrite.master_keys.keys) {
                                nhlog::db()->debug("Master key of {} changed:\nold: {}\nnew: {}",
                                                   user,
                                                   updateToWrite.master_keys.keys.size(),
                                                   update.master_keys.keys.size());
                                updateToWrite.master_key_changed = true;
                        }

                        updateToWrite.master_keys       = update.master_keys;
                        updateToWrite.self_signing_keys = update.self_signing_keys;
                        updateToWrite.user_signing_keys = update.user_signing_keys;

                        auto oldDeviceKeys = std::move(updateToWrite.device_keys);
                        updateToWrite.device_keys.clear();

                        // Don't insert keys, which we have seen once already
                        for (const auto &[device_id, device_keys] : update.device_keys) {
                                if (oldDeviceKeys.count(device_id) &&
                                    oldDeviceKeys.at(device_id).keys == device_keys.keys) {
                                        // this is safe, since the keys are the same
                                        updateToWrite.device_keys[device_id] = device_keys;
                                } else {
                                        bool keyReused = false;
                                        for (const auto &[key_id, key] : device_keys.keys) {
                                                (void)key_id;
                                                if (updateToWrite.seen_device_keys.count(key)) {
                                                        nhlog::crypto()->warn(
                                                          "Key '{}' reused by ({}: {})",
                                                          key,
                                                          user,
                                                          device_id);
                                                        keyReused = true;
                                                        break;
                                                }
                                        }

                                        if (!keyReused && !oldDeviceKeys.count(device_id))
                                                updateToWrite.device_keys[device_id] = device_keys;
                                }

                                for (const auto &[key_id, key] : device_keys.keys) {
                                        (void)key_id;
                                        updateToWrite.seen_device_keys.insert(key);
                                }
                        }
                }
                db.put(txn, user, json(updateToWrite).dump());
        }

        txn.commit();

        std::map<std::string, VerificationStatus> tmp;
        const auto local_user = utils::localUser().toStdString();

        {
                std::unique_lock<std::mutex> lock(verification_storage.verification_storage_mtx);
                for (auto &[user_id, update] : updates) {
                        (void)update;
                        if (user_id == local_user) {
                                std::swap(tmp, verification_storage.status);
                        } else {
                                verification_storage.status.erase(user_id);
                        }
                }
        }

        for (auto &[user_id, update] : updates) {
                (void)update;
                if (user_id == local_user) {
                        for (const auto &[user, status] : tmp) {
                                (void)status;
                                emit verificationStatusChanged(user);
                        }
                }
                emit verificationStatusChanged(user_id);
        }
}

void
Cache::deleteUserKeys(lmdb::txn &txn, lmdb::dbi &db, const std::vector<std::string> &user_ids)
{
        for (const auto &user_id : user_ids)
                db.del(txn, user_id);
}

void
Cache::markUserKeysOutOfDate(lmdb::txn &txn,
                             lmdb::dbi &db,
                             const std::vector<std::string> &user_ids,
                             const std::string &sync_token)
{
        mtx::requests::QueryKeys query;
        query.token = sync_token;

        for (const auto &user : user_ids) {
                nhlog::db()->debug("Marking user keys out of date: {}", user);

                std::string_view oldKeys;
                auto res = db.get(txn, user, oldKeys);

                if (!res)
                        continue;

                auto cacheEntry =
                  json::parse(std::string_view(oldKeys.data(), oldKeys.size())).get<UserKeyCache>();
                cacheEntry.last_changed = sync_token;
                db.put(txn, user, json(cacheEntry).dump());

                query.device_keys[user] = {};
        }

        if (!query.device_keys.empty())
                http::client()->query_keys(query,
                                           [this, sync_token](const mtx::responses::QueryKeys &keys,
                                                              mtx::http::RequestErr err) {
                                                   if (err) {
                                                           nhlog::net()->warn(
                                                             "failed to query device keys: {} {}",
                                                             err->matrix_error.error,
                                                             static_cast<int>(err->status_code));
                                                           return;
                                                   }

                                                   emit userKeysUpdate(sync_token, keys);
                                           });
}

void
Cache::query_keys(const std::string &user_id,
                  std::function<void(const UserKeyCache &, mtx::http::RequestErr)> cb)
{
        auto cache_ = cache::userKeys(user_id);

        if (cache_.has_value()) {
                if (!cache_->updated_at.empty() && cache_->updated_at == cache_->last_changed) {
                        cb(cache_.value(), {});
                        return;
                }
        }

        mtx::requests::QueryKeys req;
        req.device_keys[user_id] = {};

        std::string last_changed;
        if (cache_)
                last_changed = cache_->last_changed;
        req.token = last_changed;

        // use context object so that we can disconnect again
        QObject *context{new QObject(this)};
        QObject::connect(this,
                         &Cache::verificationStatusChanged,
                         context,
                         [cb, user_id, context_ = context](std::string updated_user) mutable {
                                 if (user_id == updated_user) {
                                         context_->deleteLater();
                                         auto keys = cache::userKeys(user_id);
                                         cb(keys.value_or(UserKeyCache{}), {});
                                 }
                         });

        http::client()->query_keys(
          req,
          [cb, user_id, last_changed, this](const mtx::responses::QueryKeys &res,
                                            mtx::http::RequestErr err) {
                  if (err) {
                          nhlog::net()->warn("failed to query device keys: {},{}",
                                             mtx::errors::to_string(err->matrix_error.errcode),
                                             static_cast<int>(err->status_code));
                          cb({}, err);
                          return;
                  }

                  emit userKeysUpdate(last_changed, res);
          });
}

void
to_json(json &j, const VerificationCache &info)
{
        j["device_verified"] = info.device_verified;
        j["device_blocked"]  = info.device_blocked;
}

void
from_json(const json &j, VerificationCache &info)
{
        info.device_verified = j.at("device_verified").get<std::vector<std::string>>();
        info.device_blocked  = j.at("device_blocked").get<std::vector<std::string>>();
}

std::optional<VerificationCache>
Cache::verificationCache(const std::string &user_id)
{
        std::string_view verifiedVal;

        auto txn = lmdb::txn::begin(env_);
        auto db  = getVerificationDb(txn);

        try {
                VerificationCache verified_state;
                auto res = db.get(txn, user_id, verifiedVal);
                if (res) {
                        verified_state = json::parse(verifiedVal);
                        return verified_state;
                } else {
                        return {};
                }
        } catch (std::exception &) {
                return {};
        }
}

void
Cache::markDeviceVerified(const std::string &user_id, const std::string &key)
{
        std::string_view val;

        auto txn = lmdb::txn::begin(env_);
        auto db  = getVerificationDb(txn);

        try {
                VerificationCache verified_state;
                auto res = db.get(txn, user_id, val);
                if (res) {
                        verified_state = json::parse(val);
                }

                for (const auto &device : verified_state.device_verified)
                        if (device == key)
                                return;

                verified_state.device_verified.push_back(key);
                db.put(txn, user_id, json(verified_state).dump());
                txn.commit();
        } catch (std::exception &) {
        }

        const auto local_user = utils::localUser().toStdString();
        std::map<std::string, VerificationStatus> tmp;
        {
                std::unique_lock<std::mutex> lock(verification_storage.verification_storage_mtx);
                if (user_id == local_user) {
                        std::swap(tmp, verification_storage.status);
                } else {
                        verification_storage.status.erase(user_id);
                }
        }
        if (user_id == local_user) {
                for (const auto &[user, status] : tmp) {
                        (void)status;
                        emit verificationStatusChanged(user);
                }
        } else {
                emit verificationStatusChanged(user_id);
        }
}

void
Cache::markDeviceUnverified(const std::string &user_id, const std::string &key)
{
        std::string_view val;

        auto txn = lmdb::txn::begin(env_);
        auto db  = getVerificationDb(txn);

        try {
                VerificationCache verified_state;
                auto res = db.get(txn, user_id, val);
                if (res) {
                        verified_state = json::parse(val);
                }

                verified_state.device_verified.erase(
                  std::remove(verified_state.device_verified.begin(),
                              verified_state.device_verified.end(),
                              key),
                  verified_state.device_verified.end());

                db.put(txn, user_id, json(verified_state).dump());
                txn.commit();
        } catch (std::exception &) {
        }

        const auto local_user = utils::localUser().toStdString();
        std::map<std::string, VerificationStatus> tmp;
        {
                std::unique_lock<std::mutex> lock(verification_storage.verification_storage_mtx);
                if (user_id == local_user) {
                        std::swap(tmp, verification_storage.status);
                } else {
                        verification_storage.status.erase(user_id);
                }
        }
        if (user_id == local_user) {
                for (const auto &[user, status] : tmp) {
                        (void)status;
                        emit verificationStatusChanged(user);
                }
        } else {
                emit verificationStatusChanged(user_id);
        }
}

VerificationStatus
Cache::verificationStatus(const std::string &user_id)
{
        std::unique_lock<std::mutex> lock(verification_storage.verification_storage_mtx);
        if (verification_storage.status.count(user_id))
                return verification_storage.status.at(user_id);

        VerificationStatus status;

        if (auto verifCache = verificationCache(user_id)) {
                status.verified_devices = verifCache->device_verified;
        }

        const auto local_user = utils::localUser().toStdString();

        crypto::Trust trustlevel = crypto::Trust::Unverified;
        if (user_id == local_user) {
                status.verified_devices.push_back(http::client()->device_id());
                trustlevel = crypto::Trust::Verified;
        }

        verification_storage.status[user_id] = status;

        auto verifyAtLeastOneSig = [](const auto &toVerif,
                                      const std::map<std::string, std::string> &keys,
                                      const std::string &keyOwner) {
                if (!toVerif.signatures.count(keyOwner))
                        return false;

                for (const auto &[key_id, signature] : toVerif.signatures.at(keyOwner)) {
                        if (!keys.count(key_id))
                                continue;

                        if (mtx::crypto::ed25519_verify_signature(
                              keys.at(key_id), json(toVerif), signature))
                                return true;
                }
                return false;
        };

        try {
                // for local user verify this device_key -> our master_key -> our self_signing_key
                // -> our device_keys
                //
                // for other user verify this device_key -> our master_key -> our user_signing_key
                // -> their master_key -> their self_signing_key -> their device_keys
                //
                // This means verifying the other user adds 2 extra steps,verifying our user_signing
                // key and their master key
                auto ourKeys   = userKeys(local_user);
                auto theirKeys = userKeys(user_id);
                if (!ourKeys || !theirKeys)
                        return status;

                if (!mtx::crypto::ed25519_verify_signature(
                      olm::client()->identity_keys().ed25519,
                      json(ourKeys->master_keys),
                      ourKeys->master_keys.signatures.at(local_user)
                        .at("ed25519:" + http::client()->device_id())))
                        return status;

                auto master_keys = ourKeys->master_keys.keys;

                if (user_id != local_user) {
                        bool theirMasterKeyVerified =
                          verifyAtLeastOneSig(
                            ourKeys->user_signing_keys, master_keys, local_user) &&
                          verifyAtLeastOneSig(
                            theirKeys->master_keys, ourKeys->user_signing_keys.keys, local_user);

                        if (theirMasterKeyVerified)
                                trustlevel = crypto::Trust::Verified;
                        else if (!theirKeys->master_key_changed)
                                trustlevel = crypto::Trust::TOFU;
                        else
                                return status;

                        master_keys = theirKeys->master_keys.keys;
                }

                status.user_verified = trustlevel;

                if (!verifyAtLeastOneSig(theirKeys->self_signing_keys, master_keys, user_id))
                        return status;

                for (const auto &[device, device_key] : theirKeys->device_keys) {
                        (void)device;
                        try {
                                auto identkey =
                                  device_key.keys.at("curve25519:" + device_key.device_id);
                                if (verifyAtLeastOneSig(
                                      device_key, theirKeys->self_signing_keys.keys, user_id)) {
                                        status.verified_devices.push_back(device_key.device_id);
                                        status.verified_device_keys[identkey] = trustlevel;
                                }
                        } catch (...) {
                        }
                }

                verification_storage.status[user_id] = status;
                return status;
        } catch (std::exception &) {
                return status;
        }
}

void
to_json(json &j, const RoomInfo &info)
{
        j["name"]         = info.name;
        j["topic"]        = info.topic;
        j["avatar_url"]   = info.avatar_url;
        j["version"]      = info.version;
        j["is_invite"]    = info.is_invite;
        j["is_space"]     = info.is_space;
        j["join_rule"]    = info.join_rule;
        j["guest_access"] = info.guest_access;

        if (info.member_count != 0)
                j["member_count"] = info.member_count;

        if (info.tags.size() != 0)
                j["tags"] = info.tags;
}

void
from_json(const json &j, RoomInfo &info)
{
        info.name       = j.at("name");
        info.topic      = j.at("topic");
        info.avatar_url = j.at("avatar_url");
        info.version    = j.value(
          "version", QCoreApplication::translate("RoomInfo", "no version stored").toStdString());
        info.is_invite    = j.at("is_invite");
        info.is_space     = j.value("is_space", false);
        info.join_rule    = j.at("join_rule");
        info.guest_access = j.at("guest_access");

        if (j.count("member_count"))
                info.member_count = j.at("member_count");

        if (j.count("tags"))
                info.tags = j.at("tags").get<std::vector<std::string>>();
}

void
to_json(json &j, const ReadReceiptKey &key)
{
        j = json{{"event_id", key.event_id}, {"room_id", key.room_id}};
}

void
from_json(const json &j, ReadReceiptKey &key)
{
        key.event_id = j.at("event_id").get<std::string>();
        key.room_id  = j.at("room_id").get<std::string>();
}

void
to_json(json &j, const MemberInfo &info)
{
        j["name"]       = info.name;
        j["avatar_url"] = info.avatar_url;
}

void
from_json(const json &j, MemberInfo &info)
{
        info.name       = j.at("name");
        info.avatar_url = j.at("avatar_url");
}

void
to_json(nlohmann::json &obj, const DeviceAndMasterKeys &msg)
{
        obj["devices"]     = msg.devices;
        obj["master_keys"] = msg.master_keys;
}

void
from_json(const nlohmann::json &obj, DeviceAndMasterKeys &msg)
{
        msg.devices     = obj.at("devices").get<decltype(msg.devices)>();
        msg.master_keys = obj.at("master_keys").get<decltype(msg.master_keys)>();
}

void
to_json(nlohmann::json &obj, const SharedWithUsers &msg)
{
        obj["keys"] = msg.keys;
}

void
from_json(const nlohmann::json &obj, SharedWithUsers &msg)
{
        msg.keys = obj.at("keys").get<std::map<std::string, DeviceAndMasterKeys>>();
}

void
to_json(nlohmann::json &obj, const OutboundGroupSessionData &msg)
{
        obj["session_id"]    = msg.session_id;
        obj["session_key"]   = msg.session_key;
        obj["message_index"] = msg.message_index;
        obj["ts"]            = msg.timestamp;

        obj["initially"] = msg.initially;
        obj["currently"] = msg.currently;
}

void
from_json(const nlohmann::json &obj, OutboundGroupSessionData &msg)
{
        msg.session_id    = obj.at("session_id");
        msg.session_key   = obj.at("session_key");
        msg.message_index = obj.at("message_index");
        msg.timestamp     = obj.value("ts", 0ULL);

        msg.initially = obj.value("initially", SharedWithUsers{});
        msg.currently = obj.value("currently", SharedWithUsers{});
}

void
to_json(nlohmann::json &obj, const DevicePublicKeys &msg)
{
        obj["ed25519"]    = msg.ed25519;
        obj["curve25519"] = msg.curve25519;
}

void
from_json(const nlohmann::json &obj, DevicePublicKeys &msg)
{
        msg.ed25519    = obj.at("ed25519");
        msg.curve25519 = obj.at("curve25519");
}

void
to_json(nlohmann::json &obj, const MegolmSessionIndex &msg)
{
        obj["room_id"]    = msg.room_id;
        obj["session_id"] = msg.session_id;
        obj["sender_key"] = msg.sender_key;
}

void
from_json(const nlohmann::json &obj, MegolmSessionIndex &msg)
{
        msg.room_id    = obj.at("room_id");
        msg.session_id = obj.at("session_id");
        msg.sender_key = obj.at("sender_key");
}

void
to_json(nlohmann::json &obj, const StoredOlmSession &msg)
{
        obj["ts"] = msg.last_message_ts;
        obj["s"]  = msg.pickled_session;
}
void
from_json(const nlohmann::json &obj, StoredOlmSession &msg)
{
        msg.last_message_ts = obj.at("ts").get<uint64_t>();
        msg.pickled_session = obj.at("s").get<std::string>();
}

namespace cache {
void
init(const QString &user_id)
{
        qRegisterMetaType<RoomMember>();
        qRegisterMetaType<RoomSearchResult>();
        qRegisterMetaType<RoomInfo>();
        qRegisterMetaType<QMap<QString, RoomInfo>>();
        qRegisterMetaType<std::map<QString, RoomInfo>>();
        qRegisterMetaType<std::map<QString, mtx::responses::Timeline>>();
        qRegisterMetaType<mtx::responses::QueryKeys>();

        instance_ = std::make_unique<Cache>(user_id);
}

Cache *
client()
{
        return instance_.get();
}

std::string
displayName(const std::string &room_id, const std::string &user_id)
{
        return instance_->displayName(room_id, user_id);
}

QString
displayName(const QString &room_id, const QString &user_id)
{
        return instance_->displayName(room_id, user_id);
}
QString
avatarUrl(const QString &room_id, const QString &user_id)
{
        return instance_->avatarUrl(room_id, user_id);
}

mtx::presence::PresenceState
presenceState(const std::string &user_id)
{
        if (!instance_)
                return {};
        return instance_->presenceState(user_id);
}
std::string
statusMessage(const std::string &user_id)
{
        return instance_->statusMessage(user_id);
}

// user cache stores user keys
std::optional<UserKeyCache>
userKeys(const std::string &user_id)
{
        return instance_->userKeys(user_id);
}
void
updateUserKeys(const std::string &sync_token, const mtx::responses::QueryKeys &keyQuery)
{
        instance_->updateUserKeys(sync_token, keyQuery);
}

// device & user verification cache
std::optional<VerificationStatus>
verificationStatus(const std::string &user_id)
{
        return instance_->verificationStatus(user_id);
}

void
markDeviceVerified(const std::string &user_id, const std::string &device)
{
        instance_->markDeviceVerified(user_id, device);
}

void
markDeviceUnverified(const std::string &user_id, const std::string &device)
{
        instance_->markDeviceUnverified(user_id, device);
}

std::vector<std::string>
joinedRooms()
{
        return instance_->joinedRooms();
}

QMap<QString, RoomInfo>
roomInfo(bool withInvites)
{
        return instance_->roomInfo(withInvites);
}
QHash<QString, RoomInfo>
invites()
{
        return instance_->invites();
}

QString
getRoomName(lmdb::txn &txn, lmdb::dbi &statesdb, lmdb::dbi &membersdb)
{
        return instance_->getRoomName(txn, statesdb, membersdb);
}
mtx::events::state::JoinRule
getRoomJoinRule(lmdb::txn &txn, lmdb::dbi &statesdb)
{
        return instance_->getRoomJoinRule(txn, statesdb);
}
bool
getRoomGuestAccess(lmdb::txn &txn, lmdb::dbi &statesdb)
{
        return instance_->getRoomGuestAccess(txn, statesdb);
}
QString
getRoomTopic(lmdb::txn &txn, lmdb::dbi &statesdb)
{
        return instance_->getRoomTopic(txn, statesdb);
}
QString
getRoomAvatarUrl(lmdb::txn &txn, lmdb::dbi &statesdb, lmdb::dbi &membersdb)
{
        return instance_->getRoomAvatarUrl(txn, statesdb, membersdb);
}

std::vector<RoomMember>
getMembers(const std::string &room_id, std::size_t startIndex, std::size_t len)
{
        return instance_->getMembers(room_id, startIndex, len);
}

void
saveState(const mtx::responses::Sync &res)
{
        instance_->saveState(res);
}
bool
isInitialized()
{
        return instance_->isInitialized();
}

std::string
nextBatchToken()
{
        return instance_->nextBatchToken();
}

void
deleteData()
{
        instance_->deleteData();
}

void
removeInvite(lmdb::txn &txn, const std::string &room_id)
{
        instance_->removeInvite(txn, room_id);
}
void
removeInvite(const std::string &room_id)
{
        instance_->removeInvite(room_id);
}
void
removeRoom(lmdb::txn &txn, const std::string &roomid)
{
        instance_->removeRoom(txn, roomid);
}
void
removeRoom(const std::string &roomid)
{
        instance_->removeRoom(roomid);
}
void
removeRoom(const QString &roomid)
{
        instance_->removeRoom(roomid.toStdString());
}
void
setup()
{
        instance_->setup();
}

bool
runMigrations()
{
        return instance_->runMigrations();
}

cache::CacheVersion
formatVersion()
{
        return instance_->formatVersion();
}

void
setCurrentFormat()
{
        instance_->setCurrentFormat();
}

std::vector<QString>
roomIds()
{
        return instance_->roomIds();
}

QMap<QString, mtx::responses::Notifications>
getTimelineMentions()
{
        return instance_->getTimelineMentions();
}

//! Retrieve all the user ids from a room.
std::vector<std::string>
roomMembers(const std::string &room_id)
{
        return instance_->roomMembers(room_id);
}

//! Check if the given user has power leve greater than than
//! lowest power level of the given events.
bool
hasEnoughPowerLevel(const std::vector<mtx::events::EventType> &eventTypes,
                    const std::string &room_id,
                    const std::string &user_id)
{
        return instance_->hasEnoughPowerLevel(eventTypes, room_id, user_id);
}

void
updateReadReceipt(lmdb::txn &txn, const std::string &room_id, const Receipts &receipts)
{
        instance_->updateReadReceipt(txn, room_id, receipts);
}

UserReceipts
readReceipts(const QString &event_id, const QString &room_id)
{
        return instance_->readReceipts(event_id, room_id);
}

std::optional<uint64_t>
getEventIndex(const std::string &room_id, std::string_view event_id)
{
        return instance_->getEventIndex(room_id, event_id);
}

std::optional<std::pair<uint64_t, std::string>>
lastInvisibleEventAfter(const std::string &room_id, std::string_view event_id)
{
        return instance_->lastInvisibleEventAfter(room_id, event_id);
}

RoomInfo
singleRoomInfo(const std::string &room_id)
{
        return instance_->singleRoomInfo(room_id);
}
std::vector<std::string>
roomsWithStateUpdates(const mtx::responses::Sync &res)
{
        return instance_->roomsWithStateUpdates(res);
}

std::map<QString, RoomInfo>
getRoomInfo(const std::vector<std::string> &rooms)
{
        return instance_->getRoomInfo(rooms);
}

//! Calculates which the read status of a room.
//! Whether all the events in the timeline have been read.
bool
calculateRoomReadStatus(const std::string &room_id)
{
        return instance_->calculateRoomReadStatus(room_id);
}
void
calculateRoomReadStatus()
{
        instance_->calculateRoomReadStatus();
}

void
markSentNotification(const std::string &event_id)
{
        instance_->markSentNotification(event_id);
}
//! Removes an event from the sent notifications.
void
removeReadNotification(const std::string &event_id)
{
        instance_->removeReadNotification(event_id);
}
//! Check if we have sent a desktop notification for the given event id.
bool
isNotificationSent(const std::string &event_id)
{
        return instance_->isNotificationSent(event_id);
}

//! Add all notifications containing a user mention to the db.
void
saveTimelineMentions(const mtx::responses::Notifications &res)
{
        instance_->saveTimelineMentions(res);
}

//! Remove old unused data.
void
deleteOldMessages()
{
        instance_->deleteOldMessages();
}
void
deleteOldData() noexcept
{
        instance_->deleteOldData();
}
//! Retrieve all saved room ids.
std::vector<std::string>
getRoomIds(lmdb::txn &txn)
{
        return instance_->getRoomIds(txn);
}

//! Mark a room that uses e2e encryption.
void
setEncryptedRoom(lmdb::txn &txn, const std::string &room_id)
{
        instance_->setEncryptedRoom(txn, room_id);
}
bool
isRoomEncrypted(const std::string &room_id)
{
        return instance_->isRoomEncrypted(room_id);
}

//! Check if a user is a member of the room.
bool
isRoomMember(const std::string &user_id, const std::string &room_id)
{
        return instance_->isRoomMember(user_id, room_id);
}

//
// Outbound Megolm Sessions
//
void
saveOutboundMegolmSession(const std::string &room_id,
                          const OutboundGroupSessionData &data,
                          mtx::crypto::OutboundGroupSessionPtr &session)
{
        instance_->saveOutboundMegolmSession(room_id, data, session);
}
OutboundGroupSessionDataRef
getOutboundMegolmSession(const std::string &room_id)
{
        return instance_->getOutboundMegolmSession(room_id);
}
bool
outboundMegolmSessionExists(const std::string &room_id) noexcept
{
        return instance_->outboundMegolmSessionExists(room_id);
}
void
updateOutboundMegolmSession(const std::string &room_id,
                            const OutboundGroupSessionData &data,
                            mtx::crypto::OutboundGroupSessionPtr &session)
{
        instance_->updateOutboundMegolmSession(room_id, data, session);
}
void
dropOutboundMegolmSession(const std::string &room_id)
{
        instance_->dropOutboundMegolmSession(room_id);
}

void
importSessionKeys(const mtx::crypto::ExportedSessionKeys &keys)
{
        instance_->importSessionKeys(keys);
}
mtx::crypto::ExportedSessionKeys
exportSessionKeys()
{
        return instance_->exportSessionKeys();
}

//
// Inbound Megolm Sessions
//
void
saveInboundMegolmSession(const MegolmSessionIndex &index,
                         mtx::crypto::InboundGroupSessionPtr session)
{
        instance_->saveInboundMegolmSession(index, std::move(session));
}
mtx::crypto::InboundGroupSessionPtr
getInboundMegolmSession(const MegolmSessionIndex &index)
{
        return instance_->getInboundMegolmSession(index);
}
bool
inboundMegolmSessionExists(const MegolmSessionIndex &index)
{
        return instance_->inboundMegolmSessionExists(index);
}

//
// Olm Sessions
//
void
saveOlmSession(const std::string &curve25519,
               mtx::crypto::OlmSessionPtr session,
               uint64_t timestamp)
{
        instance_->saveOlmSession(curve25519, std::move(session), timestamp);
}
std::vector<std::string>
getOlmSessions(const std::string &curve25519)
{
        return instance_->getOlmSessions(curve25519);
}
std::optional<mtx::crypto::OlmSessionPtr>
getOlmSession(const std::string &curve25519, const std::string &session_id)
{
        return instance_->getOlmSession(curve25519, session_id);
}
std::optional<mtx::crypto::OlmSessionPtr>
getLatestOlmSession(const std::string &curve25519)
{
        return instance_->getLatestOlmSession(curve25519);
}

void
saveOlmAccount(const std::string &pickled)
{
        instance_->saveOlmAccount(pickled);
}
std::string
restoreOlmAccount()
{
        return instance_->restoreOlmAccount();
}

void
storeSecret(const std::string &name, const std::string &secret)
{
        instance_->storeSecret(name, secret);
}
std::optional<std::string>
secret(const std::string &name)
{
        return instance_->secret(name);
}
} // namespace cache
